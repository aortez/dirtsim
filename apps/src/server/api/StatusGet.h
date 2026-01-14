#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace StatusGet {

DEFINE_API_NAME(StatusGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    // zpp_bits serialization (command has no fields).
    using serialize = zpp::bits::members<0>;
};

struct Okay {
    std::string state;         // Current state machine state (always present).
    std::string error_message; // Populated when state is "Error".
    int32_t timestep = 0;
    std::optional<Scenario::EnumType> scenario_id; // Present when simulation is running.
    int16_t width = 0;
    int16_t height = 0;

    // System health metrics.
    double cpu_percent = 0.0;
    double memory_percent = 0.0;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    // zpp_bits serialization.
    using serialize = zpp::bits::members<8>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace StatusGet
} // namespace Api
} // namespace DirtSim
