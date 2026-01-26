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

namespace EventSubscribe {

DEFINE_API_NAME(EventSubscribe);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    bool enabled = true;
    std::string connectionId;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool subscribed = true;
    std::string message;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace EventSubscribe
} // namespace Api
} // namespace DirtSim
