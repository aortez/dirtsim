#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"

#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultAvailableAck {

DEFINE_API_NAME(TrainingResultAvailableAck);

struct Okay;

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    bool acknowledged = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrainingResultAvailableAck
} // namespace Api
} // namespace DirtSim
