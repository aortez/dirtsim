#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"

#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace ScenarioSwitch {

DEFINE_API_NAME(ScenarioSwitch);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    Scenario::EnumType scenario_id;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool success;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace ScenarioSwitch
} // namespace Api
} // namespace DirtSim
