#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {
namespace NetworkDiagnosticsGet {

DEFINE_API_NAME(NetworkDiagnosticsGet);

struct ConnectProgressInfo {
    std::string phase;
    std::string ssid;
    bool can_cancel = true;

    using serialize = zpp::bits::members<3>;
};

inline void to_json(nlohmann::json& j, const ConnectProgressInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, ConnectProgressInfo& info)
{
    info = ReflectSerializer::from_json<ConnectProgressInfo>(j);
}

struct NetworkInfo {
    std::string ssid;
    std::string status;
    bool requires_password = false;

    using serialize = zpp::bits::members<3>;
};

inline void to_json(nlohmann::json& j, const NetworkInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, NetworkInfo& info)
{
    info = ReflectSerializer::from_json<NetworkInfo>(j);
}

struct Okay;

struct Command {
    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    bool connect_cancel_enabled = false;
    bool connect_cancel_visible = false;
    bool connect_overlay_visible = false;
    bool password_prompt_visible = false;
    bool password_submit_enabled = false;
    bool scanner_enter_enabled = false;
    bool scanner_exit_enabled = false;
    bool scanner_mode_active = false;
    bool scanner_mode_available = false;
    std::optional<ConnectProgressInfo> connect_progress;
    std::optional<std::string> connected_ssid;
    std::optional<std::string> connect_target_ssid;
    std::optional<std::string> password_prompt_target_ssid;
    std::string password_error;
    std::string scanner_status_message;
    std::vector<NetworkInfo> networks;
    std::string screen;
    std::string view_mode;
    std::string wifi_status_message;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<19>;
};

API_STANDARD_TYPES();

} // namespace NetworkDiagnosticsGet
} // namespace UiApi
} // namespace DirtSim
