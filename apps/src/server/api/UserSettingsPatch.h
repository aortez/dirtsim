#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/UserSettings.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace UserSettingsPatch {

DEFINE_API_NAME(UserSettingsPatch);

struct Okay;

struct Command {
    std::optional<Config::Clock> clockScenarioConfig = std::nullopt;
    std::optional<int> volumePercent = std::nullopt;
    std::optional<Scenario::EnumType> defaultScenario = std::nullopt;
    std::optional<StartMenuIdleAction> startMenuIdleAction = std::nullopt;
    std::optional<int> startMenuIdleTimeoutMs = std::nullopt;
    std::optional<TrainingSpec> trainingSpec = std::nullopt;
    std::optional<EvolutionConfig> evolutionConfig = std::nullopt;
    std::optional<MutationConfig> mutationConfig = std::nullopt;
    std::optional<TrainingResumePolicy> trainingResumePolicy = std::nullopt;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    bool isEmpty() const
    {
        return !clockScenarioConfig.has_value() && !volumePercent.has_value()
            && !defaultScenario.has_value() && !startMenuIdleAction.has_value()
            && !startMenuIdleTimeoutMs.has_value() && !trainingSpec.has_value()
            && !evolutionConfig.has_value() && !mutationConfig.has_value()
            && !trainingResumePolicy.has_value();
    }

    using serialize = zpp::bits::members<9>;
};

struct Okay {
    UserSettings settings;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace UserSettingsPatch
} // namespace Api
} // namespace DirtSim
