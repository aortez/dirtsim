#pragma once

#include "core/scenarios/nes/NesFitnessDetails.h"

#include <cstdint>

namespace DirtSim {

enum class SmbPhase : uint8_t {
    NonGameplay = 0,
    Gameplay = 1,
};

enum class SmbGameMode : uint8_t {
    StartDemo = 0x00,
    Normal = 0x01,
    EndCurrentWorld = 0x02,
    EndGame = 0x03,
    Unknown = 0xFF,
};

enum class SmbLifeState : uint8_t {
    Alive = 0,
    Dying = 1,
    Dead = 2,
};

enum class SmbPlayerState : uint8_t {
    LeftmostOfScreen = 0x00,
    ClimbingVine = 0x01,
    EnteringReversedLPipe = 0x02,
    GoingDownPipe = 0x03,
    Autowalk = 0x04,
    AutowalkAlt = 0x05,
    PlayerDies = 0x06,
    EnteringArea = 0x07,
    Normal = 0x08,
    Growing = 0x09,
    Shrinking = 0x0A,
    Dying = 0x0B,
    BecomingFire = 0x0C,
    Unknown = 0xFF,
};

enum class SmbFloatState : uint8_t {
    GroundedOrOther = 0x00,
    Jumping = 0x01,
    WalkedOffLedge = 0x02,
    SlidingFlagpole = 0x03,
    Unknown = 0xFF,
};

enum class SmbPowerupState : uint8_t {
    Small = 0,
    Big = 1,
    Fire = 2,
};

struct NesSuperMarioBrosState {
    SmbPhase phase = SmbPhase::NonGameplay;
    SmbGameMode gameMode = SmbGameMode::Unknown;
    SmbLifeState lifeState = SmbLifeState::Alive;
    SmbPlayerState playerState = SmbPlayerState::Unknown;
    SmbFloatState floatState = SmbFloatState::Unknown;
    SmbPowerupState powerupState = SmbPowerupState::Small;
    bool airborne = false;
    bool enemyPresent = false;
    bool secondEnemyPresent = false;
    float facingX = 0.0f;
    float movementX = 0.0f;
    double horizontalSpeedNormalized = 0.0;
    double verticalSpeedNormalized = 0.0;
    int16_t nearestEnemyDx = 0;
    int16_t nearestEnemyDy = 0;
    int16_t secondNearestEnemyDx = 0;
    int16_t secondNearestEnemyDy = 0;
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
    void restoreProgress(
        uint32_t bestStageIndex,
        uint16_t bestAbsoluteX,
        double distanceRewardTotal,
        double levelClearRewardTotal,
        uint64_t gameplayFrames,
        uint64_t gameplayFramesSinceProgress);
    NesSuperMarioBrosEvaluatorOutput evaluate(const NesSuperMarioBrosEvaluatorInput& input);

private:
    static constexpr uint8_t kBelowScreenTerminalPlayerYScreenThreshold = 224u;
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
