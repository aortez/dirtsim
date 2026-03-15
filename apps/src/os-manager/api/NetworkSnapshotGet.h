#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace NetworkSnapshotGet {

DEFINE_API_NAME(NetworkSnapshotGet);

enum class WifiNetworkStatus { Connected = 0, Saved, Open, Available };

inline void to_json(nlohmann::json& j, const WifiNetworkStatus& status)
{
    switch (status) {
        case WifiNetworkStatus::Connected:
            j = "Connected";
            return;
        case WifiNetworkStatus::Saved:
            j = "Saved";
            return;
        case WifiNetworkStatus::Open:
            j = "Open";
            return;
        case WifiNetworkStatus::Available:
            j = "Available";
            return;
    }

    throw std::runtime_error("Unhandled WifiNetworkStatus");
}

inline void from_json(const nlohmann::json& j, WifiNetworkStatus& status)
{
    const std::string text = j.get<std::string>();
    if (text == "Connected") {
        status = WifiNetworkStatus::Connected;
        return;
    }
    if (text == "Saved") {
        status = WifiNetworkStatus::Saved;
        return;
    }
    if (text == "Open") {
        status = WifiNetworkStatus::Open;
        return;
    }
    if (text == "Available") {
        status = WifiNetworkStatus::Available;
        return;
    }

    throw std::runtime_error("Invalid WifiNetworkStatus: " + text);
}

struct LocalAddressInfo {
    std::string name;
    std::string address;

    using serialize = zpp::bits::members<2>;
};

inline void to_json(nlohmann::json& j, const LocalAddressInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, LocalAddressInfo& info)
{
    info = ReflectSerializer::from_json<LocalAddressInfo>(j);
}

struct WifiStatusInfo {
    bool connected = false;
    std::string ssid;

    using serialize = zpp::bits::members<2>;
};

inline void to_json(nlohmann::json& j, const WifiStatusInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, WifiStatusInfo& info)
{
    info = ReflectSerializer::from_json<WifiStatusInfo>(j);
}

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

    using serialize = zpp::bits::members<9>;
};

inline void to_json(nlohmann::json& j, const WifiNetworkInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, WifiNetworkInfo& info)
{
    info = ReflectSerializer::from_json<WifiNetworkInfo>(j);
}

struct Okay;

struct Command {
    bool forceRefresh = false;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    WifiStatusInfo status;
    std::vector<WifiNetworkInfo> networks;
    std::vector<LocalAddressInfo> localAddresses;
    bool scanInProgress = false;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<4>;
};

API_STANDARD_TYPES();

} // namespace NetworkSnapshotGet
} // namespace OsApi
} // namespace DirtSim
