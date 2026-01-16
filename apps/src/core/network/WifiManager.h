#pragma once

#include "core/Result.h"
#include <optional>
#include <string>
#include <vector>

namespace DirtSim {
namespace Network {

enum class WifiNetworkStatus { Connected = 0, Saved, Open };

struct WifiNetworkInfo {
    std::string ssid;
    WifiNetworkStatus status = WifiNetworkStatus::Saved;
    std::optional<int> signalDbm;
    std::string security;
    std::optional<std::string> lastUsedDate;
    std::string lastUsedRelative;
    std::string connectionId;
};

struct WifiStatus {
    bool connected = false;
    std::string ssid;
};

struct WifiConnectResult {
    bool success = true;
    std::string ssid;
};

class WifiManager {
public:
    Result<WifiStatus, std::string> getStatus() const;
    Result<std::vector<WifiNetworkInfo>, std::string> listNetworks() const;
    Result<WifiConnectResult, std::string> connect(const WifiNetworkInfo& network) const;
    Result<WifiConnectResult, std::string> connectBySsid(const std::string& ssid) const;
};

} // namespace Network
} // namespace DirtSim
