#pragma once

#include "core/scenarios/nes/NesFitnessDetails.h"

namespace DirtSim {

struct FitnessContext;

struct NesSuperMarioBrosFitnessBreakdown {
    double totalFitness = 0.0;
    double distanceRewardTotal = 0.0;
    double levelClearRewardTotal = 0.0;
    uint64_t gameplayFrames = 0;
    uint64_t framesSinceProgress = 0;
    uint64_t noProgressTimeoutFrames = 0;
    uint32_t bestStageIndex = 0;
    uint8_t bestWorld = 0;
    uint8_t bestLevel = 0;
    uint16_t bestAbsoluteX = 0;
    uint8_t currentWorld = 0;
    uint8_t currentLevel = 0;
    uint16_t currentAbsoluteX = 0;
    uint8_t currentLives = 0;
    SmbEpisodeEndReason endReason = SmbEpisodeEndReason::None;
    bool done = false;
};

class NesEvaluator {
public:
    static double evaluate(const FitnessContext& context);
    static double evaluateFromRewardTotal(double rewardTotal);
    static NesSuperMarioBrosFitnessBreakdown evaluateSuperMarioBrosWithBreakdown(
        const FitnessContext& context);
};

} // namespace DirtSim
