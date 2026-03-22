#pragma once

#include "EvolutionConfig.h"
#include "TrainingPhaseTracker.h"

#include <cstdint>

namespace DirtSim {

enum class AdaptiveMutationMode : uint8_t {
    Baseline = 0,
    Explore,
    Rescue,
    Recover,
};

struct EffectiveAdaptiveMutation {
    AdaptiveMutationMode mode = AdaptiveMutationMode::Baseline;
    MutationConfig mutationConfig{};
};

EffectiveAdaptiveMutation adaptiveMutationResolve(
    const MutationConfig& baselineConfig,
    const TrainingPhaseStatus& trainingPhaseStatus,
    const EffectiveAdaptiveMutation& previousEffective,
    const EvolutionConfig& evolutionConfig);

} // namespace DirtSim
