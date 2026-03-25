#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/AdaptiveMutation.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace EvolutionMutationControlsSet {

DEFINE_API_NAME(EvolutionMutationControlsSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    MutationConfig mutationConfig{};
    int stagnationWindowGenerations = 5;
    int recoveryWindowGenerations = 3;
    AdaptiveMutationControlMode controlMode = AdaptiveMutationControlMode::Auto;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<4>;
};

struct Okay {
    MutationConfig mutationConfig{};
    int stagnationWindowGenerations = 5;
    int recoveryWindowGenerations = 3;
    AdaptiveMutationControlMode controlMode = AdaptiveMutationControlMode::Auto;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<4>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace EvolutionMutationControlsSet
} // namespace Api
} // namespace DirtSim
