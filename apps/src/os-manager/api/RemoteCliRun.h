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
namespace OsApi {
namespace RemoteCliRun {

DEFINE_API_NAME(RemoteCliRun);

struct Okay;

struct Command {
    std::string host;
    std::vector<std::string> args;
    std::optional<int> timeout_ms;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    int exit_code = 0;
    std::string stdout;
    std::string stderr;
    int elapsed_ms = 0;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<4>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace RemoteCliRun
} // namespace OsApi
} // namespace DirtSim
