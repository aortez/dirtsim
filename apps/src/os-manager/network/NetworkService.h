#pragma once

#include "core/Pimpl.h"
#include "core/Result.h"
#include "core/network/WifiManager.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace DirtSim {
namespace OsManager {

class NetworkService {
public:
    struct LocalAddressInfo {
        std::string name;
        std::string address;
    };

    struct Snapshot {
        Network::WifiStatus status;
        std::vector<Network::WifiNetworkInfo> networks;
        std::vector<Network::WifiAccessPointInfo> accessPoints;
        std::optional<std::string> activeBssid;
        std::vector<LocalAddressInfo> localAddresses;
        std::optional<Network::WifiConnectOutcome> connectOutcome;
        std::optional<Network::WifiConnectProgress> connectProgress;
        std::optional<uint64_t> lastScanAgeMs;
        bool scanInProgress = false;
    };

    using SnapshotChangedCallback = std::function<void(const Snapshot&)>;

    NetworkService();
    ~NetworkService();

    NetworkService(const NetworkService&) = delete;
    NetworkService& operator=(const NetworkService&) = delete;

    Result<std::monostate, std::string> start();
    void stop();

    Result<Snapshot, std::string> getSnapshot(bool forceRefresh);
    Result<std::monostate, std::string> cancelConnect();
    Result<Network::WifiConnectResult, std::string> connectBySsid(
        const std::string& ssid, const std::optional<std::string>& password);
    Result<Network::WifiDisconnectResult, std::string> disconnect(
        const std::optional<std::string>& ssid);
    Result<Network::WifiForgetResult, std::string> forget(const std::string& ssid);
    Result<std::monostate, std::string> requestScan();
    void setSnapshotChangedCallback(SnapshotChangedCallback callback);

private:
    struct Impl;
    Pimpl<Impl> pImpl_;
};

} // namespace OsManager
} // namespace DirtSim
