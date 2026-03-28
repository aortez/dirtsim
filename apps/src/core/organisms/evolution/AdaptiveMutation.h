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

enum class AdaptiveMutationControlMode : uint8_t {
    Auto = 0,
    Baseline,
    Explore,
    Rescue,
};

struct EffectiveAdaptiveMutation {
    AdaptiveMutationMode mode = AdaptiveMutationMode::Baseline;
    MutationConfig mutationConfig{};
};

EffectiveAdaptiveMutation adaptiveMutationResolve(
    const MutationConfig& baselineConfig,
    const TrainingPhaseStatus& trainingPhaseStatus,
    const EffectiveAdaptiveMutation& previousEffective,
    const EvolutionConfig& evolutionConfig,
    AdaptiveMutationControlMode controlMode = AdaptiveMutationControlMode::Auto);

} // namespace DirtSim
