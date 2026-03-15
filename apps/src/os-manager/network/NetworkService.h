#pragma once

#include "core/Pimpl.h"
#include "core/Result.h"
#include "core/network/WifiManager.h"
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
        std::vector<LocalAddressInfo> localAddresses;
    };

    NetworkService();
    ~NetworkService();

    NetworkService(const NetworkService&) = delete;
    NetworkService& operator=(const NetworkService&) = delete;

    Result<std::monostate, std::string> start();
    void stop();

    Result<Snapshot, std::string> getSnapshot(bool forceRefresh);
    Result<Network::WifiConnectResult, std::string> connectBySsid(
        const std::string& ssid, const std::optional<std::string>& password);
    Result<Network::WifiDisconnectResult, std::string> disconnect(
        const std::optional<std::string>& ssid);
    Result<Network::WifiForgetResult, std::string> forget(const std::string& ssid);

private:
    struct Impl;
    Pimpl<Impl> pImpl_;
};

} // namespace OsManager
} // namespace DirtSim
