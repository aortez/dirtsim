#pragma once

#include "core/scenarios/nes/NesFitnessDetails.h"

#include <cstdint>

namespace DirtSim {

enum class SmbPhase : uint8_t {
    NonGameplay = 0,
    Gameplay = 1,
};

enum class SmbLifeState : uint8_t {
    Alive = 0,
    Dying = 1,
    Dead = 2,
};

enum class SmbPowerupState : uint8_t {
    Small = 0,
    Big = 1,
    Fire = 2,
};

struct NesSuperMarioBrosState {
    SmbPhase phase = SmbPhase::NonGameplay;
    SmbLifeState lifeState = SmbLifeState::Alive;
    SmbPowerupState powerupState = SmbPowerupState::Small;
    bool airborne = false;
    double horizontalSpeedNormalized = 0.0;
    double verticalSpeedNormalized = 0.0;
    uint8_t world = 0;
    uint8_t level = 0;
    uint16_t absoluteX = 0;
    uint8_t playerXScreen = 0;
    uint8_t playerYScreen = 0;
    uint8_t lives = 0;
};

struct NesSuperMarioBrosEvaluatorInput {
    uint64_t advancedFrames = 0;
    NesSuperMarioBrosState state;
};

struct NesSuperMarioBrosEvaluatorOutput {
    bool done = false;
    double rewardDelta = 0.0;
    double distanceRewardDelta = 0.0;
    double levelClearRewardDelta = 0.0;
    SmbEpisodeEndReason endReason = SmbEpisodeEndReason::None;
    NesSuperMarioBrosFitnessSnapshot snapshot;
};

class NesSuperMarioBrosEvaluator {
public:
    void reset();
    NesSuperMarioBrosEvaluatorOutput evaluate(const NesSuperMarioBrosEvaluatorInput& input);

private:
    static constexpr double kDistanceReward = 0.5;
    static constexpr double kLevelClearReward = 1000.0;
    static constexpr uint64_t kNoProgressTimeoutFrames = 1800;

    bool hasBestProgress_ = false;
    bool hasLastLives_ = false;
    uint64_t gameplayFrameCount_ = 0;
    uint64_t lastProgressFrame_ = 0;
    uint32_t bestStageIndex_ = 0;
    uint16_t bestAbsoluteX_ = 0;
    uint8_t lastLives_ = 0;
    double distanceRewardTotal_ = 0.0;
    double levelClearRewardTotal_ = 0.0;
};

} // namespace DirtSim
