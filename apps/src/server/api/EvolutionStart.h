#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"
#include "core/organisms/evolution/EvolutionConfig.h"

#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace EvolutionStart {

DEFINE_API_NAME(EvolutionStart);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    EvolutionConfig evolution;
    MutationConfig mutation;
    Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    bool started = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace EvolutionStart
} // namespace Api
} // namespace DirtSim
