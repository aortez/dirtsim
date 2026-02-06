#pragma once

#include "Event.h"
#include "EventProcessor.h"
#include "EventSink.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/SystemMetrics.h"
#include "core/Timers.h"
#include "states/State.h"
#include "ui/UiConfig.h"
#include "ui/UserSettingsManager.h"

#include <algorithm>
#include <memory>
#include <string>

struct _lv_display_t;

namespace DirtSim {

class H264Encoder;

namespace Network {
class WebSocketService;
class WebSocketServiceInterface;
} // namespace Network

namespace Ui {
class RemoteInputDevice;
class UiComponentManager;
class WebRtcStreamer;
class FractalAnimator;
} // namespace Ui
} // namespace DirtSim

namespace DirtSim {
namespace Ui {

class StateMachine : public StateMachineBase,
                     public StateMachineInterface<Event>,
                     public EventSink {
public:
    explicit StateMachine(
        _lv_display_t* display, UserSettingsManager& userSettingsManager, uint16_t wsPort = 7070);
    ~StateMachine();

    // Test-only constructor: creates minimal StateMachine without display or networking.
    struct TestMode {};
    explicit StateMachine(TestMode, UserSettingsManager& userSettingsManager);

    void mainLoopRun();

    void queueEvent(const Event& event) override;

    void handleEvent(const Event& event);

    std::string getCurrentStateName() const override;

    void processEvents() override;

    void setupWebSocketService();

    void updateAnimations();

    _lv_display_t* display = nullptr;
    EventProcessor eventProcessor;

    std::unique_ptr<Network::WebSocketServiceInterface> wsService_;

    std::unique_ptr<UiComponentManager> uiManager_;
    std::unique_ptr<RemoteInputDevice> remoteInputDevice_;

    std::unique_ptr<WebRtcStreamer> webRtcStreamer_;
    std::unique_ptr<FractalAnimator> fractalAnimator_;

    Network::WebSocketServiceInterface& getWebSocketService();
    Network::WebSocketService* getConcreteWebSocketService();
    bool hasWebSocketService() const { return wsService_ != nullptr; }
    void setLastServerAddress(const std::string& host, uint16_t port);
    bool queueReconnectToLastServer();

    UiComponentManager* getUiComponentManager() { return uiManager_.get(); }

    RemoteInputDevice* getRemoteInputDevice() { return remoteInputDevice_.get(); }

    WebRtcStreamer* getWebRtcStreamer() { return webRtcStreamer_.get(); }

    FractalAnimator& getFractalAnimator();

    Timers& getTimers() { return timers_; }

    double getUiFps() const;

    UserSettings& getUserSettings() { return userSettingsManager_->get(); }
    const UserSettings& getUserSettings() const { return userSettingsManager_->get(); }
    int getSynthVolumePercent() const { return synthVolumePercent_; }
    void setSynthVolumePercent(int value) { synthVolumePercent_ = std::clamp(value, 0, 100); }

    // UI configuration (loaded from ui.json).
    std::unique_ptr<UiConfig> uiConfig;

    const UiConfig& getUiConfig() const
    {
        static UiConfig defaultConfig;
        return uiConfig ? *uiConfig : defaultConfig;
    }

private:
    static constexpr uint32_t AutoShrinkTimeoutMs = 10000;
    static constexpr uint32_t StartMenuIdleClockTimeoutMs = 60000;

    SystemMetrics systemMetrics_;
    Timers timers_;
    State::Any fsmState{ State::Startup{} };
    std::unique_ptr<H264Encoder> h264Encoder_;
    std::string lastServerHost_;
    uint16_t lastServerPort_ = 0;
    bool hasLastServerAddress_ = false;
    uint16_t wsPort_ = 7070;
    uint32_t lastInactiveMs_ = 0;
    UserSettingsManager* userSettingsManager_ = nullptr;
    bool startMenuIdleClockTriggered_ = false;
    int synthVolumePercent_ = 50;

    bool isAutoShrinkBlocked() const;
    void autoShrinkIfIdle();

    void transitionTo(State::Any newState);
};

} // namespace Ui
} // namespace DirtSim
