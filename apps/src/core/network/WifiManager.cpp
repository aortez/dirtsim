#include "WifiManager.h"

#include "core/network/WifiManagerLibNm.h"

namespace DirtSim {
namespace Network {

Result<WifiStatus, std::string> WifiManager::getStatus() const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<WifiStatus, std::string>::error(errorMessage);
    }

    return Internal::getStatus(client.get());
}

Result<std::vector<WifiNetworkInfo>, std::string> WifiManager::listNetworks() const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error(errorMessage);
    }

    return Internal::listNetworks(client.get(), Internal::WifiListOptions{ .forceScan = true });
}

Result<std::vector<WifiAccessPointInfo>, std::string> WifiManager::listAccessPoints() const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error(errorMessage);
    }

    return Internal::listAccessPoints(client.get(), Internal::WifiListOptions{ .forceScan = true });
}

Result<WifiConnectResult, std::string> WifiManager::connect(const WifiNetworkInfo& network) const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<WifiConnectResult, std::string>::error(errorMessage);
    }

    return Internal::connect(client.get(), network);
}

Result<WifiConnectResult, std::string> WifiManager::connectBySsid(
    const std::string& ssid, const std::optional<std::string>& password) const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<WifiConnectResult, std::string>::error(errorMessage);
    }

    return Internal::connectBySsid(client.get(), ssid, password);
}

Result<WifiDisconnectResult, std::string> WifiManager::disconnect(
    const std::optional<std::string>& ssid) const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<WifiDisconnectResult, std::string>::error(errorMessage);
    }

    return Internal::disconnect(client.get(), ssid);
}

Result<WifiForgetResult, std::string> WifiManager::forget(const std::string& ssid) const
{
    std::string errorMessage;
    auto client = Internal::createClient(errorMessage);
    if (!client) {
        return Result<WifiForgetResult, std::string>::error(errorMessage);
    }

    return Internal::forget(client.get(), ssid);
}

} // namespace Network
} // namespace DirtSim
