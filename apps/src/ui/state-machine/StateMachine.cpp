#include "StateMachine.h"
#include "api/GenomeBrowserOpen.h"
#include "api/GenomeDetailLoad.h"
#include "api/GenomeDetailOpen.h"
#include "api/IconRailExpand.h"
#include "api/IconRailShowIcons.h"
#include "api/IconSelect.h"
#include "api/StopButtonPress.h"
#include "api/StreamStart.h"
#include "api/SynthKeyEvent.h"
#include "api/TrainingActiveScenarioControlsShow.h"
#include "api/TrainingConfigShowEvolution.h"
#include "api/WebRtcAnswer.h"
#include "api/WebRtcCandidate.h"
#include "api/WebSocketAccessSet.h"
#include "audio/api/MasterVolumeSet.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/StateLifecycle.h"
#include "core/encoding/H264Encoder.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/JsonProtocol.h"
#include "core/network/WebSocketService.h"
#include "network/CommandDeserializerJson.h"
#include "server/api/EventSubscribe.h"
#include "states/State.h"
#include "ui/DisplayCapture.h"
#include "ui/RemoteInputDevice.h"
#include "ui/UiComponentManager.h"
#include "ui/rendering/FractalAnimator.h"
#include "ui/rendering/WebRtcStreamer.h"
#include <chrono>
#include <rtc/rtc.hpp>
#include <type_traits>

namespace DirtSim {
namespace Ui {

StateMachine::StateMachine(TestMode, UserSettingsManager& userSettingsManager)
    : display(nullptr), userSettingsManager_(userSettingsManager)
{
    // Minimal initialization for unit testing.
    // No WebSocket, no UI components, no WebRTC - just the state machine core.
    fractalAnimator_ = std::make_unique<FractalAnimator>();
    LOG_INFO(State, "StateMachine created in test mode");
}

StateMachine::StateMachine(
    _lv_display_t* disp, UserSettingsManager& userSettingsManager, uint16_t wsPort)
    : display(disp), userSettingsManager_(userSettingsManager)
{
    LOG_INFO(State, "Initialized in state: {}", getCurrentStateName());
    wsPort_ = wsPort;
    fractalAnimator_ = std::make_unique<FractalAnimator>();

    // Create unified WebSocketService for both client (to server) and server (for CLI) roles.
    wsService_ = std::make_unique<Network::WebSocketService>();
    setupWebSocketService();

    // Start listening for CLI/browser commands on the specified port.
    auto listenResult = wsService_->listen(wsPort, "127.0.0.1");
    if (listenResult.isError()) {
        LOG_ERROR(Network, "Failed to listen on port {}: {}", wsPort, listenResult.errorValue());
    }
    else {
        LOG_INFO(Network, "WebSocketService listening on port {}", wsPort);
    }

    // Create UI manager for LVGL screen/container management.
    uiManager_ = std::make_unique<UiComponentManager>(disp);
    uiManager_->setEventSink(this); // StateMachine implements EventSink.
    uiManager_->setFractalAnimator(fractalAnimator_.get());
    LOG_INFO(State, "UiComponentManager created");

    // Create remote input device for WebSocket mouse events.
    remoteInputDevice_ = std::make_unique<RemoteInputDevice>(disp);
    LOG_INFO(State, "RemoteInputDevice created");

    // Create WebRTC streamer for video streaming.
    // ICE candidates are sent via wsService_->sendToClient() in the StreamStart handler.
    webRtcStreamer_ = std::make_unique<WebRtcStreamer>();
    webRtcStreamer_->setDisplay(disp);
    LOG_INFO(State, "WebRtcStreamer created");
}

FractalAnimator& StateMachine::getFractalAnimator()
{
    DIRTSIM_ASSERT(fractalAnimator_, "FractalAnimator not initialized");
    return *fractalAnimator_.get();
}

void StateMachine::setupWebSocketService()
{
    LOG_INFO(Network, "Setting up WebSocketService command handlers...");

    // Get concrete WebSocketService for template method access.
    auto* ws = getConcreteWebSocketService();
    DIRTSIM_ASSERT(ws != nullptr, "Failed to cast wsService_ to WebSocketService");

    // Register handlers for UI commands that come from CLI (port 7070).
    // All UI commands are queued to the state machine for processing.
    ws->registerHandler<UiApi::SimRun::Cwc>([this](UiApi::SimRun::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::SimPause::Cwc>(
        [this](UiApi::SimPause::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::SimStop::Cwc>([this](UiApi::SimStop::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::TrainingQuit::Cwc>(
        [this](UiApi::TrainingQuit::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::TrainingResultDiscard::Cwc>(
        [this](UiApi::TrainingResultDiscard::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::TrainingResultSave::Cwc>(
        [this](UiApi::TrainingResultSave::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::TrainingStart::Cwc>(
        [this](UiApi::TrainingStart::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::TrainingActiveScenarioControlsShow::Cwc>(
        [this](UiApi::TrainingActiveScenarioControlsShow::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::TrainingConfigShowEvolution::Cwc>(
        [this](UiApi::TrainingConfigShowEvolution::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::GenomeBrowserOpen::Cwc>(
        [this](UiApi::GenomeBrowserOpen::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::GenomeDetailLoad::Cwc>(
        [this](UiApi::GenomeDetailLoad::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::GenomeDetailOpen::Cwc>(
        [this](UiApi::GenomeDetailOpen::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::IconRailExpand::Cwc>(
        [this](UiApi::IconRailExpand::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::IconRailShowIcons::Cwc>(
        [this](UiApi::IconRailShowIcons::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::IconSelect::Cwc>(
        [this](UiApi::IconSelect::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::StateGet::Cwc>(
        [this](UiApi::StateGet::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::StatusGet::Cwc>(
        [this](UiApi::StatusGet::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::StopButtonPress::Cwc>(
        [this](UiApi::StopButtonPress::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::SynthKeyEvent::Cwc>(
        [this](UiApi::SynthKeyEvent::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::WebSocketAccessSet::Cwc>([this](UiApi::WebSocketAccessSet::Cwc cwc) {
        using Response = UiApi::WebSocketAccessSet::Response;

        auto* wsService = getConcreteWebSocketService();
        if (!wsService) {
            cwc.sendResponse(Response::error(ApiError("WebSocket service unavailable")));
            return;
        }

        if (wsPort_ == 0) {
            cwc.sendResponse(Response::error(ApiError("WebSocket port not set")));
            return;
        }

        UiApi::WebSocketAccessSet::Okay okay;
        okay.enabled = cwc.command.enabled;
        cwc.sendResponse(Response::okay(std::move(okay)));

        const std::string bindAddress = cwc.command.enabled ? "0.0.0.0" : "127.0.0.1";
        if (cwc.command.enabled) {
            wsService->setAccessToken(cwc.command.token);
        }
        else {
            wsService->clearAccessToken();
            wsService->closeNonLocalClients();
            if (webRtcStreamer_) {
                webRtcStreamer_->closeAllClients();
            }
        }

        wsService->stopListening(false);
        auto listenResult = wsService->listen(wsPort_, bindAddress);
        if (listenResult.isError()) {
            LOG_ERROR(
                Network,
                "WebSocketAccessSet failed to bind {}:{}: {}",
                bindAddress,
                wsPort_,
                listenResult.errorValue());
            return;
        }
    });
    ws->registerHandler<UiApi::ScreenGrab::Cwc>(
        [this](UiApi::ScreenGrab::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::StreamStart::Cwc>(
        [this](UiApi::StreamStart::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::WebRtcAnswer::Cwc>(
        [this](UiApi::WebRtcAnswer::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::WebRtcCandidate::Cwc>(
        [this](UiApi::WebRtcCandidate::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::Exit::Cwc>([this](UiApi::Exit::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::MouseDown::Cwc>(
        [this](UiApi::MouseDown::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::MouseMove::Cwc>(
        [this](UiApi::MouseMove::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::MouseUp::Cwc>([this](UiApi::MouseUp::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::PlantSeed::Cwc>(
        [this](UiApi::PlantSeed::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::DrawDebugToggle::Cwc>(
        [this](UiApi::DrawDebugToggle::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::PixelRendererToggle::Cwc>(
        [this](UiApi::PixelRendererToggle::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<UiApi::RenderModeSelect::Cwc>(
        [this](UiApi::RenderModeSelect::Cwc cwc) { queueEvent(cwc); });
    ws->registerHandler<Api::TrainingResult::Cwc>(
        [this](Api::TrainingResult::Cwc cwc) { queueEvent(cwc); });

    // NOTE: Binary callback for RenderMessages is set up in Disconnected state when connecting.
    // Don't set it here or it will overwrite that handler!

    // =========================================================================
    // JSON protocol support - for CLI and browser clients.
    // =========================================================================

    // Inject JSON deserializer.
    wsService_->setJsonDeserializer([](const std::string& json) -> std::any {
        CommandDeserializerJson deserializer;
        auto result = deserializer.deserialize(json);
        if (result.isError()) {
            // Throw on error - WebSocketService will catch and send error response.
            throw std::runtime_error(result.errorValue().message);
        }
        return result.value(); // Return UiApiCommand variant wrapped in std::any.
    });

    // Inject JSON command dispatcher.
    wsService_->setJsonCommandDispatcher(
        [this](
            std::any cmdAny,
            std::shared_ptr<rtc::WebSocket> ws,
            uint64_t correlationId,
            Network::WebSocketService::HandlerInvoker invokeHandler) {
            // Cast back to UiApiCommand variant.
            UiApiCommand cmdVariant = std::any_cast<UiApiCommand>(cmdAny);
// Macro to dispatch JSON commands with response data.
#define DISPATCH_UI_CMD_WITH_RESP(NamespaceType)                                            \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }

// Macro to dispatch JSON commands with empty response (success: true only).
#define DISPATCH_UI_CMD_EMPTY(NamespaceType)                                                \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }

            // Dispatch all UI commands.
            DISPATCH_UI_CMD_WITH_RESP(UiApi::DrawDebugToggle);
            DISPATCH_UI_CMD_EMPTY(UiApi::Exit);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::GenomeBrowserOpen);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::GenomeDetailLoad);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::GenomeDetailOpen);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::IconSelect);
            DISPATCH_UI_CMD_EMPTY(UiApi::MouseDown);
            DISPATCH_UI_CMD_EMPTY(UiApi::MouseMove);
            DISPATCH_UI_CMD_EMPTY(UiApi::MouseUp);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::PixelRendererToggle);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::RenderModeSelect);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::ScreenGrab);
            DISPATCH_UI_CMD_EMPTY(UiApi::SimPause);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::SimRun);
            DISPATCH_UI_CMD_EMPTY(UiApi::SimStop);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::StateGet);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::StatusGet);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::StopButtonPress);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::StreamStart);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::SynthKeyEvent);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::TrainingActiveScenarioControlsShow);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::TrainingConfigShowEvolution);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::TrainingQuit);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::WebRtcAnswer);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::WebRtcCandidate);
            DISPATCH_UI_CMD_WITH_RESP(UiApi::WebSocketAccessSet);

            // If we get here, command wasn't recognized.
            LOG_WARN(Network, "Unknown JSON command in dispatcher");

#undef DISPATCH_UI_CMD_WITH_RESP
#undef DISPATCH_UI_CMD_EMPTY
        });

    LOG_INFO(Network, "WebSocketService handlers registered");
}

StateMachine::~StateMachine()
{
    LOG_INFO(State, "Shutting down from state: {}", getCurrentStateName());

    // WebSocketService cleanup handled by unique_ptr.
}

void StateMachine::mainLoopRun()
{
    LOG_INFO(State, "Starting main event loop");

    queueEvent(InitCompleteEvent{});

    while (!shouldExit()) {
        processEvents();
    }

    LOG_INFO(State, "Main event loop exiting (shouldExit=true)");
}

void StateMachine::queueEvent(const Event& event)
{
    eventProcessor.enqueueEvent(event);
}

void StateMachine::processEvents()
{
    eventProcessor.processEventsFromQueue(*this);
}

void StateMachine::updateAnimations()
{
    // Track how often main loop runs (debug).
    static int callCount = 0;
    static double lastLogTime = 0.0;
    callCount++;

    double currentTime =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (currentTime - lastLogTime >= 60.0) {
        double loopFps = callCount / (currentTime - lastLogTime);
        LOG_INFO(State, "Main loop FPS = {:.1f}", loopFps);
        callCount = 0;
        lastLogTime = currentTime;
    }

    if (fractalAnimator_) {
        fractalAnimator_->update();
    }

    // Delegate to current state (if it has animation updates).
    std::visit(
        [](auto&& state) {
            if constexpr (requires { state.updateAnimations(); }) {
                state.updateAnimations();
            }
        },
        fsmState.getVariant());

    // Send WebRTC video frames to connected clients.
    if (webRtcStreamer_ && webRtcStreamer_->hasClients()) {
        webRtcStreamer_->sendFrame();
    }

    autoShrinkIfIdle();
}

bool StateMachine::isAutoShrinkBlocked() const
{
    if (uiManager_) {
        auto* panel = uiManager_->getExpandablePanel();
        if (panel && panel->isVisible()) {
            return true;
        }
    }

    return std::visit(
        [](const auto& state) -> bool {
            if constexpr (requires { state.isTrainingResultModalVisible(); }) {
                return state.isTrainingResultModalVisible();
            }
            return false;
        },
        fsmState.getVariant());
}

void StateMachine::autoShrinkIfIdle()
{
    if (!display || !uiManager_) {
        return;
    }

    uint32_t inactiveMs = lv_display_get_inactive_time(display);
    if (lastInactiveMs_ != 0 && inactiveMs < lastInactiveMs_) {
        LOG_DEBUG(
            State,
            "Auto-shrink activity detected, inactivity timer reset ({}ms -> {}ms)",
            lastInactiveMs_,
            inactiveMs);
        startMenuIdleActionTriggered_ = false;
    }
    lastInactiveMs_ = inactiveMs;

    const uint32_t startMenuIdleTimeoutMs = static_cast<uint32_t>(std::clamp(
        getUserSettings().startMenuIdleTimeoutMs,
        static_cast<int>(StartMenuIdleTimeoutMinMs),
        static_cast<int>(StartMenuIdleTimeoutMaxMs)));

    if (!startMenuIdleActionTriggered_ && inactiveMs >= startMenuIdleTimeoutMs
        && std::holds_alternative<State::StartMenu>(fsmState.getVariant())) {
        startMenuIdleActionTriggered_ = true;
        LOG_INFO(
            State,
            "StartMenu idle timeout reached (inactive={}ms, timeout={}ms), dispatching idle action",
            inactiveMs,
            startMenuIdleTimeoutMs);
        queueEvent(StartMenuIdleTimeoutEvent{});
    }

    auto* iconRail = uiManager_->getIconRail();
    if (!iconRail || iconRail->isMinimized()) {
        return;
    }

    if (isAutoShrinkBlocked()) {
        return;
    }

    if (inactiveMs < AutoShrinkTimeoutMs) {
        return;
    }

    LOG_DEBUG(
        State, "Auto-shrink idle timeout reached (inactive={}ms), minimizing IconRail", inactiveMs);
    iconRail->setMode(RailMode::Minimized);
}

void StateMachine::handleEvent(const Event& event)
{
    {
        // High-frequency events log at DEBUG to avoid spam.
        const bool isHighFrequency = std::holds_alternative<UiUpdateEvent>(event)
            || std::holds_alternative<EvolutionProgressReceivedEvent>(event)
            || std::holds_alternative<TrainingBestPlaybackFrameReceivedEvent>(event);
        const std::string msg = "Handling global event: " + getEventName(event);
        if (isHighFrequency) {
            LOG_DEBUG(State, "{}", msg);
        }
        else {
            LOG_INFO(State, "{}", msg);
        }
    }

    if (std::holds_alternative<UiApi::StateGet::Cwc>(event)) {
        LOG_DEBUG(State, "Processing StateGet command");
        auto& cwc = std::get<UiApi::StateGet::Cwc>(event);

        UiApi::StateGet::Okay state{
            .state = getCurrentStateName(),
        };

        std::visit(
            [&state](const auto& currentState) {
                using T = std::decay_t<decltype(currentState)>;
                if constexpr (std::is_same_v<T, State::SimRunning>) {
                    state.scenario_id = currentState.scenarioId;
                }
            },
            fsmState.getVariant());

        cwc.sendResponse(UiApi::StateGet::Response::okay(std::move(state)));
        return;
    }

    if (std::holds_alternative<ServerConnectedEvent>(event)) {
        if (!wsService_ || !wsService_->isConnected()) {
            LOG_WARN(State, "Ignoring ServerConnectedEvent without active WebSocket connection");
        }
        else {
            Api::EventSubscribe::Command eventCmd{
                .enabled = true,
                .connectionId = "",
            };
            auto result = wsService_->sendCommandAndGetResponse<Api::EventSubscribe::OkayType>(
                eventCmd, 2000);
            DIRTSIM_ASSERT(!result.isError(), "EventSubscribe failed: " + result.errorValue());
            DIRTSIM_ASSERT(
                !result.value().isError(),
                "EventSubscribe rejected: " + result.value().errorValue().message);
            LOG_INFO(State, "Subscribed to server event stream");

            userSettingsManager_.setWebSocketService(wsService_.get());
            userSettingsManager_.syncFromServerOrAssert(2000);
            applyServerUserSettings(userSettingsManager_.get());
        }
    }

    if (std::holds_alternative<UserSettingsUpdatedEvent>(event)) {
        const auto& settingsEvent = std::get<UserSettingsUpdatedEvent>(event);
        userSettingsManager_.applyServerUpdate(settingsEvent.settings);
        applyServerUserSettings(settingsEvent.settings);
    }

    // Handle StatusGet universally (works in all states).
    if (std::holds_alternative<UiApi::StatusGet::Cwc>(event)) {
        LOG_DEBUG(State, "Processing StatusGet command");
        auto& cwc = std::get<UiApi::StatusGet::Cwc>(event);

        // Get system health metrics.
        auto metrics = systemMetrics_.get();

        Ui::IconId selectedIcon = Ui::IconId::NONE;
        bool panelVisible = false;
        if (auto* uiManager = getUiComponentManager()) {
            if (auto* iconRail = uiManager->getIconRail()) {
                selectedIcon = iconRail->getSelectedIcon();
            }
            if (auto* panel = uiManager->getExpandablePanel()) {
                panelVisible = panel->isVisible();
            }
        }

        UiApi::StatusGet::StateDetails stateDetails = UiApi::StatusGet::NoStateDetails{};

        std::visit(
            [&stateDetails](const auto& currentState) {
                using StateType = std::decay_t<decltype(currentState)>;
                if constexpr (
                    std::is_same_v<StateType, State::Synth>
                    || std::is_same_v<StateType, State::SynthConfig>) {
                    stateDetails = UiApi::StatusGet::SynthStateDetails{
                        .last_key_index = currentState.getLastKeyIndex(),
                        .last_key_is_black = currentState.getLastKeyIsBlack(),
                    };
                }
            },
            fsmState.getVariant());

        UiApi::StatusGet::Okay status{
            .state = getCurrentStateName(),
            .connected_to_server = wsService_ && wsService_->isConnected(),
            .server_url = wsService_ ? wsService_->getUrl() : "",
            .display_width =
                display ? static_cast<uint32_t>(lv_display_get_horizontal_resolution(display)) : 0U,
            .display_height =
                display ? static_cast<uint32_t>(lv_display_get_vertical_resolution(display)) : 0U,
            .fps = getUiFps(),
            .cpu_percent = metrics.cpu_percent,
            .memory_percent = metrics.memory_percent,
            .selected_icon = selectedIcon,
            .panel_visible = panelVisible,
            .state_details = stateDetails,
        };

        LOG_DEBUG(State, "Sending StatusGet response (state={})", status.state);
        cwc.sendResponse(UiApi::StatusGet::Response::okay(std::move(status)));
        return;
    }

    if (std::holds_alternative<ServerDisconnectedEvent>(event)) {
        auto& evt = std::get<ServerDisconnectedEvent>(event);
        LOG_WARN(State, "Server disconnected (reason: {})", evt.reason);

        userSettingsManager_.setWebSocketService(nullptr);

        if (std::holds_alternative<State::Shutdown>(fsmState.getVariant())) {
            LOG_INFO(State, "Ignoring disconnect while shutting down");
            return;
        }

        if (std::holds_alternative<State::Disconnected>(fsmState.getVariant())) {
            LOG_INFO(State, "Already in Disconnected state");
        }
        else {
            LOG_INFO(State, "Transitioning back to Disconnected");
            if (!queueReconnectToLastServer()) {
                LOG_WARN(State, "No previous server address available for reconnect");
            }

            transitionTo(State::Disconnected{});
            return;
        }
    }

    // Handle Exit universally (works in all states).
    if (std::holds_alternative<UiApi::Exit::Cwc>(event)) {
        auto& cwc = std::get<UiApi::Exit::Cwc>(event);
        LOG_INFO(State, "Exit command received, shutting down");
        cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));
        transitionTo(State::Shutdown{});
        return;
    }

    // Handle mouse input with state-specific override (SimRunning) or fallback.
    if (std::holds_alternative<UiApi::MouseDown::Cwc>(event)) {
        auto& cwc = std::get<UiApi::MouseDown::Cwc>(event);
        bool handled = false;
        std::visit(
            [this, &cwc, &handled](auto&& state) {
                using StateType = std::decay_t<decltype(state)>;
                if constexpr (requires { state.onEvent(cwc, *this); }) {
                    handled = true;
                    auto newState = state.onEvent(cwc, *this);
                    if (!std::holds_alternative<StateType>(newState.getVariant())) {
                        transitionTo(std::move(newState));
                    }
                    else {
                        fsmState = std::move(newState);
                    }
                }
            },
            fsmState.getVariant());
        if (!handled) {
            if (getRemoteInputDevice()) {
                getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
                getRemoteInputDevice()->updatePressed(true);
            }
            cwc.sendResponse(UiApi::MouseDown::Response::okay(std::monostate{}));
        }
        return;
    }

    if (std::holds_alternative<UiApi::MouseMove::Cwc>(event)) {
        auto& cwc = std::get<UiApi::MouseMove::Cwc>(event);
        bool handled = false;
        std::visit(
            [this, &cwc, &handled](auto&& state) {
                using StateType = std::decay_t<decltype(state)>;
                if constexpr (requires { state.onEvent(cwc, *this); }) {
                    handled = true;
                    auto newState = state.onEvent(cwc, *this);
                    if (!std::holds_alternative<StateType>(newState.getVariant())) {
                        transitionTo(std::move(newState));
                    }
                    else {
                        fsmState = std::move(newState);
                    }
                }
            },
            fsmState.getVariant());
        if (!handled) {
            if (getRemoteInputDevice()) {
                getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
            }
            cwc.sendResponse(UiApi::MouseMove::Response::okay(std::monostate{}));
        }
        return;
    }

    if (std::holds_alternative<UiApi::MouseUp::Cwc>(event)) {
        auto& cwc = std::get<UiApi::MouseUp::Cwc>(event);
        bool handled = false;
        std::visit(
            [this, &cwc, &handled](auto&& state) {
                using StateType = std::decay_t<decltype(state)>;
                if constexpr (requires { state.onEvent(cwc, *this); }) {
                    handled = true;
                    auto newState = state.onEvent(cwc, *this);
                    if (!std::holds_alternative<StateType>(newState.getVariant())) {
                        transitionTo(std::move(newState));
                    }
                    else {
                        fsmState = std::move(newState);
                    }
                }
            },
            fsmState.getVariant());
        if (!handled) {
            if (getRemoteInputDevice()) {
                getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
                getRemoteInputDevice()->updatePressed(false);
            }
            cwc.sendResponse(UiApi::MouseUp::Response::okay(std::monostate{}));
        }
        return;
    }

    if (std::holds_alternative<UiApi::IconSelect::Cwc>(event)) {
        auto& cwc = std::get<UiApi::IconSelect::Cwc>(event);
        auto* uiManager = getUiComponentManager();
        if (!uiManager) {
            cwc.sendResponse(
                UiApi::IconSelect::Response::error(ApiError("UI manager unavailable")));
            return;
        }

        auto* iconRail = uiManager->getIconRail();
        if (!iconRail) {
            cwc.sendResponse(UiApi::IconSelect::Response::error(ApiError("IconRail unavailable")));
            return;
        }

        bool selected = false;
        if (cwc.command.id == IconId::NONE) {
            iconRail->deselectAll();
        }
        else if (iconRail->isIconSelectable(cwc.command.id)) {
            iconRail->selectIcon(cwc.command.id);
            selected = true;
        }

        UiApi::IconSelect::Okay response{ .selected = selected };
        cwc.sendResponse(UiApi::IconSelect::Response::okay(std::move(response)));
        return;
    }

    if (std::holds_alternative<UiApi::IconRailExpand::Cwc>(event)) {
        auto& cwc = std::get<UiApi::IconRailExpand::Cwc>(event);
        auto* uiManager = getUiComponentManager();
        if (!uiManager) {
            cwc.sendResponse(
                UiApi::IconRailExpand::Response::error(ApiError("UI manager unavailable")));
            return;
        }

        auto* iconRail = uiManager->getIconRail();
        if (!iconRail) {
            cwc.sendResponse(
                UiApi::IconRailExpand::Response::error(ApiError("IconRail unavailable")));
            return;
        }

        iconRail->setMode(RailMode::Normal);
        UiApi::IconRailExpand::Okay response{ .expanded = !iconRail->isMinimized() };
        cwc.sendResponse(UiApi::IconRailExpand::Response::okay(std::move(response)));
        return;
    }

    if (std::holds_alternative<UiApi::IconRailShowIcons::Cwc>(event)) {
        auto& cwc = std::get<UiApi::IconRailShowIcons::Cwc>(event);
        auto* uiManager = getUiComponentManager();
        if (!uiManager) {
            cwc.sendResponse(
                UiApi::IconRailShowIcons::Response::error(ApiError("UI manager unavailable")));
            return;
        }

        auto* iconRail = uiManager->getIconRail();
        if (!iconRail) {
            cwc.sendResponse(
                UiApi::IconRailShowIcons::Response::error(ApiError("IconRail unavailable")));
            return;
        }

        iconRail->showIcons();
        if (display) {
            lv_display_trigger_activity(display);
            lastInactiveMs_ = 0;
            startMenuIdleActionTriggered_ = false;
        }
        UiApi::IconRailShowIcons::Okay response{ .shown = !iconRail->isMinimized() };
        cwc.sendResponse(UiApi::IconRailShowIcons::Response::okay(std::move(response)));
        return;
    }

    // Handle ScreenGrab.
    if (std::holds_alternative<UiApi::ScreenGrab::Cwc>(event)) {
        auto& cwc = std::get<UiApi::ScreenGrab::Cwc>(event);

        LOG_INFO(State, "Processing ScreenGrab command (scale={})", cwc.command.scale);

        // Capture display pixels.
        auto screenshotData = captureDisplayPixels(display, cwc.command.scale);
        if (!screenshotData) {
            LOG_ERROR(State, "Failed to capture display pixels");
            try {
                cwc.sendResponse(
                    UiApi::ScreenGrab::Response::error(ApiError("Failed to capture display")));
            }
            catch (const std::exception& e) {
                LOG_WARN(State, "Failed to send error response: {}", e.what());
            }
            return;
        }

        const bool wantsBinaryPayload = cwc.usesBinary && cwc.command.binaryPayload;
        std::string payloadData;
        bool isKeyframe = true;
        uint64_t timestampMs = 0;
        UiApi::ScreenGrab::Format responseFormat = cwc.command.format;

        if (cwc.command.format == UiApi::ScreenGrab::Format::H264) {
            // H.264 encoding requested.
            // Lazy-initialize encoder if needed or if size changed.
            // Round to even for comparison (encoder internally uses even dimensions).
            uint32_t evenWidth = screenshotData->width & ~1u;
            uint32_t evenHeight = screenshotData->height & ~1u;

            if (!h264Encoder_ || h264Encoder_->getWidth() != evenWidth
                || h264Encoder_->getHeight() != evenHeight) {
                h264Encoder_ = std::make_unique<H264Encoder>();
                if (!h264Encoder_->initialize(screenshotData->width, screenshotData->height)) {
                    LOG_ERROR(State, "Failed to initialize H.264 encoder");
                    cwc.sendResponse(
                        UiApi::ScreenGrab::Response::error(
                            ApiError("Failed to initialize H.264 encoder")));
                    return;
                }
            }

            // Encode frame.
            auto encoded = h264Encoder_->encode(
                screenshotData->pixels.data(), screenshotData->width, screenshotData->height);
            if (!encoded) {
                LOG_ERROR(State, "H.264 encoding failed");
                cwc.sendResponse(
                    UiApi::ScreenGrab::Response::error(ApiError("H.264 encoding failed")));
                return;
            }

            if (wantsBinaryPayload) {
                payloadData.assign(
                    reinterpret_cast<const char*>(encoded->data.data()), encoded->data.size());
            }
            else {
                payloadData = base64Encode(encoded->data);
            }
            isKeyframe = encoded->isKeyframe;
            timestampMs = encoded->timestampMs;

            if (wantsBinaryPayload) {
                LOG_INFO(
                    State,
                    "ScreenGrab H.264 encoded {}x{} ({} bytes raw -> {} bytes h264, keyframe={})",
                    screenshotData->width,
                    screenshotData->height,
                    screenshotData->pixels.size(),
                    encoded->data.size(),
                    isKeyframe);
            }
            else {
                LOG_INFO(
                    State,
                    "ScreenGrab H.264 encoded {}x{} ({} bytes raw -> {} bytes h264 -> {} bytes "
                    "base64,"
                    " keyframe={})",
                    screenshotData->width,
                    screenshotData->height,
                    screenshotData->pixels.size(),
                    encoded->data.size(),
                    payloadData.size(),
                    isKeyframe);
            }
        }
        else if (cwc.command.format == UiApi::ScreenGrab::Format::Png) {
            // PNG encoding requested.
            auto pngData = encodePNG(
                screenshotData->pixels.data(), screenshotData->width, screenshotData->height);
            if (pngData.empty()) {
                LOG_ERROR(State, "PNG encoding failed");
                cwc.sendResponse(
                    UiApi::ScreenGrab::Response::error(ApiError("PNG encoding failed")));
                return;
            }

            if (wantsBinaryPayload) {
                payloadData.assign(reinterpret_cast<const char*>(pngData.data()), pngData.size());
            }
            else {
                payloadData = base64Encode(pngData);
            }

            auto now = std::chrono::system_clock::now();
            timestampMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                    .count());

            if (wantsBinaryPayload) {
                LOG_INFO(
                    State,
                    "ScreenGrab PNG encoded {}x{} ({} bytes raw -> {} bytes png)",
                    screenshotData->width,
                    screenshotData->height,
                    screenshotData->pixels.size(),
                    pngData.size());
            }
            else {
                LOG_INFO(
                    State,
                    "ScreenGrab PNG encoded {}x{} ({} bytes raw -> {} bytes png -> {} bytes "
                    "base64)",
                    screenshotData->width,
                    screenshotData->height,
                    screenshotData->pixels.size(),
                    pngData.size(),
                    payloadData.size());
            }
        }
        else {
            // Raw ARGB8888 format.
            if (wantsBinaryPayload) {
                payloadData.assign(
                    reinterpret_cast<const char*>(screenshotData->pixels.data()),
                    screenshotData->pixels.size());
            }
            else {
                payloadData = base64Encode(screenshotData->pixels);
            }

            auto now = std::chrono::system_clock::now();
            timestampMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                    .count());

            if (wantsBinaryPayload) {
                LOG_INFO(
                    State,
                    "ScreenGrab captured {}x{} ({} bytes raw)",
                    screenshotData->width,
                    screenshotData->height,
                    screenshotData->pixels.size());
            }
            else {
                LOG_INFO(
                    State,
                    "ScreenGrab captured {}x{} ({} bytes raw, {} bytes base64)",
                    screenshotData->width,
                    screenshotData->height,
                    screenshotData->pixels.size(),
                    payloadData.size());
            }
        }

        try {
            // For H.264, use encoder's dimensions (may be rounded to even).
            // For raw, use screenshot dimensions.
            uint32_t responseWidth = screenshotData->width;
            uint32_t responseHeight = screenshotData->height;
            if (cwc.command.format == UiApi::ScreenGrab::Format::H264 && h264Encoder_) {
                responseWidth = h264Encoder_->getWidth();
                responseHeight = h264Encoder_->getHeight();
            }

            UiApi::ScreenGrab::Okay response{ .data = std::move(payloadData),
                                              .width = responseWidth,
                                              .height = responseHeight,
                                              .format = responseFormat,
                                              .timestampMs = timestampMs,
                                              .isKeyframe = isKeyframe };
            cwc.sendResponse(UiApi::ScreenGrab::Response::okay(std::move(response)));
        }
        catch (const std::exception& e) {
            LOG_WARN(State, "Failed to send response: {}", e.what());
        }
        return;
    }

    // Handle StreamStart - browser requests to start video stream.
    if (std::holds_alternative<UiApi::StreamStart::Cwc>(event)) {
        auto& cwc = std::get<UiApi::StreamStart::Cwc>(event);
        LOG_INFO(
            Network,
            "StreamStart from client {} (connectionId={})",
            cwc.command.clientId,
            cwc.command.connectionId);

        if (!webRtcStreamer_) {
            cwc.sendResponse(
                UiApi::StreamStart::Response::error(ApiError("WebRTC streamer not available")));
            return;
        }

        // Create callback for sending ICE candidates back to this client.
        std::string connectionId = cwc.command.connectionId;
        auto onIceCandidate = [this, connectionId](const std::string& candidateJson) {
            if (wsService_) {
                auto result = wsService_->sendToClient(connectionId, candidateJson);
                if (result.isError()) {
                    LOG_WARN(Network, "Failed to send ICE candidate: {}", result.errorValue());
                }
            }
        };

        // Initiate stream and get SDP offer synchronously.
        std::string sdpOffer =
            webRtcStreamer_->initiateStream(cwc.command.clientId, onIceCandidate);

        if (sdpOffer.empty()) {
            cwc.sendResponse(
                UiApi::StreamStart::Response::error(ApiError("Failed to create WebRTC offer")));
            return;
        }

        UiApi::StreamStart::Okay response{ .initiated = true, .sdpOffer = std::move(sdpOffer) };
        cwc.sendResponse(UiApi::StreamStart::Response::okay(std::move(response)));
        return;
    }

    // Handle WebRtcAnswer - browser's answer to our offer.
    if (std::holds_alternative<UiApi::WebRtcAnswer::Cwc>(event)) {
        auto& cwc = std::get<UiApi::WebRtcAnswer::Cwc>(event);
        LOG_INFO(Network, "WebRtcAnswer from client {}", cwc.command.clientId);

        if (webRtcStreamer_) {
            webRtcStreamer_->handleAnswer(cwc.command.clientId, cwc.command.sdp);
            UiApi::WebRtcAnswer::Okay response{ .accepted = true };
            cwc.sendResponse(UiApi::WebRtcAnswer::Response::okay(std::move(response)));
        }
        else {
            cwc.sendResponse(
                UiApi::WebRtcAnswer::Response::error(ApiError("WebRTC streamer not available")));
        }
        return;
    }

    // Handle WebRtcCandidate universally (works in all states).
    if (std::holds_alternative<UiApi::WebRtcCandidate::Cwc>(event)) {
        auto& cwc = std::get<UiApi::WebRtcCandidate::Cwc>(event);
        LOG_DEBUG(Network, "Processing WebRtcCandidate from client {}", cwc.command.clientId);

        if (webRtcStreamer_) {
            webRtcStreamer_->handleCandidate(
                cwc.command.clientId, cwc.command.candidate, cwc.command.mid);
            UiApi::WebRtcCandidate::Okay response{ .added = true };
            cwc.sendResponse(UiApi::WebRtcCandidate::Response::okay(std::move(response)));
        }
        else {
            cwc.sendResponse(
                UiApi::WebRtcCandidate::Response::error(ApiError("WebRTC streamer not available")));
        }
        return;
    }

    std::visit(
        [this](auto&& evt) {
            std::visit(
                [this, &evt](auto&& state) -> void {
                    using StateType = std::decay_t<decltype(state)>;

                    if constexpr (requires { state.onEvent(evt, *this); }) {
                        auto newState = state.onEvent(evt, *this);
                        if (!std::holds_alternative<StateType>(newState.getVariant())) {
                            transitionTo(std::move(newState));
                        }
                        else {
                            // Same state type - move it back into variant to preserve state.
                            fsmState = std::move(newState);
                        }
                    }
                    else {
                        // Handle state-independent events generically.
                        if constexpr (
                            std::is_same_v<std::decay_t<decltype(evt)>, UiUpdateEvent>
                            || std::is_same_v<std::decay_t<decltype(evt)>, UserSettingsUpdatedEvent>
                            || std::is_same_v<
                                std::decay_t<decltype(evt)>,
                                TrainingBestPlaybackFrameReceivedEvent>) {
                            // UiUpdateEvent can arrive in any state (server keeps sending updates).
                            // States that care (SimRunning) have specific handlers.
                            // Other states (Paused, etc.) gracefully ignore without warning.
                            LOG_INFO(
                                State,
                                "Ignoring {} in state {}",
                                getEventName(Event{ evt }),
                                State::getCurrentStateName(fsmState));
                            // Stay in current state - no transition.
                        }
                        else {
                            LOG_WARN(
                                State,
                                "State {} does not handle event {}",
                                State::getCurrentStateName(fsmState),
                                getEventName(Event{ evt }));

                            // If this is an API command with sendResponse, send error.
                            if constexpr (requires {
                                              evt.sendResponse(
                                                  std::declval<typename std::decay_t<
                                                      decltype(evt)>::Response>());
                                          }) {
                                auto errorMsg = std::string("Command not supported in state: ")
                                    + State::getCurrentStateName(fsmState);
                                using EventType = std::decay_t<decltype(evt)>;
                                using ResponseType = typename EventType::Response;
                                evt.sendResponse(ResponseType::error(ApiError(errorMsg)));
                            }
                        }
                    }
                },
                fsmState.getVariant());
        },
        event);
}

void StateMachine::applyServerUserSettings(const DirtSim::UserSettings& settings)
{
    setSynthVolumePercent(settings.volumePercent);
    syncAudioMasterVolume(settings.volumePercent);
}

void StateMachine::syncAudioMasterVolume(int volumePercent)
{
    const int clampedVolume = std::clamp(volumePercent, 0, 100);

    Network::WebSocketService audioClient;
    const auto connectResult = audioClient.connect("ws://localhost:6060", 200);
    if (connectResult.isError()) {
        if (!audioVolumeWarningLogged_) {
            LOG_WARN(
                State, "Audio service unavailable for volume sync: {}", connectResult.errorValue());
            audioVolumeWarningLogged_ = true;
        }
        return;
    }

    AudioApi::MasterVolumeSet::Command cmd{ .volume_percent = clampedVolume };
    const auto result =
        audioClient.sendCommandAndGetResponse<AudioApi::MasterVolumeSet::Okay>(cmd, 500);
    if (result.isError()) {
        LOG_WARN(State, "MasterVolumeSet failed: {}", result.errorValue());
        return;
    }

    if (result.value().isError()) {
        LOG_WARN(State, "MasterVolumeSet rejected: {}", result.value().errorValue().message);
        return;
    }

    audioClient.disconnect();
    audioVolumeWarningLogged_ = false;
}

std::string StateMachine::getCurrentStateName() const
{
    return State::getCurrentStateName(fsmState);
}

double StateMachine::getUiFps() const
{
    return std::visit(
        [](const auto& state) -> double {
            using T = std::decay_t<decltype(state)>;
            if constexpr (std::is_same_v<T, State::SimRunning>) {
                return state.smoothedUiFps;
            }
            return 0.0;
        },
        fsmState.getVariant());
}

Network::WebSocketServiceInterface& StateMachine::getWebSocketService()
{
    assert(wsService_ && "wsService_ is null!");
    return *wsService_.get();
}

Network::WebSocketService* StateMachine::getConcreteWebSocketService()
{
    return dynamic_cast<Network::WebSocketService*>(wsService_.get());
}

void StateMachine::setLastServerAddress(const std::string& host, uint16_t port)
{
    lastServerHost_ = host;
    lastServerPort_ = port;
    hasLastServerAddress_ = !lastServerHost_.empty() && lastServerPort_ != 0;
}

bool StateMachine::queueReconnectToLastServer()
{
    if (!hasLastServerAddress_) {
        return false;
    }

    queueEvent(ConnectToServerCommand{ lastServerHost_, lastServerPort_ });
    return true;
}

void StateMachine::transitionTo(State::Any newState)
{
    const bool wasStartMenu = std::holds_alternative<State::StartMenu>(fsmState.getVariant());
    std::string oldStateName = State::getCurrentStateName(fsmState);

    invokeOnExit(fsmState, *this);

    auto expectedIndex = newState.getVariant().index();
    fsmState = std::move(newState);
    const bool isStartMenu = std::holds_alternative<State::StartMenu>(fsmState.getVariant());

    if (!wasStartMenu && isStartMenu) {
        if (display) {
            lv_display_trigger_activity(display);
        }
        lastInactiveMs_ = 0;
        startMenuIdleActionTriggered_ = false;
        LOG_INFO(State, "StartMenu entered, reset idle auto-start timer");
    }

    std::string newStateName = State::getCurrentStateName(fsmState);
    LOG_INFO(State, "Ui::StateMachine: {} -> {}", oldStateName, newStateName);

    fsmState = invokeOnEnter(std::move(fsmState), *this);

    // Chain transition if onEnter redirected to a different state.
    if (fsmState.getVariant().index() != expectedIndex) {
        transitionTo(std::move(fsmState));
    }
}

} // namespace Ui
} // namespace DirtSim
