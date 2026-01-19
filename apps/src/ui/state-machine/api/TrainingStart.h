#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>

namespace DirtSim {
namespace UiApi {

namespace TrainingStart {

DEFINE_API_NAME(TrainingStart);

struct Command {
    EvolutionConfig evolution;
    MutationConfig mutation;
    TrainingSpec training;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

struct Okay {
    bool queued = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrainingStart
} // namespace UiApi
} // namespace DirtSim
