#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace WifiConnect {

DEFINE_API_NAME(WifiConnect);

struct Okay;

struct Command {
    std::string ssid;
    std::optional<std::string> password;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool success = true;
    std::string ssid;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<2>;
};

API_STANDARD_TYPES();

} // namespace WifiConnect
} // namespace OsApi
} // namespace DirtSim
