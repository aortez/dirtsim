#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace WifiForget {

DEFINE_API_NAME(WifiForget);

struct Okay;

struct Command {
    std::string ssid;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool success = true;
    std::string ssid;
    int removed = 0;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<3>;
};

API_STANDARD_TYPES();

} // namespace WifiForget
} // namespace OsApi
} // namespace DirtSim
