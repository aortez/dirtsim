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
    bool autoConnect = false;
    bool hasCredentials = false;
    std::optional<std::string> lastUsedDate;
    std::string lastUsedRelative;
    std::string connectionId;
};

struct WifiAccessPointInfo {
    std::string ssid;
    std::string bssid;
    std::optional<int> signalDbm;
    std::optional<int> frequencyMhz;
    std::optional<int> channel;
    std::string security;
    bool active = false;
};

struct WifiStatus {
    bool connected = false;
    std::string ssid;
};

struct WifiConnectResult {
    bool success = true;
    std::string ssid;
};

struct WifiDisconnectResult {
    bool success = true;
    std::string ssid;
};

struct WifiForgetResult {
    bool success = true;
    std::string ssid;
    int removed = 0;
};

class WifiManager {
public:
    Result<WifiStatus, std::string> getStatus() const;
    Result<std::vector<WifiNetworkInfo>, std::string> listNetworks() const;
    Result<std::vector<WifiAccessPointInfo>, std::string> listAccessPoints() const;
    Result<WifiConnectResult, std::string> connect(const WifiNetworkInfo& network) const;
    Result<WifiConnectResult, std::string> connectBySsid(
        const std::string& ssid, const std::optional<std::string>& password = std::nullopt) const;
    Result<WifiDisconnectResult, std::string> disconnect(
        const std::optional<std::string>& ssid = std::nullopt) const;
    Result<WifiForgetResult, std::string> forget(const std::string& ssid) const;
};

} // namespace Network
} // namespace DirtSim
