#pragma once

#include "core/Result.h"
#include "core/StateMachineBase.h"
#include "core/StateMachineInterface.h"
#include "core/SystemMetrics.h"
#include "core/network/WebSocketService.h"
#include "os-manager/Event.h"
#include "os-manager/EventProcessor.h"
#include "os-manager/PeerTrust.h"
#include "os-manager/api/PeerClientKeyEnsure.h"
#include "os-manager/api/RemoteCliRun.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/TrustBundleGet.h"
#include "os-manager/api/TrustPeer.h"
#include "os-manager/api/UntrustPeer.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include "os-manager/states/State.h"
#include "server/api/ApiError.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace DirtSim {
namespace OsManager {

class LocalProcessBackend;
class PeerAdvertisement;
class PeerDiscoveryInterface;
struct PeerInfo;

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
        std::function<Result<std::string, ApiError>(const std::string&)> commandRunner;
        std::function<std::filesystem::path(const std::string&)> homeDirResolver;
        std::function<Result<std::monostate, ApiError>(
            const std::filesystem::path&, const std::filesystem::path&, const std::string&)>
            sshPermissionsEnsurer;
        std::function<Result<OsApi::RemoteCliRun::Okay, ApiError>(
            const PeerTrustBundle&, const std::vector<std::string>&, int)>
            remoteCliRunner;
    };

    struct TestMode {
        Dependencies dependencies;
        BackendConfig backendConfig;
        bool hasBackendConfig = false;
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
    std::vector<PeerInfo> getPeers() const;
    Result<OsApi::PeerClientKeyEnsure::Okay, ApiError> ensurePeerClientKey();
    Result<OsApi::RemoteCliRun::Okay, ApiError> remoteCliRun(
        const OsApi::RemoteCliRun::Command& command);
    Result<OsApi::TrustBundleGet::Okay, ApiError> getTrustBundle();
    Result<OsApi::TrustPeer::Okay, ApiError> trustPeer(const OsApi::TrustPeer::Command& command);
    Result<OsApi::UntrustPeer::Okay, ApiError> untrustPeer(
        const OsApi::UntrustPeer::Command& command);
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
    std::filesystem::path getPeerAllowlistPath() const;
    std::filesystem::path getPeerClientKeyPath() const;
    Result<std::vector<PeerTrustBundle>, ApiError> loadPeerAllowlist() const;
    Result<std::monostate, ApiError> savePeerAllowlist(
        const std::vector<PeerTrustBundle>& allowlist) const;
    Result<std::string, ApiError> getHostFingerprintSha256() const;
    Result<std::string, ApiError> getClientKeyFingerprintSha256() const;
    Result<std::string, ApiError> getPeerClientPublicKey(bool* created);
    Result<PeerTrustBundle, ApiError> buildTrustBundle(bool* created);
    Result<std::string, ApiError> runCommandCapture(const std::string& command) const;
    std::filesystem::path getSshUserHomeDir(const std::string& user) const;
    Result<std::monostate, ApiError> applySshPermissions(
        const std::filesystem::path& dirPath,
        const std::filesystem::path& filePath,
        const std::string& user) const;
    std::pair<uint16_t, uint16_t> computePeerAdvertisementPorts() const;
    void setPeerAdvertisementEnabled(bool enabled);
    void scheduleRebootInternal();
    void transitionTo(State::Any newState);
    void initializeDefaultDependencies();
    void initializePeerDiscovery();

    uint16_t port_ = 0;
    bool enableNetworking_ = true;
    EventProcessor eventProcessor_;
    State::Any fsmState_{ State::Startup{} };
    SystemMetrics systemMetrics_;
    Network::WebSocketService wsService_;
    Dependencies dependencies_;
    BackendConfig backendConfig_;
    std::unique_ptr<LocalProcessBackend> localBackend_;
    std::unique_ptr<PeerAdvertisement> serverPeerAdvertisement_;
    std::unique_ptr<PeerAdvertisement> uiPeerAdvertisement_;
    std::unique_ptr<PeerDiscoveryInterface> peerDiscovery_;
    std::string peerServiceName_;
    std::string peerUiServiceName_;
    bool webUiEnabled_ = false;
    bool webSocketEnabled_ = false;
    std::string webSocketToken_;
};

} // namespace OsManager
} // namespace DirtSim
