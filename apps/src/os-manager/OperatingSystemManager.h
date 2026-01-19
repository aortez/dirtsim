#pragma once

#include "core/Result.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/SystemMetrics.h"
#include "core/network/WebSocketService.h"
#include "os-manager/Event.h"
#include "os-manager/EventProcessor.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/states/State.h"
#include "server/api/ApiError.h"
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace DirtSim {
namespace OsManager {

class OperatingSystemManager : public StateMachineBase, public StateMachineInterface<Event> {
public:
    struct Dependencies {
        std::function<Result<std::monostate, ApiError>(const std::string&, const std::string&)>
            serviceCommand;
        std::function<int(const std::string&)> systemCommand;
        std::function<OsApi::SystemStatus::Okay()> systemStatus;
        std::function<void()> reboot;
    };

    struct TestMode {
        Dependencies dependencies;
    };

    explicit OperatingSystemManager(uint16_t port);
    explicit OperatingSystemManager(TestMode mode);

    Result<std::monostate, std::string> start();
    void stop();

    void mainLoopRun();

    void requestExit();

    void queueEvent(const Event& event) override;
    void handleEvent(const Event& event);

    std::string getCurrentStateName() const override;
    void processEvents() override;

    OsApi::SystemStatus::Okay buildSystemStatus();
    Result<std::monostate, ApiError> startService(const std::string& unitName);
    Result<std::monostate, ApiError> stopService(const std::string& unitName);
    Result<std::monostate, ApiError> restartService(const std::string& unitName);
    void scheduleReboot();

private:
    friend struct OperatingSystemManagerTestAccessor;

    struct DiskStats {
        uint64_t free_bytes = 0;
        uint64_t total_bytes = 0;
    };

    void setupWebSocketService();
    OsApi::SystemStatus::Okay buildSystemStatusInternal();
    DiskStats getDiskStats(const std::string& path) const;
    std::string getServerHealth(int timeoutMs);
    std::string getUiHealth(int timeoutMs);
    Result<std::monostate, ApiError> runServiceCommand(
        const std::string& action, const std::string& unitName);
    void scheduleRebootInternal();
    void transitionTo(State::Any newState);
    void initializeDefaultDependencies();

    uint16_t port_ = 0;
    bool enableNetworking_ = true;
    EventProcessor eventProcessor_;
    State::Any fsmState_{ State::Startup{} };
    SystemMetrics systemMetrics_;
    Network::WebSocketService wsService_;
    Dependencies dependencies_;
};

} // namespace OsManager
} // namespace DirtSim
