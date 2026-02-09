#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/CellSet.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/SeedAdd.h"
#include "server/api/SimStop.h"
#include "ui/InteractionMode.h"
#include "ui/RemoteInputDevice.h"
#include "ui/SimPlayground.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <atomic>
#include <cassert>
#include <nlohmann/json.hpp>
#include <optional>

namespace DirtSim {
namespace Ui {
namespace State {

namespace {
std::optional<Vector2i> resolveSeedTarget(
    const WorldData& data, const UiApi::PlantSeed::Command& cmd, std::string& error)
{
    if (cmd.x.has_value() != cmd.y.has_value()) {
        error = "PlantSeed requires both x and y when specifying a position";
        return std::nullopt;
    }

    if (cmd.x.has_value()) {
        const int x = cmd.x.value();
        const int y = cmd.y.value();
        if (!data.inBounds(x, y)) {
            error = "PlantSeed position out of bounds";
            return std::nullopt;
        }
        return Vector2i{ x, y };
    }

    const int centerX = data.width / 2;
    const int centerY = data.height / 2;
    if (!data.inBounds(centerX, centerY)) {
        error = "PlantSeed resolved position out of bounds";
        return std::nullopt;
    }

    return Vector2i{ centerX, centerY };
}
} // namespace

void SimRunning::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Simulation is running, displaying world updates");

    // Subscribe to render messages from the server (synchronous call).
    auto& wsService = sm.getWebSocketService();
    if (wsService.isConnected()) {
        static std::atomic<uint64_t> nextId{ 1 };
        Api::RenderFormatSet::Command cmd;
        cmd.format =
            debugDrawEnabled ? RenderFormat::EnumType::Debug : RenderFormat::EnumType::Basic;

        // Send binary command and wait for response.
        auto envelope = DirtSim::Network::make_command_envelope(nextId.fetch_add(1), cmd);
        auto result = wsService.sendBinaryAndReceive(envelope);
        if (result.isError()) {
            LOG_ERROR(State, "Failed to send RenderFormatSet: {}", result.errorValue());
            assert(false && "Failed to send RenderFormatSet command");
        }
        else {
            LOG_INFO(
                State,
                "Subscribed to render messages (format={})",
                debugDrawEnabled ? "DEBUG" : "BASIC");
        }
    }

    if (!playground_) {
        auto* uiManager = sm.getUiComponentManager();
        DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");
        uiManager->getSimulationContainer();

        playground_ = std::make_unique<SimPlayground>(
            uiManager, &sm.getWebSocketService(), sm, &sm.getFractalAnimator());
        DIRTSIM_ASSERT(playground_, "SimPlayground creation failed");

        IconRail* iconRail = uiManager->getIconRail();
        DIRTSIM_ASSERT(iconRail, "IconRail must exist");
        iconRail->setLayout(RailLayout::SingleColumn);
        iconRail->setVisibleIcons({ IconId::CORE, IconId::SCENARIO, IconId::PHYSICS });
        iconRail->deselectAll(); // Start fresh, no panel open.

        LOG_INFO(State, "Created simulation playground");
    }
}

void SimRunning::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting SimRunning state");

    playground_.reset();

    // Clear panel content after playground cleanup.
    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
        }
    }
}

State::Any SimRunning::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    // Tree icon has special behavior - toggles neural grid.
    if (evt.selectedId == IconId::TREE) {
        uiManager->setNeuralGridVisible(true);
        // Don't show expandable panel for tree.
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->hide();
        }
    }
    else if (evt.previousId == IconId::TREE && evt.selectedId != IconId::TREE) {
        // Switched away from tree - hide neural grid.
        uiManager->setNeuralGridVisible(false);
    }

    // Show/hide expandable panel based on selection.
    if (auto* panel = uiManager->getExpandablePanel()) {
        if (evt.selectedId != IconId::NONE && evt.selectedId != IconId::TREE) {
            panel->show();
        }
        else if (evt.selectedId == IconId::NONE) {
            panel->hide();
        }
    }

    // Notify playground about the selection change for panel content updates.
    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    playground_->onIconSelected(evt.selectedId, evt.previousId);

    return std::move(*this);
}

State::Any SimRunning::onEvent(const RailModeChangedEvent& evt, StateMachine& /*sm*/)
{
    LOG_INFO(
        State,
        "IconRail mode changed to: {}",
        evt.newMode == RailMode::Minimized ? "Minimized" : "Normal");

    // Trigger display resize for auto-scaling scenarios.
    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    playground_->sendDisplayResizeUpdate();

    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::DrawDebugToggle::Cwc& cwc, StateMachine& sm)
{
    using Response = UiApi::DrawDebugToggle::Response;

    // If no callback (server-pushed command), toggle. Otherwise use explicit value.
    if (!cwc.callback) {
        debugDrawEnabled = !debugDrawEnabled;
    }
    else {
        debugDrawEnabled = cwc.command.enabled;
    }
    LOG_INFO(State, "Debug draw mode {}", debugDrawEnabled ? "enabled" : "disabled");

    // Auto-switch render format based on debug mode (synchronous call).
    auto& wsServiceRef = sm.getWebSocketService();
    if (wsServiceRef.isConnected()) {
        static std::atomic<uint64_t> nextId{ 1 };
        Api::RenderFormatSet::Command cmd;
        cmd.format =
            debugDrawEnabled ? RenderFormat::EnumType::Debug : RenderFormat::EnumType::Basic;

        // Send binary command and wait for response.
        auto envelope = DirtSim::Network::make_command_envelope(nextId.fetch_add(1), cmd);
        auto result = wsServiceRef.sendBinaryAndReceive(envelope);
        if (result.isError()) {
            LOG_ERROR(State, "Failed to send RenderFormatSet: {}", result.errorValue());
            assert(false && "Failed to send RenderFormatSet command");
        }
        else {
            LOG_INFO(State, "Sent RenderFormatSet to {}", debugDrawEnabled ? "DEBUG" : "BASIC");
        }
    }

    cwc.sendResponse(Response::okay(UiApi::DrawDebugToggle::Okay{ debugDrawEnabled }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::PixelRendererToggle::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::PixelRendererToggle::Response;

    // DEPRECATED: Convert old boolean API to new RenderMode for backward compatibility.
    RenderMode mode = cwc.command.enabled ? RenderMode::SHARP : RenderMode::LVGL_DEBUG;
    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    playground_->setRenderMode(mode);

    cwc.sendResponse(Response::okay(UiApi::PixelRendererToggle::Okay{ cwc.command.enabled }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::RenderModeSelect::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::RenderModeSelect::Response;

    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    playground_->setRenderMode(cwc.command.mode);

    cwc.sendResponse(Response::okay(UiApi::RenderModeSelect::Okay{ cwc.command.mode }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Mouse down at ({}, {}) button={}",
        cwc.command.pixelX,
        cwc.command.pixelY,
        static_cast<int>(cwc.command.button));

    activeMouseButton = cwc.command.button;

    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(true);
    }

    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    if (playground_->getInteractionMode() == InteractionMode::DRAW) {
        LOG_INFO(State, "Draw mode active");
        auto cell = playground_->pixelToCell(cwc.command.pixelX, cwc.command.pixelY);
        if (cell) {
            Material::EnumType material = (cwc.command.button == UiApi::MouseButton::LEFT)
                ? Material::EnumType::Wall
                : Material::EnumType::Air;

            static std::atomic<uint64_t> nextId{ 1 };
            Api::CellSet::Command cmd{ cell->x, cell->y, material, 1.0 };
            auto envelope = DirtSim::Network::make_command_envelope(nextId.fetch_add(1), cmd);
            sm.getWebSocketService().sendBinary(DirtSim::Network::serialize_envelope(envelope));

            LOG_INFO(State, "Draw: cell ({}, {}) -> {}", cell->x, cell->y, toString(material));
        }
        else {
            LOG_WARN(
                State,
                "Draw mode active but pixel ({}, {}) is outside world",
                cwc.command.pixelX,
                cwc.command.pixelY);
        }
    }

    cwc.sendResponse(UiApi::MouseDown::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    LOG_DEBUG(State, "Mouse move at ({}, {})", cwc.command.pixelX, cwc.command.pixelY);

    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
    }

    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    if (activeMouseButton.has_value()
        && playground_->getInteractionMode() == InteractionMode::DRAW) {
        auto cell = playground_->pixelToCell(cwc.command.pixelX, cwc.command.pixelY);
        if (cell) {
            Material::EnumType material = (*activeMouseButton == UiApi::MouseButton::LEFT)
                ? Material::EnumType::Wall
                : Material::EnumType::Air;

            static std::atomic<uint64_t> nextId{ 1 };
            Api::CellSet::Command cmd{ cell->x, cell->y, material, 1.0 };
            auto envelope = DirtSim::Network::make_command_envelope(nextId.fetch_add(1), cmd);
            sm.getWebSocketService().sendBinary(DirtSim::Network::serialize_envelope(envelope));
        }
    }

    cwc.sendResponse(UiApi::MouseMove::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    LOG_DEBUG(State, "Mouse up at ({}, {})", cwc.command.pixelX, cwc.command.pixelY);

    activeMouseButton = std::nullopt;

    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(false);
    }

    cwc.sendResponse(UiApi::MouseUp::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::PlantSeed::Cwc& cwc, StateMachine& sm)
{
    using Response = UiApi::PlantSeed::Response;

    std::string error;
    std::optional<Vector2i> target;
    if (!worldData) {
        if (cwc.command.x.has_value() != cwc.command.y.has_value()) {
            error = "PlantSeed requires both x and y when specifying a position";
            cwc.sendResponse(Response::error(ApiError(error)));
            return std::move(*this);
        }
        if (!cwc.command.x.has_value()) {
            cwc.sendResponse(Response::error(ApiError("PlantSeed requires world data")));
            return std::move(*this);
        }
        target = Vector2i{ cwc.command.x.value(), cwc.command.y.value() };
    }
    else {
        target = resolveSeedTarget(*worldData, cwc.command, error);
        if (!target.has_value()) {
            cwc.sendResponse(Response::error(ApiError(error)));
            return std::move(*this);
        }
    }

    Api::SeedAdd::Command cmd{
        .x = target.value().x,
        .y = target.value().y,
        .genome_id = std::nullopt,
    };

    const auto result =
        sm.getWebSocketService().sendCommandAndGetResponse<Api::SeedAdd::OkayType>(cmd, 2000);
    if (result.isError()) {
        LOG_ERROR(State, "PlantSeed failed: {}", result.errorValue());
        cwc.sendResponse(Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    if (result.value().isError()) {
        LOG_ERROR(State, "PlantSeed error: {}", result.value().errorValue().message);
        cwc.sendResponse(Response::error(result.value().errorValue()));
        return std::move(*this);
    }

    LOG_INFO(State, "PlantSeed sent to server at ({}, {})", cmd.x, cmd.y);
    cwc.sendResponse(Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::SimPause::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "SimPause command received, pausing simulation");

    // TODO: Send pause command to DSSM server.

    cwc.sendResponse(UiApi::SimPause::Response::okay({ true }));

    // Transition to Paused state (keep renderer for when we resume).
    return Paused{ std::move(worldData) };
}

State::Any SimRunning::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "SimStop command received, stopping server simulation");

    // Tell the server to stop the simulation.
    auto& wsService = sm.getWebSocketService();
    if (wsService.isConnected()) {
        Api::SimStop::Command cmd;
        const auto result = wsService.sendCommandAndGetResponse<Api::SimStop::OkayType>(cmd, 2000);
        if (result.isError()) {
            LOG_ERROR(State, "Failed to send SimStop to server: {}", result.errorValue());
        }
        else if (result.value().isError()) {
            LOG_ERROR(State, "Server SimStop error: {}", result.value().errorValue().message);
        }
        else {
            LOG_INFO(State, "Server simulation stopped");
        }
    }

    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));

    // Transition to StartMenu state.
    return StartMenu{};
}

State::Any SimRunning::onEvent(const PhysicsSettingsReceivedEvent& evt, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Received PhysicsSettings from server (gravity={:.2f})", evt.settings.gravity);

    // Update UI controls with server settings.
    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    playground_->updatePhysicsPanels(evt.settings);

    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiUpdateEvent& evt, StateMachine& sm)
{
    LOG_DEBUG(State, "Received world update (step {}) via push", evt.stepCount);

    // Calculate UI FPS based on time between updates.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime);

    if (elapsed.count() > 0) {
        measuredUiFps = 1000.0 / elapsed.count();

        // Exponentially weighted moving average (90% old, 10% new) for smooth display.
        if (smoothedUiFps == 0.0) {
            smoothedUiFps = measuredUiFps; // Initialize.
        }
        else {
            smoothedUiFps = 0.9 * smoothedUiFps + 0.1 * measuredUiFps;
        }

        LOG_DEBUG(State, "UI FPS: {:.1f} (smoothed: {:.1f})", measuredUiFps, smoothedUiFps);
    }

    lastFrameTime = now;

    updateCount++;
    // Log performance stats every once in a while.
    if (updateCount % 1000 == 0) {
        auto& timers = sm.getTimers();

        // Get current stats.
        double parseTotal = timers.getAccumulatedTime("parse_message");
        uint32_t parseCount = timers.getCallCount("parse_message");
        double renderTotal = timers.getAccumulatedTime("render_world");
        uint32_t renderCount = timers.getCallCount("render_world");

        // Calculate interval stats.
        static double lastParseTotal = 0.0;
        static uint32_t lastParseCount = 0;
        static double lastRenderTotal = 0.0;
        static uint32_t lastRenderCount = 0;

        double intervalParseTime = parseTotal - lastParseTotal;
        uint32_t intervalParseCount = parseCount - lastParseCount;
        double intervalRenderTime = renderTotal - lastRenderTotal;
        uint32_t intervalRenderCount = renderCount - lastRenderCount;

        // Get additional timing info.
        double copyTotal = timers.getAccumulatedTime("copy_worlddata");
        uint32_t copyCount = timers.getCallCount("copy_worlddata");
        double updateTotal = timers.getAccumulatedTime("update_controls");
        uint32_t updateCount_ = timers.getCallCount("update_controls");

        static double lastCopyTotal = 0.0;
        static uint32_t lastCopyCount = 0;
        static double lastUpdateTotal = 0.0;
        static uint32_t lastUpdateCount = 0;

        double intervalCopyTime = copyTotal - lastCopyTotal;
        uint32_t intervalCopyCount = copyCount - lastCopyCount;
        double intervalUpdateTime = updateTotal - lastUpdateTotal;
        uint32_t intervalUpdateCount = updateCount_ - lastUpdateCount;

        LOG_INFO(State, "UI Performance Stats (last n updates, total {}):", updateCount);
        LOG_INFO(
            State,
            "  Message parse: {:.1f}ms avg ({} calls, {:.1f}ms interval)",
            intervalParseCount > 0 ? intervalParseTime / intervalParseCount : 0.0,
            intervalParseCount,
            intervalParseTime);
        LOG_INFO(
            State,
            "  WorldData copy: {:.1f}ms avg ({} calls, {:.1f}ms interval)",
            intervalCopyCount > 0 ? intervalCopyTime / intervalCopyCount : 0.0,
            intervalCopyCount,
            intervalCopyTime);
        LOG_INFO(
            State,
            "  Update controls: {:.1f}ms avg ({} calls, {:.1f}ms interval)",
            intervalUpdateCount > 0 ? intervalUpdateTime / intervalUpdateCount : 0.0,
            intervalUpdateCount,
            intervalUpdateTime);
        LOG_INFO(
            State,
            "  World render: {:.1f}ms avg ({} calls, {:.1f}ms interval)",
            intervalRenderCount > 0 ? intervalRenderTime / intervalRenderCount : 0.0,
            intervalRenderCount,
            intervalRenderTime);

        lastCopyTotal = copyTotal;
        lastCopyCount = copyCount;
        lastUpdateTotal = updateTotal;
        lastUpdateCount = updateCount_;

        // Store current totals for next interval.
        lastParseTotal = parseTotal;
        lastParseCount = parseCount;
        lastRenderTotal = renderTotal;
        lastRenderCount = renderCount;
    }

    // Update local worldData with received state.
    sm.getTimers().startTimer("copy_worlddata");
    worldData = std::make_unique<WorldData>(evt.worldData);
    sm.getTimers().stopTimer("copy_worlddata");
    scenarioId = evt.scenario_id;

    // Update and render via playground.
    DIRTSIM_ASSERT(playground_, "playground_ must be set in SimRunning");
    DIRTSIM_ASSERT(worldData, "worldData must be set in SimRunning after UiUpdate");

    // Update controls with new world state.
    sm.getTimers().startTimer("update_controls");
    playground_->updateFromWorldData(
        *worldData, evt.scenario_id, evt.scenario_config, smoothedUiFps);
    sm.getTimers().stopTimer("update_controls");

    // Render world.
    sm.getTimers().startTimer("render_world");
    playground_->render(*worldData, debugDrawEnabled);
    sm.getTimers().stopTimer("render_world");

    // Render neural grid (tree vision).
    sm.getTimers().startTimer("render_neural_grid");
    playground_->renderNeuralGrid(*worldData);
    sm.getTimers().stopTimer("render_neural_grid");

    LOG_DEBUG(
        State,
        "Rendered world ({}x{}, step {})",
        worldData->width,
        worldData->height,
        worldData->timestep);

    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
