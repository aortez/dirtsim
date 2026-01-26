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

namespace TrainingStreamConfigSet {

DEFINE_API_NAME(TrainingStreamConfigSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    int intervalMs = 0;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    int intervalMs = 0;
    std::string message;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrainingStreamConfigSet
} // namespace Api
} // namespace DirtSim
