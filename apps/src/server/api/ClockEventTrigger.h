#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/scenarios/clock_scenario/ClockEventTypes.h"
#include <nlohmann/json.hpp>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace ClockEventTrigger {

DEFINE_API_NAME(ClockEventTrigger);

struct Command {
    API_COMMAND_T(std::monostate);
    ClockEventType event_type = ClockEventType::RAIN;

    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace ClockEventTrigger
} // namespace Api
} // namespace DirtSim
