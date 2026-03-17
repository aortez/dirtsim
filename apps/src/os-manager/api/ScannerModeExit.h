#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace ScannerModeExit {

DEFINE_API_NAME(ScannerModeExit);

struct Okay;

struct Command {
    API_COMMAND_T(std::monostate);
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    bool active = false;
    std::string detail;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace ScannerModeExit
} // namespace OsApi
} // namespace DirtSim
