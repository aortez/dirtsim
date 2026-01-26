#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace RenderStreamConfigSet {

DEFINE_API_NAME(RenderStreamConfigSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    bool renderEnabled = true;
    int renderEveryN = 1;
    std::string connectionId;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    bool renderEnabled = true;
    int renderEveryN = 1;
    std::string message;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<3>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace RenderStreamConfigSet
} // namespace Api
} // namespace DirtSim
