#pragma once

#include "core/Result.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/SystemMetrics.h"
#include "core/network/WebSocketService.h"
#include "os-manager/Event.h"
#include "os-manager/EventProcessor.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include "os-manager/states/State.h"
#include "server/api/ApiError.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>

namespace DirtSim {
namespace OsManager {

class LocalProcessBackend;

class OperatingSystemManager : public StateMachineBase, public StateMachineInterface<Event> {
public:
    enum class BackendType {
        Systemd,
        LocalProcess,
    };

    struct BackendConfig {
        BackendType type = BackendType::Systemd;
        std::string audioPath;
        std::string audioArgs;
        std::string serverPath;
        std::string serverArgs;
        std::string uiPath;
        std::string uiArgs;
        std::string uiBackend;
        std::string uiDisplay;
        std::string workDir;

        static BackendConfig fromEnvironment();
    };

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
    explicit OperatingSystemManager(uint16_t port, const BackendConfig& backendConfig);
    explicit OperatingSystemManager(TestMode mode);
    ~OperatingSystemManager();

    Result<std::monostate, std::string> start();
    void stop();

    void mainLoopRun();

    void requestExit();

    void queueEvent(const Event& event) override;
    void handleEvent(const Event& event);

    std::string getCurrentStateName() const override;
    void processEvents() override;

    OsApi::SystemStatus::Okay buildSystemStatus();
    Result<OsApi::WebSocketAccessSet::Okay, ApiError> setWebSocketAccess(bool enabled);
    Result<OsApi::WebUiAccessSet::Okay, ApiError> setWebUiAccess(bool enabled);
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
    std::string getAudioHealth(int timeoutMs);
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
    BackendConfig backendConfig_;
    std::unique_ptr<LocalProcessBackend> localBackend_;
    bool webUiEnabled_ = false;
    bool webSocketEnabled_ = false;
    std::string webSocketToken_;
};

} // namespace OsManager
} // namespace DirtSim
