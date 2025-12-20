#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/RenderFormatSet.h"
#include "ui/RemoteInputDevice.h"
#include "ui/SimPlayground.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/PhysicsControls.h"
#include "ui/state-machine/StateMachine.h"
#include <atomic>
#include <cassert>
#include <nlohmann/json.hpp>

namespace DirtSim {
namespace Ui {
namespace State {

void SimRunning::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Simulation is running, displaying world updates");

    // Subscribe to render messages from the server (synchronous call).
    auto& wsService = sm.getWebSocketService();
    if (wsService.isConnected()) {
        static std::atomic<uint64_t> nextId{ 1 };
        Api::RenderFormatSet::Command cmd;
        cmd.format = debugDrawEnabled ? RenderFormat::DEBUG : RenderFormat::BASIC;

        // Send binary command and wait for response.
        auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
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

    // Create playground if not already created.
    if (!playground_) {
        // Ensure simulation layout is created first (this creates the IconRail).
        sm.getUiComponentManager()->getSimulationContainer();

        // Now create playground and connect to IconRail.
        playground_ = std::make_unique<SimPlayground>(
            sm.getUiComponentManager(), &sm.getWebSocketService(), sm);
        playground_->connectToIconRail();
        LOG_INFO(State, "Created simulation playground");
    }
}

State::Any SimRunning::onEvent(const ServerDisconnectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_WARN(State, "Server disconnected (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    // Lost connection - go back to Disconnected state (world is lost).
    return Disconnected{};
}

State::Any SimRunning::onEvent(const UiApi::DrawDebugToggle::Cwc& cwc, StateMachine& sm)
{
    using Response = UiApi::DrawDebugToggle::Response;

    debugDrawEnabled = cwc.command.enabled;
    LOG_INFO(State, "Debug draw mode {}", debugDrawEnabled ? "enabled" : "disabled");

    // Auto-switch render format based on debug mode (synchronous call).
    auto& wsServiceRef = sm.getWebSocketService();
    if (wsServiceRef.isConnected()) {
        static std::atomic<uint64_t> nextId{ 1 };
        Api::RenderFormatSet::Command cmd;
        cmd.format = debugDrawEnabled ? RenderFormat::DEBUG : RenderFormat::BASIC;

        // Send binary command and wait for response.
        auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
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
    if (playground_) {
        playground_->setRenderMode(mode);
    }

    cwc.sendResponse(Response::okay(UiApi::PixelRendererToggle::Okay{ cwc.command.enabled }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::RenderModeSelect::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::RenderModeSelect::Response;

    if (playground_) {
        playground_->setRenderMode(cwc.command.mode);
    }

    cwc.sendResponse(Response::okay(UiApi::RenderModeSelect::Okay{ cwc.command.mode }));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any SimRunning::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    LOG_DEBUG(State, "Mouse down at ({}, {})", cwc.command.pixelX, cwc.command.pixelY);

    // Update remote input device state (enables LVGL widget interaction).
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(true);
    }

    cwc.sendResponse(UiApi::MouseDown::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    LOG_DEBUG(State, "Mouse move at ({}, {})", cwc.command.pixelX, cwc.command.pixelY);

    // Update remote input device position (enables LVGL widget interaction).
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
    }

    cwc.sendResponse(UiApi::MouseMove::Response::okay(std::monostate{}));
    return std::move(*this);
}

State::Any SimRunning::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    LOG_DEBUG(State, "Mouse up at ({}, {})", cwc.command.pixelX, cwc.command.pixelY);

    // Update remote input device state (enables LVGL widget interaction).
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(false);
    }

    cwc.sendResponse(UiApi::MouseUp::Response::okay(std::monostate{}));
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

State::Any SimRunning::onEvent(const PhysicsSettingsReceivedEvent& evt, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Received PhysicsSettings from server (gravity={:.2f})", evt.settings.gravity);

    // Update UI controls with server settings.
    if (playground_) {
        playground_->updatePhysicsPanels(evt.settings);
    }
    else {
        LOG_WARN(State, "Playground not available");
    }

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

    // Update and render via playground.
    if (playground_ && worldData) {
        // Update controls with new world state.
        sm.getTimers().startTimer("update_controls");
        playground_->updateFromWorldData(*worldData, evt.scenario_id, evt.scenario_config, smoothedUiFps);
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
    }

    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
