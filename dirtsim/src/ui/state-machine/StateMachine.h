#pragma once

#include "Event.h"
#include "EventProcessor.h"
#include "EventSink.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/SystemMetrics.h"
#include "core/Timers.h"
#include "states/State.h"

#include <memory>
#include <string>

struct _lv_display_t;

namespace DirtSim {

class H264Encoder;

namespace Network {
class WebSocketService;
}

namespace Server {
class PeerAdvertisement;
} // namespace Server

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

    void setupWebSocketService();

    void updateAnimations();

    _lv_display_t* display = nullptr;
    EventProcessor eventProcessor;

    std::unique_ptr<Network::WebSocketService> wsService_;

    std::unique_ptr<UiComponentManager> uiManager_;
    std::unique_ptr<RemoteInputDevice> remoteInputDevice_;

    std::unique_ptr<WebRtcStreamer> webRtcStreamer_;

    Network::WebSocketService& getWebSocketService();

    UiComponentManager* getUiComponentManager() { return uiManager_.get(); }

    RemoteInputDevice* getRemoteInputDevice() { return remoteInputDevice_.get(); }

    WebRtcStreamer* getWebRtcStreamer() { return webRtcStreamer_.get(); }

    Timers& getTimers() { return timers_; }

private:
    SystemMetrics systemMetrics_;
    Timers timers_;
    State::Any fsmState{ State::Startup{} };
    std::unique_ptr<H264Encoder> h264Encoder_;
    std::unique_ptr<Server::PeerAdvertisement> peerAd_;

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
