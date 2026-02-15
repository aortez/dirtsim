#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingResumePolicy.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include <nlohmann/json_fwd.hpp>
#include <zpp_bits.h>

namespace DirtSim {

enum class StartMenuIdleAction : uint8_t {
    ClockScenario = 0,
    None,
    TrainingSession,
};

struct UserSettings {
    int timezoneIndex = 2;
    int volumePercent = 20;
    Scenario::EnumType defaultScenario = Scenario::EnumType::Sandbox;
    StartMenuIdleAction startMenuIdleAction = StartMenuIdleAction::ClockScenario;
    bool startMenuAutoRun = false;
    TrainingSpec trainingSpec;
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    TrainingResumePolicy trainingResumePolicy = TrainingResumePolicy::WarmFromBest;

    using serialize = zpp::bits::members<9>;
};

void from_json(const nlohmann::json& j, UserSettings& settings);
void to_json(nlohmann::json& j, const UserSettings& settings);

} // namespace DirtSim
