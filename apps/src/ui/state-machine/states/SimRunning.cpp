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
#include <cassert>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>

namespace DirtSim {
namespace Ui {
namespace State {

double LatencyAccumulator::average(uint32_t sampleCount) const
{
    if (sampleCount == 0) {
        return 0.0;
    }
    return total / sampleCount;
}

void LatencyAccumulator::record(double sample)
{
    total += sample;
    totalSq += sample * sample;
    if (sample < min) {
        min = sample;
    }
    if (sample > max) {
        max = sample;
    }
}

void LatencyAccumulator::reset()
{
    max = 0.0;
    min = 1e9;
    total = 0.0;
    totalSq = 0.0;
}

double LatencyAccumulator::stddev(uint32_t sampleCount) const
{
    if (sampleCount == 0) {
        return 0.0;
    }

    const double avg = average(sampleCount);
    const double variance = (totalSq / sampleCount) - (avg * avg);
    return variance > 0.0 ? std::sqrt(variance) : 0.0;
}

void DisplayFrameStagingStats::reset()
{
    handledAfterRenderCount = 0;
    handledBeforeRenderCount = 0;
    handledOtherPhaseCount = 0;
    sampleCount = 0;
    receiveToUiApplyMs.reset();
    timerStartToFlushMs.reset();
    uiApplyToTimerStartMs.reset();
}

void LiveInputLatencyStats::reset()
{
    coalescedCount = 0;
    inputLatencyCount = 0;
    observedLatencyCount = 0;
    latchToDisplayMs.reset();
    observedToDisplayMs.reset();
    observedToLatchMs.reset();
    observedToRequestMs.reset();
    requestToDisplayMs.reset();
    requestToLatchMs.reset();
}

void SmbResponseLatencyStats::reset()
{
    bucketCounts.fill(0u);
    coalescedCount = 0;
    gameInputLatencyCount = 0;
    gameInputObservedLatencyCount = 0;
    latencyCount = 0;
    observedLatencyCount = 0;
    detectedToDisplayMs.reset();
    framesAfterLatch.reset();
    framesToGameInput.reset();
    gameInputToDetectedMs.reset();
    latchToDetectedMs.reset();
    latchToDisplayMs.reset();
    latchToGameInputMs.reset();
    observedToDetectedMs.reset();
    observedToDisplayMs.reset();
    observedToGameInputMs.reset();
    requestToDisplayMs.reset();
}

namespace {
constexpr uint32_t kLiveInputLatencyLogSampleCount = 20;
constexpr uint32_t kSmbResponseLatencyLogSampleCount = 10;

size_t smbResponseBucketIndex(
    NesSuperMarioBrosResponseKind kind,
    NesSuperMarioBrosResponseContext context,
    NesSuperMarioBrosResponseMilestone milestone)
{
    switch (kind) {
        case NesSuperMarioBrosResponseKind::Jump:
            switch (context) {
                case NesSuperMarioBrosResponseContext::StandingJump:
                    return 0u;
                case NesSuperMarioBrosResponseContext::RunningJump:
                    return 1u;
                case NesSuperMarioBrosResponseContext::GroundedStart:
                case NesSuperMarioBrosResponseContext::GroundedTurnaround:
                case NesSuperMarioBrosResponseContext::GroundedDuck:
                    break;
            }
            break;
        case NesSuperMarioBrosResponseKind::Duck:
            return 2u;
        case NesSuperMarioBrosResponseKind::MoveLeft:
            switch (context) {
                case NesSuperMarioBrosResponseContext::GroundedStart:
                    return 3u;
                case NesSuperMarioBrosResponseContext::GroundedTurnaround:
                    switch (milestone) {
                        case NesSuperMarioBrosResponseMilestone::Acknowledge:
                            return 4u;
                        case NesSuperMarioBrosResponseMilestone::Commit:
                            return 5u;
                        case NesSuperMarioBrosResponseMilestone::Motion:
                            return 6u;
                    }
                case NesSuperMarioBrosResponseContext::StandingJump:
                case NesSuperMarioBrosResponseContext::RunningJump:
                case NesSuperMarioBrosResponseContext::GroundedDuck:
                    break;
            }
            break;
        case NesSuperMarioBrosResponseKind::MoveRight:
            switch (context) {
                case NesSuperMarioBrosResponseContext::GroundedStart:
                    return 7u;
                case NesSuperMarioBrosResponseContext::GroundedTurnaround:
                    switch (milestone) {
                        case NesSuperMarioBrosResponseMilestone::Acknowledge:
                            return 8u;
                        case NesSuperMarioBrosResponseMilestone::Commit:
                            return 9u;
                        case NesSuperMarioBrosResponseMilestone::Motion:
                            return 10u;
                    }
                case NesSuperMarioBrosResponseContext::StandingJump:
                case NesSuperMarioBrosResponseContext::RunningJump:
                case NesSuperMarioBrosResponseContext::GroundedDuck:
                    break;
            }
            break;
    }

    return 0u;
}

const char* smbResponseBucketLabel(size_t bucketIndex)
{
    switch (bucketIndex) {
        case 0u:
            return "JumpStanding";
        case 1u:
            return "JumpRunning";
        case 2u:
            return "DuckGrounded";
        case 3u:
            return "MoveLeftStart";
        case 4u:
            return "MoveLeftTurnAck";
        case 5u:
            return "MoveLeftTurnCommit";
        case 6u:
            return "MoveLeftTurnMotion";
        case 7u:
            return "MoveRightStart";
        case 8u:
            return "MoveRightTurnAck";
        case 9u:
            return "MoveRightTurnCommit";
        case 10u:
            return "MoveRightTurnMotion";
    }

    return "Unknown";
}

std::optional<std::chrono::steady_clock::time_point> steadyClockTimePointFromNs(uint64_t ns)
{
    if (ns == 0) {
        return std::nullopt;
    }
    return std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ns));
}

uint8_t displayLoopPhaseEncode(DisplayLoopPhase phase)
{
    return static_cast<uint8_t>(phase);
}

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
    lastFrameTime = std::chrono::steady_clock::time_point{};
    pendingDisplayReceiveTime = std::chrono::steady_clock::time_point{};
    pendingDisplayUiApplyTime = std::chrono::steady_clock::time_point{};
    pendingDisplayTimerHandlerStartTime = std::chrono::steady_clock::time_point{};
    pendingDisplayServerSendTimestampNs = 0;
    pendingDisplayControllerObservedTimestampNs = 0;
    pendingDisplayControllerLatchTimestampNs = 0;
    pendingDisplayControllerRequestTimestampNs = 0;
    pendingDisplayControllerSequenceId = 0;
    pendingDisplayHandlePhase = displayLoopPhaseEncode(DisplayLoopPhase::Unknown);
    pendingDisplayMeasurement = false;
    pendingDisplaySmbResponseAppliedFrameId = 0;
    pendingDisplaySmbResponseDetectedTimestampNs = 0;
    pendingDisplaySmbResponseEventId = 0;
    pendingDisplaySmbResponseFrameId = 0;
    pendingDisplaySmbResponseGameInputCopiedFrameId = 0;
    pendingDisplaySmbResponseGameInputCopiedTimestampNs = 0;
    pendingDisplaySmbResponseObservedTimestampNs = 0;
    pendingDisplaySmbResponseLatchTimestampNs = 0;
    pendingDisplaySmbResponseRequestTimestampNs = 0;
    pendingDisplaySmbResponseSequenceId = 0;
    pendingDisplaySmbResponseMeasurement = false;
    displayFrameStagingStats.reset();
    displayLatencyMs.reset();
    displayLatencyCount = 0;
    displayCoalescedCount = 0;
    frameIntervalMs.reset();
    frameIntervalCount = 0;
    liveInputLatencyStats.reset();
    lastDisplayedControllerSequenceId = 0;
    lastDisplayedSmbResponseEventId = 0;
    queueDelayMs.reset();
    queueDelayCount = 0;
    smbResponseLatencyStats.reset();
    transportDelayMs.reset();
    transportDelayCount = 0;

    // Subscribe to render messages from the server (synchronous call).
    auto& wsService = sm.getWebSocketService();
    if (wsService.isConnected()) {
        Api::RenderFormatSet::Command cmd;
        cmd.format =
            debugDrawEnabled ? RenderFormat::EnumType::Debug : RenderFormat::EnumType::Basic;

        // Send binary command and wait for response.
        auto envelope = DirtSim::Network::make_command_envelope(wsService.allocateRequestId(), cmd);
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
        UiServices& uiServices = static_cast<UiServices&>(sm);
        EventSink& eventSink = static_cast<EventSink&>(sm);

        playground_ = std::make_unique<SimPlayground>(
            uiManager, &sm.getWebSocketService(), uiServices, eventSink, &sm.getFractalAnimator());
        DIRTSIM_ASSERT(playground_, "SimPlayground creation failed");

        IconRail* iconRail = uiManager->getIconRail();
        DIRTSIM_ASSERT(iconRail, "IconRail must exist");
        iconRail->setVisible(true);
        iconRail->setLayout(RailLayout::SingleColumn);
        iconRail->setMinimizedAffordanceStyle(IconRail::minimizedAffordanceLeftTopSquare());
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
        Api::RenderFormatSet::Command cmd;
        cmd.format =
            debugDrawEnabled ? RenderFormat::EnumType::Debug : RenderFormat::EnumType::Basic;

        // Send binary command and wait for response.
        auto envelope =
            DirtSim::Network::make_command_envelope(wsServiceRef.allocateRequestId(), cmd);
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

            Api::CellSet::Command cmd{ cell->x, cell->y, material, 1.0 };
            auto envelope = DirtSim::Network::make_command_envelope(
                sm.getWebSocketService().allocateRequestId(), cmd);
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

            Api::CellSet::Command cmd{ cell->x, cell->y, material, 1.0 };
            auto envelope = DirtSim::Network::make_command_envelope(
                sm.getWebSocketService().allocateRequestId(), cmd);
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
    const uint8_t displayHandlePhase = displayLoopPhaseEncode(sm.getDisplayLoopPhase());
    const bool hasLastFrameTime = lastFrameTime.time_since_epoch().count() > 0;
    const auto elapsed = hasLastFrameTime
        ? std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime)
        : std::chrono::milliseconds(0);

    if (const auto serverSendTime = steadyClockTimePointFromNs(evt.serverSendTimestampNs);
        serverSendTime.has_value() && evt.timestamp.time_since_epoch().count() > 0) {
        const double transportDelayMs =
            std::chrono::duration<double, std::milli>(evt.timestamp - serverSendTime.value())
                .count();
        this->transportDelayMs.record(transportDelayMs);
        transportDelayCount++;
        if (transportDelayCount >= 1000) {
            LOG_INFO(
                State,
                "  Transport delay: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f}, {} frames)",
                this->transportDelayMs.average(transportDelayCount),
                this->transportDelayMs.min,
                this->transportDelayMs.max,
                this->transportDelayMs.stddev(transportDelayCount),
                transportDelayCount);
            this->transportDelayMs.reset();
            transportDelayCount = 0;
        }
    }

    // Measure queue delay: time from WebSocket receive to processEvents pickup.
    if (evt.timestamp.time_since_epoch().count() > 0) {
        const double queueDelayMs =
            std::chrono::duration<double, std::milli>(now - evt.timestamp).count();
        this->queueDelayMs.record(queueDelayMs);
        queueDelayCount++;
        if (queueDelayCount >= 1000) {
            LOG_INFO(
                State,
                "  Queue delay: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f}, {} frames)",
                this->queueDelayMs.average(queueDelayCount),
                this->queueDelayMs.min,
                this->queueDelayMs.max,
                this->queueDelayMs.stddev(queueDelayCount),
                queueDelayCount);
            this->queueDelayMs.reset();
            queueDelayCount = 0;
        }
    }

    if (hasLastFrameTime && elapsed.count() > 0) {
        measuredUiFps = 1000.0 / elapsed.count();

        // Exponentially weighted moving average (90% old, 10% new) for smooth display.
        if (smoothedUiFps == 0.0) {
            smoothedUiFps = measuredUiFps; // Initialize.
        }
        else {
            smoothedUiFps = 0.9 * smoothedUiFps + 0.1 * measuredUiFps;
        }

        // Track frame interval jitter.
        const double intervalMs =
            std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
        frameIntervalMs.record(intervalMs);
        frameIntervalCount++;

        LOG_DEBUG(State, "UI FPS: {:.1f} (smoothed: {:.1f})", measuredUiFps, smoothedUiFps);
    }

    lastFrameTime = now;

    updateCount++;
    // Log performance stats every once in a while.
    if (updateCount % 1000 == 0) {
        auto& timers = sm.getTimers();

        // Get current stats.
        const double parseTotal = timers.getAccumulatedTime("parse_message");
        const uint32_t parseCount = timers.getCallCount("parse_message");
        const double renderTotal = timers.getAccumulatedTime("render_world");
        const uint32_t renderCount = timers.getCallCount("render_world");

        // Calculate interval stats.
        const double intervalParseTime = parseTotal - lastParseTotal;
        const uint32_t intervalParseCount = parseCount - lastParseCount;
        const double intervalRenderTime = renderTotal - lastRenderTotal;
        const uint32_t intervalRenderCount = renderCount - lastRenderCount;

        // Get additional timing info.
        const double copyTotal = timers.getAccumulatedTime("copy_worlddata");
        const uint32_t copyCount = timers.getCallCount("copy_worlddata");
        const double updateTotal = timers.getAccumulatedTime("update_controls");
        const uint32_t updateCount_ = timers.getCallCount("update_controls");

        const double intervalCopyTime = copyTotal - lastCopyTotal;
        const uint32_t intervalCopyCount = copyCount - lastCopyCount;
        const double intervalUpdateTime = updateTotal - lastUpdateTotal;
        const uint32_t intervalUpdateCount = updateCount_ - lastUpdateCount;

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

        // Frame interval jitter.
        if (frameIntervalCount > 0) {
            LOG_INFO(
                State,
                "  Frame interval: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f}, {} frames)",
                frameIntervalMs.average(frameIntervalCount),
                frameIntervalMs.min,
                frameIntervalMs.max,
                frameIntervalMs.stddev(frameIntervalCount),
                frameIntervalCount);
        }
        frameIntervalMs.reset();
        frameIntervalCount = 0;

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
    if (evt.scenarioVideoFrame.has_value()) {
        playground_->presentVideoFrame(evt.scenarioVideoFrame.value());
        sm.eventProcessor.requestYield();
    }
    else {
        playground_->render(*worldData, debugDrawEnabled);
    }
    sm.getTimers().stopTimer("render_world");

    // Render neural grid (tree vision).
    sm.getTimers().startTimer("render_neural_grid");
    playground_->renderNeuralGrid(*worldData);
    sm.getTimers().stopTimer("render_neural_grid");
    const auto uiApplyTime = std::chrono::steady_clock::now();

    uint64_t controllerObservedTimestampNs = 0;
    uint64_t controllerLatchTimestampNs = 0;
    uint64_t controllerRequestTimestampNs = 0;
    uint64_t controllerSequenceId = 0;
    if (evt.nesControllerTelemetry.has_value()) {
        const NesControllerTelemetry& controllerTelemetry = evt.nesControllerTelemetry.value();
        if (controllerTelemetry.controllerSource == NesGameAdapterControllerSource::LiveInput
            && controllerTelemetry.controllerObservedTimestampNs.has_value()
            && controllerTelemetry.controllerLatchTimestampNs.has_value()
            && controllerTelemetry.controllerRequestTimestampNs.has_value()
            && controllerTelemetry.controllerSequenceId.has_value()) {
            controllerObservedTimestampNs =
                controllerTelemetry.controllerObservedTimestampNs.value();
            controllerLatchTimestampNs = controllerTelemetry.controllerLatchTimestampNs.value();
            controllerRequestTimestampNs = controllerTelemetry.controllerRequestTimestampNs.value();
            controllerSequenceId = controllerTelemetry.controllerSequenceId.value();
        }
    }
    if (evt.nesSmbResponseTelemetry.has_value()) {
        const auto& responseTelemetry = evt.nesSmbResponseTelemetry.value();
        if (pendingDisplaySmbResponseMeasurement) {
            smbResponseLatencyStats.coalescedCount++;
        }
        pendingDisplaySmbResponseAppliedFrameId = responseTelemetry.controllerAppliedFrameId;
        pendingDisplaySmbResponseDetectedTimestampNs =
            responseTelemetry.responseDetectedTimestampNs;
        pendingDisplaySmbResponseEventId = responseTelemetry.responseEventId;
        pendingDisplaySmbResponseFrameId = responseTelemetry.responseFrameId;
        pendingDisplaySmbResponseGameInputCopiedFrameId = responseTelemetry.gameInputCopiedFrameId;
        pendingDisplaySmbResponseGameInputCopiedTimestampNs =
            responseTelemetry.gameInputCopiedTimestampNs;
        pendingDisplaySmbResponseObservedTimestampNs =
            responseTelemetry.controllerObservedTimestampNs;
        pendingDisplaySmbResponseLatchTimestampNs = responseTelemetry.controllerLatchTimestampNs;
        pendingDisplaySmbResponseRequestTimestampNs =
            responseTelemetry.controllerRequestTimestampNs;
        pendingDisplaySmbResponseSequenceId = responseTelemetry.controllerSequenceId;
        pendingDisplaySmbResponseMeasurement = true;
        smbResponseLatencyStats.bucketCounts[smbResponseBucketIndex(
            responseTelemetry.kind, responseTelemetry.context, responseTelemetry.milestone)]++;
    }

    if (evt.serverSendTimestampNs > 0) {
        if (pendingDisplayMeasurement) {
            displayCoalescedCount++;
        }
        if (pendingDisplayControllerSequenceId > 0 && controllerSequenceId > 0
            && pendingDisplayControllerSequenceId != controllerSequenceId) {
            liveInputLatencyStats.coalescedCount++;
        }
        pendingDisplayReceiveTime = evt.timestamp;
        pendingDisplayUiApplyTime = uiApplyTime;
        pendingDisplayTimerHandlerStartTime = std::chrono::steady_clock::time_point{};
        pendingDisplayServerSendTimestampNs = evt.serverSendTimestampNs;
        pendingDisplayControllerObservedTimestampNs = controllerObservedTimestampNs;
        pendingDisplayControllerLatchTimestampNs = controllerLatchTimestampNs;
        pendingDisplayControllerRequestTimestampNs = controllerRequestTimestampNs;
        pendingDisplayControllerSequenceId = controllerSequenceId;
        pendingDisplayHandlePhase = displayHandlePhase;
        pendingDisplayMeasurement = true;
    }

    LOG_DEBUG(
        State,
        "Rendered world ({}x{}, step {})",
        worldData->width,
        worldData->height,
        worldData->timestep);

    return std::move(*this);
}

void SimRunning::onDisplayTimerHandlerStart(std::chrono::steady_clock::time_point startTime)
{
    if (!pendingDisplayMeasurement) {
        return;
    }
    if (pendingDisplayTimerHandlerStartTime.time_since_epoch().count() > 0) {
        return;
    }
    pendingDisplayTimerHandlerStartTime = startTime;
}

void SimRunning::onDisplayFlush(std::chrono::steady_clock::time_point flushTime)
{
    const bool hasPendingDisplayMeasurement = pendingDisplayMeasurement;
    if (!hasPendingDisplayMeasurement && !pendingDisplaySmbResponseMeasurement) {
        return;
    }

    if (hasPendingDisplayMeasurement) {
        const auto serverSendTime = steadyClockTimePointFromNs(pendingDisplayServerSendTimestampNs);
        if (!serverSendTime.has_value()) {
            pendingDisplayMeasurement = false;
            pendingDisplayReceiveTime = std::chrono::steady_clock::time_point{};
            pendingDisplayUiApplyTime = std::chrono::steady_clock::time_point{};
            pendingDisplayTimerHandlerStartTime = std::chrono::steady_clock::time_point{};
            pendingDisplayServerSendTimestampNs = 0;
            pendingDisplayHandlePhase = displayLoopPhaseEncode(DisplayLoopPhase::Unknown);
        }
        else {
            const double displayLatencyMs =
                std::chrono::duration<double, std::milli>(flushTime - serverSendTime.value())
                    .count();
            this->displayLatencyMs.record(displayLatencyMs);
            displayLatencyCount++;

            if (pendingDisplayReceiveTime.time_since_epoch().count() > 0
                && pendingDisplayUiApplyTime.time_since_epoch().count() > 0
                && pendingDisplayTimerHandlerStartTime.time_since_epoch().count() > 0) {
                const double receiveToUiApplyMs =
                    std::chrono::duration<double, std::milli>(
                        pendingDisplayUiApplyTime - pendingDisplayReceiveTime)
                        .count();
                const double uiApplyToTimerStartMs =
                    std::chrono::duration<double, std::milli>(
                        pendingDisplayTimerHandlerStartTime - pendingDisplayUiApplyTime)
                        .count();
                const double timerStartToFlushMs =
                    std::chrono::duration<double, std::milli>(
                        flushTime - pendingDisplayTimerHandlerStartTime)
                        .count();
                if (receiveToUiApplyMs >= 0.0 && uiApplyToTimerStartMs >= 0.0
                    && timerStartToFlushMs >= 0.0) {
                    displayFrameStagingStats.receiveToUiApplyMs.record(receiveToUiApplyMs);
                    displayFrameStagingStats.uiApplyToTimerStartMs.record(uiApplyToTimerStartMs);
                    displayFrameStagingStats.timerStartToFlushMs.record(timerStartToFlushMs);
                    displayFrameStagingStats.sampleCount++;

                    switch (static_cast<DisplayLoopPhase>(pendingDisplayHandlePhase)) {
                        case DisplayLoopPhase::ProcessEventsBeforeRender:
                            displayFrameStagingStats.handledBeforeRenderCount++;
                            break;
                        case DisplayLoopPhase::ProcessEventsAfterRender:
                            displayFrameStagingStats.handledAfterRenderCount++;
                            break;
                        case DisplayLoopPhase::TimerHandler:
                        case DisplayLoopPhase::WaitingForEvents:
                        case DisplayLoopPhase::Unknown:
                            displayFrameStagingStats.handledOtherPhaseCount++;
                            break;
                    }
                }
            }

            if (pendingDisplayControllerSequenceId > 0) {
                if (pendingDisplayControllerSequenceId != lastDisplayedControllerSequenceId) {
                    const auto observedTime =
                        steadyClockTimePointFromNs(pendingDisplayControllerObservedTimestampNs);
                    const auto requestTime =
                        steadyClockTimePointFromNs(pendingDisplayControllerRequestTimestampNs);
                    const auto latchTime =
                        steadyClockTimePointFromNs(pendingDisplayControllerLatchTimestampNs);
                    if (observedTime.has_value() && requestTime.has_value()
                        && latchTime.has_value()) {
                        const double observedToRequestMs =
                            std::chrono::duration<double, std::milli>(
                                requestTime.value() - observedTime.value())
                                .count();
                        const double observedToLatchMs =
                            std::chrono::duration<double, std::milli>(
                                latchTime.value() - observedTime.value())
                                .count();
                        const double observedToDisplayMs =
                            std::chrono::duration<double, std::milli>(
                                flushTime - observedTime.value())
                                .count();
                        if (observedToRequestMs >= 0.0 && observedToLatchMs >= 0.0
                            && observedToDisplayMs >= 0.0) {
                            liveInputLatencyStats.observedToRequestMs.record(observedToRequestMs);
                            liveInputLatencyStats.observedToLatchMs.record(observedToLatchMs);
                            liveInputLatencyStats.observedToDisplayMs.record(observedToDisplayMs);
                            liveInputLatencyStats.observedLatencyCount++;
                        }
                    }
                    if (requestTime.has_value() && latchTime.has_value()) {
                        const double requestToLatchMs = std::chrono::duration<double, std::milli>(
                                                            latchTime.value() - requestTime.value())
                                                            .count();
                        const double requestToDisplayMs = std::chrono::duration<double, std::milli>(
                                                              flushTime - requestTime.value())
                                                              .count();
                        const double latchToDisplayMs =
                            std::chrono::duration<double, std::milli>(flushTime - latchTime.value())
                                .count();
                        if (requestToLatchMs >= 0.0 && requestToDisplayMs >= 0.0
                            && latchToDisplayMs >= 0.0) {
                            liveInputLatencyStats.requestToLatchMs.record(requestToLatchMs);
                            liveInputLatencyStats.requestToDisplayMs.record(requestToDisplayMs);
                            liveInputLatencyStats.latchToDisplayMs.record(latchToDisplayMs);
                            liveInputLatencyStats.inputLatencyCount++;
                        }
                    }
                }
                lastDisplayedControllerSequenceId = pendingDisplayControllerSequenceId;
            }

            pendingDisplayMeasurement = false;
            pendingDisplayReceiveTime = std::chrono::steady_clock::time_point{};
            pendingDisplayUiApplyTime = std::chrono::steady_clock::time_point{};
            pendingDisplayTimerHandlerStartTime = std::chrono::steady_clock::time_point{};
            pendingDisplayServerSendTimestampNs = 0;
            pendingDisplayControllerObservedTimestampNs = 0;
            pendingDisplayControllerLatchTimestampNs = 0;
            pendingDisplayControllerRequestTimestampNs = 0;
            pendingDisplayControllerSequenceId = 0;
            pendingDisplayHandlePhase = displayLoopPhaseEncode(DisplayLoopPhase::Unknown);
        }
    }

    if (pendingDisplaySmbResponseMeasurement) {
        if (pendingDisplaySmbResponseEventId != lastDisplayedSmbResponseEventId) {
            const auto observedTime =
                steadyClockTimePointFromNs(pendingDisplaySmbResponseObservedTimestampNs);
            const auto requestTime =
                steadyClockTimePointFromNs(pendingDisplaySmbResponseRequestTimestampNs);
            const auto latchTime =
                steadyClockTimePointFromNs(pendingDisplaySmbResponseLatchTimestampNs);
            const auto gameInputCopiedTime =
                steadyClockTimePointFromNs(pendingDisplaySmbResponseGameInputCopiedTimestampNs);
            const auto responseDetectedTime =
                steadyClockTimePointFromNs(pendingDisplaySmbResponseDetectedTimestampNs);
            if (requestTime.has_value() && latchTime.has_value()
                && responseDetectedTime.has_value()) {
                const bool hasFramesAfterLatch =
                    pendingDisplaySmbResponseFrameId >= pendingDisplaySmbResponseAppliedFrameId;
                const double framesAfterLatch = hasFramesAfterLatch
                    ? static_cast<double>(
                          pendingDisplaySmbResponseFrameId
                          - pendingDisplaySmbResponseAppliedFrameId)
                    : 0.0;
                const double latchToDetectedMs =
                    std::chrono::duration<double, std::milli>(
                        responseDetectedTime.value() - latchTime.value())
                        .count();
                const double requestToDisplayMs =
                    std::chrono::duration<double, std::milli>(flushTime - requestTime.value())
                        .count();
                const double latchToDisplayMs =
                    std::chrono::duration<double, std::milli>(flushTime - latchTime.value())
                        .count();
                const double detectedToDisplayMs = std::chrono::duration<double, std::milli>(
                                                       flushTime - responseDetectedTime.value())
                                                       .count();
                if (hasFramesAfterLatch && latchToDetectedMs >= 0.0 && requestToDisplayMs >= 0.0
                    && latchToDisplayMs >= 0.0 && detectedToDisplayMs >= 0.0) {
                    smbResponseLatencyStats.framesAfterLatch.record(framesAfterLatch);
                    smbResponseLatencyStats.latchToDetectedMs.record(latchToDetectedMs);
                    smbResponseLatencyStats.requestToDisplayMs.record(requestToDisplayMs);
                    smbResponseLatencyStats.latchToDisplayMs.record(latchToDisplayMs);
                    smbResponseLatencyStats.detectedToDisplayMs.record(detectedToDisplayMs);
                    smbResponseLatencyStats.latencyCount++;
                }

                if (gameInputCopiedTime.has_value()
                    && pendingDisplaySmbResponseGameInputCopiedFrameId
                        >= pendingDisplaySmbResponseAppliedFrameId) {
                    const double framesToGameInput = static_cast<double>(
                        pendingDisplaySmbResponseGameInputCopiedFrameId
                        - pendingDisplaySmbResponseAppliedFrameId);
                    const double latchToGameInputMs =
                        std::chrono::duration<double, std::milli>(
                            gameInputCopiedTime.value() - latchTime.value())
                            .count();
                    const double gameInputToDetectedMs =
                        std::chrono::duration<double, std::milli>(
                            responseDetectedTime.value() - gameInputCopiedTime.value())
                            .count();
                    if (framesToGameInput >= 0.0 && latchToGameInputMs >= 0.0
                        && gameInputToDetectedMs >= 0.0) {
                        smbResponseLatencyStats.framesToGameInput.record(framesToGameInput);
                        smbResponseLatencyStats.latchToGameInputMs.record(latchToGameInputMs);
                        smbResponseLatencyStats.gameInputToDetectedMs.record(gameInputToDetectedMs);
                        smbResponseLatencyStats.gameInputLatencyCount++;
                    }
                }

                if (observedTime.has_value()) {
                    const double observedToDetectedMs =
                        std::chrono::duration<double, std::milli>(
                            responseDetectedTime.value() - observedTime.value())
                            .count();
                    const double observedToDisplayMs =
                        std::chrono::duration<double, std::milli>(flushTime - observedTime.value())
                            .count();
                    if (observedToDetectedMs >= 0.0 && observedToDisplayMs >= 0.0) {
                        smbResponseLatencyStats.observedToDetectedMs.record(observedToDetectedMs);
                        smbResponseLatencyStats.observedToDisplayMs.record(observedToDisplayMs);
                        smbResponseLatencyStats.observedLatencyCount++;
                    }

                    if (gameInputCopiedTime.has_value()) {
                        const double observedToGameInputMs =
                            std::chrono::duration<double, std::milli>(
                                gameInputCopiedTime.value() - observedTime.value())
                                .count();
                        if (observedToGameInputMs >= 0.0) {
                            smbResponseLatencyStats.observedToGameInputMs.record(
                                observedToGameInputMs);
                            smbResponseLatencyStats.gameInputObservedLatencyCount++;
                        }
                    }
                }
            }
        }
        lastDisplayedSmbResponseEventId = pendingDisplaySmbResponseEventId;
        pendingDisplaySmbResponseAppliedFrameId = 0;
        pendingDisplaySmbResponseDetectedTimestampNs = 0;
        pendingDisplaySmbResponseEventId = 0;
        pendingDisplaySmbResponseFrameId = 0;
        pendingDisplaySmbResponseGameInputCopiedFrameId = 0;
        pendingDisplaySmbResponseGameInputCopiedTimestampNs = 0;
        pendingDisplaySmbResponseObservedTimestampNs = 0;
        pendingDisplaySmbResponseLatchTimestampNs = 0;
        pendingDisplaySmbResponseRequestTimestampNs = 0;
        pendingDisplaySmbResponseSequenceId = 0;
        pendingDisplaySmbResponseMeasurement = false;
    }

    if (liveInputLatencyStats.inputLatencyCount >= kLiveInputLatencyLogSampleCount) {
        LOG_INFO(
            State,
            "  Live input latency ({} displayed changes, {} coalesced):",
            liveInputLatencyStats.inputLatencyCount,
            liveInputLatencyStats.coalescedCount);
        if (liveInputLatencyStats.observedLatencyCount > 0) {
            LOG_INFO(
                State,
                "    Observed -> request: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
                liveInputLatencyStats.observedToRequestMs.average(
                    liveInputLatencyStats.observedLatencyCount),
                liveInputLatencyStats.observedToRequestMs.min,
                liveInputLatencyStats.observedToRequestMs.max,
                liveInputLatencyStats.observedToRequestMs.stddev(
                    liveInputLatencyStats.observedLatencyCount));
            LOG_INFO(
                State,
                "    Observed -> latch: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
                liveInputLatencyStats.observedToLatchMs.average(
                    liveInputLatencyStats.observedLatencyCount),
                liveInputLatencyStats.observedToLatchMs.min,
                liveInputLatencyStats.observedToLatchMs.max,
                liveInputLatencyStats.observedToLatchMs.stddev(
                    liveInputLatencyStats.observedLatencyCount));
            LOG_INFO(
                State,
                "    Observed -> display: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
                liveInputLatencyStats.observedToDisplayMs.average(
                    liveInputLatencyStats.observedLatencyCount),
                liveInputLatencyStats.observedToDisplayMs.min,
                liveInputLatencyStats.observedToDisplayMs.max,
                liveInputLatencyStats.observedToDisplayMs.stddev(
                    liveInputLatencyStats.observedLatencyCount));
        }
        LOG_INFO(
            State,
            "    Request -> latch: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            liveInputLatencyStats.requestToLatchMs.average(liveInputLatencyStats.inputLatencyCount),
            liveInputLatencyStats.requestToLatchMs.min,
            liveInputLatencyStats.requestToLatchMs.max,
            liveInputLatencyStats.requestToLatchMs.stddev(liveInputLatencyStats.inputLatencyCount));
        LOG_INFO(
            State,
            "    Request -> display: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            liveInputLatencyStats.requestToDisplayMs.average(
                liveInputLatencyStats.inputLatencyCount),
            liveInputLatencyStats.requestToDisplayMs.min,
            liveInputLatencyStats.requestToDisplayMs.max,
            liveInputLatencyStats.requestToDisplayMs.stddev(
                liveInputLatencyStats.inputLatencyCount));
        LOG_INFO(
            State,
            "    Latch -> display: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            liveInputLatencyStats.latchToDisplayMs.average(liveInputLatencyStats.inputLatencyCount),
            liveInputLatencyStats.latchToDisplayMs.min,
            liveInputLatencyStats.latchToDisplayMs.max,
            liveInputLatencyStats.latchToDisplayMs.stddev(liveInputLatencyStats.inputLatencyCount));
        liveInputLatencyStats.reset();
    }

    if (smbResponseLatencyStats.latencyCount >= kSmbResponseLatencyLogSampleCount) {
        const bool hasGameInputLatency = smbResponseLatencyStats.gameInputLatencyCount > 0;
        const bool hasObservedGameInputLatency =
            smbResponseLatencyStats.gameInputObservedLatencyCount > 0;
        LOG_INFO(
            State,
            "  SMB response latency ({} displayed responses, {} coalesced):",
            smbResponseLatencyStats.latencyCount,
            smbResponseLatencyStats.coalescedCount);
        std::string breakdown;
        for (size_t bucketIndex = 0; bucketIndex < smbResponseLatencyStats.bucketCounts.size();
             ++bucketIndex) {
            const uint32_t bucketCount = smbResponseLatencyStats.bucketCounts[bucketIndex];
            if (bucketCount == 0) {
                continue;
            }
            if (!breakdown.empty()) {
                breakdown += " ";
            }
            breakdown += std::string(smbResponseBucketLabel(bucketIndex)) + "="
                + std::to_string(bucketCount);
        }
        if (!breakdown.empty()) {
            LOG_INFO(State, "    Breakdown: {}", breakdown);
        }
        if (hasObservedGameInputLatency) {
            LOG_INFO(
                State,
                "    Observed -> SMB input mask: {:.2f}ms avg (min={:.2f} max={:.2f}"
                " stddev={:.2f})",
                smbResponseLatencyStats.observedToGameInputMs.average(
                    smbResponseLatencyStats.gameInputObservedLatencyCount),
                smbResponseLatencyStats.observedToGameInputMs.min,
                smbResponseLatencyStats.observedToGameInputMs.max,
                smbResponseLatencyStats.observedToGameInputMs.stddev(
                    smbResponseLatencyStats.gameInputObservedLatencyCount));
        }
        if (smbResponseLatencyStats.observedLatencyCount > 0) {
            LOG_INFO(
                State,
                "    Observed -> response detect: {:.2f}ms avg (min={:.2f} max={:.2f}"
                " stddev={:.2f})",
                smbResponseLatencyStats.observedToDetectedMs.average(
                    smbResponseLatencyStats.observedLatencyCount),
                smbResponseLatencyStats.observedToDetectedMs.min,
                smbResponseLatencyStats.observedToDetectedMs.max,
                smbResponseLatencyStats.observedToDetectedMs.stddev(
                    smbResponseLatencyStats.observedLatencyCount));
            LOG_INFO(
                State,
                "    Observed -> response display: {:.2f}ms avg (min={:.2f} max={:.2f}"
                " stddev={:.2f})",
                smbResponseLatencyStats.observedToDisplayMs.average(
                    smbResponseLatencyStats.observedLatencyCount),
                smbResponseLatencyStats.observedToDisplayMs.min,
                smbResponseLatencyStats.observedToDisplayMs.max,
                smbResponseLatencyStats.observedToDisplayMs.stddev(
                    smbResponseLatencyStats.observedLatencyCount));
        }
        if (hasGameInputLatency) {
            LOG_INFO(
                State,
                "    Frames to SMB input mask: {:.2f} avg (min={:.2f} max={:.2f} stddev={:.2f})",
                smbResponseLatencyStats.framesToGameInput.average(
                    smbResponseLatencyStats.gameInputLatencyCount),
                smbResponseLatencyStats.framesToGameInput.min,
                smbResponseLatencyStats.framesToGameInput.max,
                smbResponseLatencyStats.framesToGameInput.stddev(
                    smbResponseLatencyStats.gameInputLatencyCount));
            LOG_INFO(
                State,
                "    Latch -> SMB input mask: {:.2f}ms avg (min={:.2f} max={:.2f}"
                " stddev={:.2f})",
                smbResponseLatencyStats.latchToGameInputMs.average(
                    smbResponseLatencyStats.gameInputLatencyCount),
                smbResponseLatencyStats.latchToGameInputMs.min,
                smbResponseLatencyStats.latchToGameInputMs.max,
                smbResponseLatencyStats.latchToGameInputMs.stddev(
                    smbResponseLatencyStats.gameInputLatencyCount));
            LOG_INFO(
                State,
                "    SMB input mask -> response detect: {:.2f}ms avg (min={:.2f} max={:.2f}"
                " stddev={:.2f})",
                smbResponseLatencyStats.gameInputToDetectedMs.average(
                    smbResponseLatencyStats.gameInputLatencyCount),
                smbResponseLatencyStats.gameInputToDetectedMs.min,
                smbResponseLatencyStats.gameInputToDetectedMs.max,
                smbResponseLatencyStats.gameInputToDetectedMs.stddev(
                    smbResponseLatencyStats.gameInputLatencyCount));
        }
        LOG_INFO(
            State,
            "    Frames after latch: {:.2f} avg (min={:.2f} max={:.2f} stddev={:.2f})",
            smbResponseLatencyStats.framesAfterLatch.average(smbResponseLatencyStats.latencyCount),
            smbResponseLatencyStats.framesAfterLatch.min,
            smbResponseLatencyStats.framesAfterLatch.max,
            smbResponseLatencyStats.framesAfterLatch.stddev(smbResponseLatencyStats.latencyCount));
        LOG_INFO(
            State,
            "    Latch -> response detect: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            smbResponseLatencyStats.latchToDetectedMs.average(smbResponseLatencyStats.latencyCount),
            smbResponseLatencyStats.latchToDetectedMs.min,
            smbResponseLatencyStats.latchToDetectedMs.max,
            smbResponseLatencyStats.latchToDetectedMs.stddev(smbResponseLatencyStats.latencyCount));
        LOG_INFO(
            State,
            "    Request -> response display: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            smbResponseLatencyStats.requestToDisplayMs.average(
                smbResponseLatencyStats.latencyCount),
            smbResponseLatencyStats.requestToDisplayMs.min,
            smbResponseLatencyStats.requestToDisplayMs.max,
            smbResponseLatencyStats.requestToDisplayMs.stddev(
                smbResponseLatencyStats.latencyCount));
        LOG_INFO(
            State,
            "    Latch -> response display: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            smbResponseLatencyStats.latchToDisplayMs.average(smbResponseLatencyStats.latencyCount),
            smbResponseLatencyStats.latchToDisplayMs.min,
            smbResponseLatencyStats.latchToDisplayMs.max,
            smbResponseLatencyStats.latchToDisplayMs.stddev(smbResponseLatencyStats.latencyCount));
        LOG_INFO(
            State,
            "    Response detect -> display: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            smbResponseLatencyStats.detectedToDisplayMs.average(
                smbResponseLatencyStats.latencyCount),
            smbResponseLatencyStats.detectedToDisplayMs.min,
            smbResponseLatencyStats.detectedToDisplayMs.max,
            smbResponseLatencyStats.detectedToDisplayMs.stddev(
                smbResponseLatencyStats.latencyCount));
        smbResponseLatencyStats.reset();
    }

    if (displayFrameStagingStats.sampleCount >= 1000) {
        LOG_INFO(
            State,
            "  UI frame staging ({} displayed frames):",
            displayFrameStagingStats.sampleCount);
        LOG_INFO(
            State,
            "    Handled phase: BeforeRender={} AfterRender={} Other={}",
            displayFrameStagingStats.handledBeforeRenderCount,
            displayFrameStagingStats.handledAfterRenderCount,
            displayFrameStagingStats.handledOtherPhaseCount);
        LOG_INFO(
            State,
            "    Receive -> UI apply: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            displayFrameStagingStats.receiveToUiApplyMs.average(
                displayFrameStagingStats.sampleCount),
            displayFrameStagingStats.receiveToUiApplyMs.min,
            displayFrameStagingStats.receiveToUiApplyMs.max,
            displayFrameStagingStats.receiveToUiApplyMs.stddev(
                displayFrameStagingStats.sampleCount));
        LOG_INFO(
            State,
            "    UI apply -> timer start: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            displayFrameStagingStats.uiApplyToTimerStartMs.average(
                displayFrameStagingStats.sampleCount),
            displayFrameStagingStats.uiApplyToTimerStartMs.min,
            displayFrameStagingStats.uiApplyToTimerStartMs.max,
            displayFrameStagingStats.uiApplyToTimerStartMs.stddev(
                displayFrameStagingStats.sampleCount));
        LOG_INFO(
            State,
            "    Timer start -> flush: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            displayFrameStagingStats.timerStartToFlushMs.average(
                displayFrameStagingStats.sampleCount),
            displayFrameStagingStats.timerStartToFlushMs.min,
            displayFrameStagingStats.timerStartToFlushMs.max,
            displayFrameStagingStats.timerStartToFlushMs.stddev(
                displayFrameStagingStats.sampleCount));
        displayFrameStagingStats.reset();
    }

    if (displayLatencyCount < 1000) {
        return;
    }

    LOG_INFO(
        State,
        "  Display latency: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f}, {} displayed,"
        " {} coalesced)",
        displayLatencyMs.average(displayLatencyCount),
        displayLatencyMs.min,
        displayLatencyMs.max,
        displayLatencyMs.stddev(displayLatencyCount),
        displayLatencyCount,
        displayCoalescedCount);
    displayLatencyMs.reset();
    displayLatencyCount = 0;
    displayCoalescedCount = 0;
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
