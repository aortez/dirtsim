#pragma once

#include "Event.h"
#include "EventProcessor.h"
#include "EventSink.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/Timers.h"
#include "states/State.h"
#include <memory>
#include <string>

// Forward declaration for LVGL display structure.
struct _lv_display_t;

// Forward declarations for network and UI components.
namespace DirtSim {

class H264Encoder;

namespace Network {
class WebSocketService;
}

namespace Ui {
class RemoteInputDevice;
class UiComponentManager;
class WebRtcStreamer;
} // namespace Ui
} // namespace DirtSim

namespace DirtSim {
namespace Ui {

class StateMachine : public StateMachineBase,
                     public StateMachineInterface<Event>,
                     public EventSink {
public:
    explicit StateMachine(_lv_display_t* display, uint16_t wsPort = 7070);
    ~StateMachine();

    void mainLoopRun();
    void queueEvent(const Event& event) override;
    void handleEvent(const Event& event);

    std::string getCurrentStateName() const override;
    void processEvents() override;

    // Update background animations (called every main loop iteration).
    void updateAnimations();

    _lv_display_t* display = nullptr;
    EventProcessor eventProcessor;

    // WebSocket connections.
    std::unique_ptr<Network::WebSocketService> wsService_; // Unified service (client + server).

    // UI management.
    std::unique_ptr<UiComponentManager> uiManager_;        // LVGL screen and container management.
    std::unique_ptr<RemoteInputDevice> remoteInputDevice_; // Remote mouse input from WebSocket.

    // WebRTC video streaming.
    std::unique_ptr<WebRtcStreamer> webRtcStreamer_;

    /**
     * @brief Get WebSocketService (unified client + server).
     * @return Pointer to WebSocketService (non-owning).
     */
    Network::WebSocketService* getWebSocketService() { return wsService_.get(); }

    /**
     * @brief Setup WebSocketService for UI operation.
     *
     * Configures both client role (connect to server) and server role (listen for CLI).
     */
    void setupWebSocketService();

    /**
     * @brief Get UI manager for LVGL screen/container access.
     * @return Pointer to UI manager (non-owning).
     */
    UiComponentManager* getUiComponentManager() { return uiManager_.get(); }

    /**
     * @brief Get remote input device for WebSocket mouse events.
     * @return Pointer to RemoteInputDevice (non-owning).
     */
    RemoteInputDevice* getRemoteInputDevice() { return remoteInputDevice_.get(); }

    /**
     * @brief Get WebRTC streamer for video streaming.
     * @return Pointer to WebRtcStreamer (non-owning).
     */
    WebRtcStreamer* getWebRtcStreamer() { return webRtcStreamer_.get(); }

    /**
     * @brief Get performance timers for instrumentation.
     * @return Reference to timers.
     */
    Timers& getTimers() { return timers_; }

private:
    Timers timers_; // Performance instrumentation timers.
    State::Any fsmState{ State::Startup{} };
    std::unique_ptr<H264Encoder> h264Encoder_; // Lazy-initialized H.264 encoder.

    void transitionTo(State::Any newState);

    template <typename StateType>
    void callOnEnter(StateType& state)
    {
        if constexpr (requires { state.onEnter(*this); }) {
            state.onEnter(*this);
        }
    }

    template <typename StateType>
    void callOnExit(StateType& state)
    {
        if constexpr (requires { state.onExit(*this); }) {
            state.onExit(*this);
        }
    }
};

} // namespace Ui
} // namespace DirtSim
