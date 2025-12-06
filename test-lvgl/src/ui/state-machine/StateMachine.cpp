#include "StateMachine.h"
#include "api/StreamStart.h"
#include "api/WebRtcAnswer.h"
#include "api/WebRtcCandidate.h"
#include "core/encoding/H264Encoder.h"
#include "core/network/WebSocketService.h"
#include "network/WebSocketServer.h"
#include "states/State.h"
#include "ui/DisplayCapture.h"
#include "ui/UiComponentManager.h"
#include "ui/rendering/WebRtcStreamer.h"
#include <chrono>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

StateMachine::StateMachine(_lv_display_t* disp, uint16_t wsPort) : display(disp)
{
    spdlog::info("Ui::StateMachine initialized in state: {}", getCurrentStateName());

    // Create OLD WebSocket server for accepting remote commands (CLI port 7070).
    // TODO: Migrate to wsService_->listen(7070) once ready.
    wsServer_ = std::make_unique<WebSocketServer>(*this, wsPort);
    wsServer_->start();
    spdlog::info("Ui::StateMachine: WebSocket server listening on port {}", wsPort);

    // Create unified WebSocketService (replaces old wsClient_).
    wsService_ = std::make_unique<Network::WebSocketService>();
    setupWebSocketService();
    spdlog::info("Ui::StateMachine: WebSocketService initialized");

    // Create UI manager for LVGL screen/container management.
    uiManager_ = std::make_unique<UiComponentManager>(disp);
    spdlog::info("Ui::StateMachine: UiComponentManager created");

    // Create WebRTC streamer for video streaming.
    webRtcStreamer_ = std::make_unique<WebRtcStreamer>();
    webRtcStreamer_->setDisplay(disp);

    // Set up signaling callback to send answers back to clients.
    webRtcStreamer_->setSignalingCallback(
        [this](const std::string& clientId, const std::string& message) {
            // Send the signaling message (SDP answer) to the client via WebSocket.
            if (wsServer_) {
                wsServer_->broadcastText(message);
                spdlog::debug("WebRtcStreamer: Sent signaling message to client {}", clientId);
            }
        });
    spdlog::info("Ui::StateMachine: WebRtcStreamer created");
}

void StateMachine::setupWebSocketService()
{
    spdlog::info("Ui::StateMachine: Setting up WebSocketService command handlers...");

    // Register handlers for UI commands that come from CLI (port 7070).
    // All UI commands are queued to the state machine for processing.
    wsService_->registerHandler<UiApi::SimRun::Cwc>(
        [this](UiApi::SimRun::Cwc cwc) { queueEvent(cwc); });
    wsService_->registerHandler<UiApi::SimPause::Cwc>(
        [this](UiApi::SimPause::Cwc cwc) { queueEvent(cwc); });
    wsService_->registerHandler<UiApi::SimStop::Cwc>(
        [this](UiApi::SimStop::Cwc cwc) { queueEvent(cwc); });
    wsService_->registerHandler<UiApi::StatusGet::Cwc>(
        [this](UiApi::StatusGet::Cwc cwc) { queueEvent(cwc); });

    // NOTE: Binary callback for RenderMessages is set up in Disconnected state when connecting.
    // Don't set it here or it will overwrite that handler!

    // TODO: Register remaining UI commands (ScreenGrab, DrawDebugToggle, etc.).

    spdlog::info("Ui::StateMachine: WebSocketService handlers registered");
}

StateMachine::~StateMachine()
{
    spdlog::info("Ui::StateMachine shutting down from state: {}", getCurrentStateName());

    // Stop and clean up WebSocket server (unique_ptr handles deletion).
    if (wsServer_) {
        wsServer_->stop();
    }

    // WebSocketService cleanup handled by unique_ptr.
}

void StateMachine::mainLoopRun()
{
    spdlog::info("Starting UI main event loop");

    // Initialize by sending init complete event.
    queueEvent(InitCompleteEvent{});

    // Main event processing loop.
    while (!shouldExit()) {
        processEvents();
        // TODO: LVGL event processing integration.
        // TODO: WebSocket client/server event processing.
    }

    spdlog::info("UI main event loop exiting (shouldExit=true)");
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
    if (currentTime - lastLogTime >= 10.0) {
        double loopFps = callCount / (currentTime - lastLogTime);
        spdlog::info("StateMachine: Main loop FPS = {:.1f}", loopFps);
        callCount = 0;
        lastLogTime = currentTime;
    }

    // Delegate to current state (if it has animation updates).
    std::visit(
        [](auto&& state) {
            if constexpr (requires { state.updateAnimations(); }) {
                state.updateAnimations();
            }
        },
        fsmState);

    // Send WebRTC video frames to connected clients.
    if (webRtcStreamer_ && webRtcStreamer_->hasClients()) {
        webRtcStreamer_->sendFrame();
    }
}

void StateMachine::handleEvent(const Event& event)
{
    spdlog::debug("Ui::StateMachine: Handling event: {}", getEventName(event));

    // Handle StatusGet universally (works in all states).
    if (std::holds_alternative<UiApi::StatusGet::Cwc>(event)) {
        spdlog::debug("Ui::StateMachine: Processing StatusGet command");
        auto& cwc = std::get<UiApi::StatusGet::Cwc>(event);

        UiApi::StatusGet::Okay status{
            .state = getCurrentStateName(),
            .connected_to_server = wsService_ && wsService_->isConnected(),
            .server_url = wsService_ ? wsService_->getUrl() : "",
            .display_width =
                display ? static_cast<uint32_t>(lv_display_get_horizontal_resolution(display)) : 0U,
            .display_height =
                display ? static_cast<uint32_t>(lv_display_get_vertical_resolution(display)) : 0U,
            .fps = 0.0 // TODO: Track and return actual FPS.
        };

        spdlog::debug("Ui::StateMachine: Sending StatusGet response (state={})", status.state);
        cwc.sendResponse(UiApi::StatusGet::Response::okay(std::move(status)));
        return;
    }

    // Handle ScreenGrab universally (works in all states).
    // Note: Throttling is handled per-client in WebSocketServer before queuing.
    if (std::holds_alternative<UiApi::ScreenGrab::Cwc>(event)) {
        auto& cwc = std::get<UiApi::ScreenGrab::Cwc>(event);

        spdlog::info(
            "Ui::StateMachine: Processing ScreenGrab command (scale={})", cwc.command.scale);

        // Capture display pixels.
        auto screenshotData = captureDisplayPixels(display, cwc.command.scale);
        if (!screenshotData) {
            spdlog::error("Ui::StateMachine: Failed to capture display pixels");
            try {
                cwc.sendResponse(
                    UiApi::ScreenGrab::Response::error(ApiError("Failed to capture display")));
            }
            catch (const std::exception& e) {
                spdlog::warn("Ui::StateMachine: Failed to send error response: {}", e.what());
            }
            return;
        }

        std::string base64Data;
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
                    spdlog::error("Ui::StateMachine: Failed to initialize H.264 encoder");
                    cwc.sendResponse(UiApi::ScreenGrab::Response::error(
                        ApiError("Failed to initialize H.264 encoder")));
                    return;
                }
            }

            // Encode frame.
            auto encoded = h264Encoder_->encode(
                screenshotData->pixels.data(), screenshotData->width, screenshotData->height);
            if (!encoded) {
                spdlog::error("Ui::StateMachine: H.264 encoding failed");
                cwc.sendResponse(
                    UiApi::ScreenGrab::Response::error(ApiError("H.264 encoding failed")));
                return;
            }

            base64Data = base64Encode(encoded->data);
            isKeyframe = encoded->isKeyframe;
            timestampMs = encoded->timestampMs;

            spdlog::info(
                "Ui::StateMachine: ScreenGrab H.264 encoded {}x{} ({} bytes raw -> {} bytes h264 "
                "-> {} bytes base64, keyframe={})",
                screenshotData->width,
                screenshotData->height,
                screenshotData->pixels.size(),
                encoded->data.size(),
                base64Data.size(),
                isKeyframe);
        }
        else {
            // Raw ARGB8888 format.
            base64Data = base64Encode(screenshotData->pixels);

            auto now = std::chrono::system_clock::now();
            timestampMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                    .count());

            spdlog::info(
                "Ui::StateMachine: ScreenGrab captured {}x{} ({} bytes raw, {} bytes base64)",
                screenshotData->width,
                screenshotData->height,
                screenshotData->pixels.size(),
                base64Data.size());
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

            UiApi::ScreenGrab::Okay response{ .data = std::move(base64Data),
                                              .width = responseWidth,
                                              .height = responseHeight,
                                              .format = responseFormat,
                                              .timestampMs = timestampMs,
                                              .isKeyframe = isKeyframe };
            cwc.sendResponse(UiApi::ScreenGrab::Response::okay(std::move(response)));
        }
        catch (const std::exception& e) {
            spdlog::warn("Ui::StateMachine: Failed to send response: {}", e.what());
        }
        return;
    }

    // Handle StreamStart - browser requests to start video stream.
    if (std::holds_alternative<UiApi::StreamStart::Cwc>(event)) {
        auto& cwc = std::get<UiApi::StreamStart::Cwc>(event);
        spdlog::info("Ui::StateMachine: StreamStart from client {}", cwc.command.clientId);

        if (webRtcStreamer_) {
            webRtcStreamer_->initiateStream(cwc.command.clientId);
            // Note: The actual SDP offer is sent via the signaling callback.
            UiApi::StreamStart::Okay response{ .initiated = true };
            cwc.sendResponse(UiApi::StreamStart::Response::okay(std::move(response)));
        }
        else {
            cwc.sendResponse(
                UiApi::StreamStart::Response::error(ApiError("WebRTC streamer not available")));
        }
        return;
    }

    // Handle WebRtcAnswer - browser's answer to our offer.
    if (std::holds_alternative<UiApi::WebRtcAnswer::Cwc>(event)) {
        auto& cwc = std::get<UiApi::WebRtcAnswer::Cwc>(event);
        spdlog::info("Ui::StateMachine: WebRtcAnswer from client {}", cwc.command.clientId);

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
        spdlog::debug(
            "Ui::StateMachine: Processing WebRtcCandidate from client {}", cwc.command.clientId);

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
                        if (!std::holds_alternative<StateType>(newState)) {
                            transitionTo(std::move(newState));
                        }
                        else {
                            // Same state type - move it back into variant to preserve state.
                            fsmState = std::move(newState);
                        }
                    }
                    else {
                        // Handle state-independent events generically.
                        if constexpr (std::is_same_v<std::decay_t<decltype(evt)>, UiUpdateEvent>) {
                            // UiUpdateEvent can arrive in any state (server keeps sending updates).
                            // States that care (SimRunning) have specific handlers.
                            // Other states (Paused, etc.) gracefully ignore without warning.
                            spdlog::info(
                                "Ui::StateMachine: Ignoring UiUpdateEvent in state {}",
                                State::getCurrentStateName(fsmState));
                            // Stay in current state - no transition.
                        }
                        else {
                            spdlog::warn(
                                "Ui::StateMachine: State {} does not handle event {}",
                                State::getCurrentStateName(fsmState),
                                getEventName(Event{ evt }));

                            // If this is an API command with sendResponse, send error.
                            if constexpr (requires {
                                              evt.sendResponse(std::declval<typename std::decay_t<
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
                fsmState);
        },
        event);
}

std::string StateMachine::getCurrentStateName() const
{
    return State::getCurrentStateName(fsmState);
}

void StateMachine::transitionTo(State::Any newState)
{
    std::string oldStateName = State::getCurrentStateName(fsmState);

    std::visit([this](auto&& state) { callOnExit(state); }, fsmState);

    fsmState = std::move(newState);

    std::string newStateName = State::getCurrentStateName(fsmState);
    spdlog::info("Ui::StateMachine: {} -> {}", oldStateName, newStateName);

    std::visit([this](auto&& state) { callOnEnter(state); }, fsmState);
}

} // namespace Ui
} // namespace DirtSim
