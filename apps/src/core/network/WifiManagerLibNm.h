#pragma once

#include "core/Result.h"
#include "core/network/WifiManager.h"

#include <NetworkManager.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim {
namespace Network {
namespace Internal {

struct GObjectDeleter {
    void operator()(gpointer object) const;
};

template <typename T>
using GObjectPtr = std::unique_ptr<T, GObjectDeleter>;

struct WifiListOptions {
    bool forceScan = false;
    GMainContext* mainContext = nullptr;
};

GObjectPtr<NMClient> createClient(std::string& errorMessage, GMainContext* mainContext = nullptr);

NMDeviceWifi* findWifiDevice(NMClient* client);

Result<WifiStatus, std::string> getStatus(NMClient* client);

Result<std::vector<WifiNetworkInfo>, std::string> listNetworks(
    NMClient* client, const WifiListOptions& options = {});

Result<std::vector<WifiAccessPointInfo>, std::string> listAccessPoints(
    NMClient* client, const WifiListOptions& options = {});

Result<WifiConnectResult, std::string> connect(
    NMClient* client, const WifiNetworkInfo& network, GMainContext* mainContext = nullptr);

Result<WifiConnectResult, std::string> connectBySsid(
    NMClient* client,
    const std::string& ssid,
    const std::optional<std::string>& password = std::nullopt,
    GMainContext* mainContext = nullptr);

Result<WifiDisconnectResult, std::string> disconnect(
    NMClient* client,
    const std::optional<std::string>& ssid = std::nullopt,
    GMainContext* mainContext = nullptr);

Result<WifiForgetResult, std::string> forget(
    NMClient* client, const std::string& ssid, GMainContext* mainContext = nullptr);

} // namespace Internal
} // namespace Network
} // namespace DirtSim
