#pragma once

#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingSpec.h"

namespace DirtSim::Ui {

struct UserSettings {
    TrainingSpec trainingSpec;
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    int streamIntervalMs = 16;
};

} // namespace DirtSim::Ui
