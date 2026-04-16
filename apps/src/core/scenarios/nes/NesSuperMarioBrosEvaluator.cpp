#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"

namespace DirtSim {

namespace {

constexpr uint32_t kLevelsPerWorld = 4;
constexpr uint64_t kSmbNoProgressTimeoutFrames = 1800;

uint32_t getStageIndex(const NesSuperMarioBrosState& state)
{
    return (static_cast<uint32_t>(state.world) * kLevelsPerWorld) + state.level;
}

uint8_t getStageLevel(uint32_t stageIndex)
{
    return static_cast<uint8_t>(stageIndex % kLevelsPerWorld);
}

uint8_t getStageWorld(uint32_t stageIndex)
{
    return static_cast<uint8_t>(stageIndex / kLevelsPerWorld);
}

NesSuperMarioBrosFitnessSnapshot makeSnapshot(
    const NesSuperMarioBrosState& state,
    uint64_t gameplayFrameCount,
    uint64_t lastProgressFrame,
    uint32_t bestStageIndex,
    uint16_t bestAbsoluteX,
    double distanceRewardTotal,
    double levelClearRewardTotal,
    uint64_t noProgressTimeoutFrames,
    SmbEpisodeEndReason endReason,
    bool done)
{
    return NesSuperMarioBrosFitnessSnapshot{
        .totalReward = distanceRewardTotal + levelClearRewardTotal,
        .distanceRewardTotal = distanceRewardTotal,
        .levelClearRewardTotal = levelClearRewardTotal,
        .gameplayFrames = gameplayFrameCount,
        .framesSinceProgress =
            gameplayFrameCount >= lastProgressFrame ? (gameplayFrameCount - lastProgressFrame) : 0,
        .noProgressTimeoutFrames = noProgressTimeoutFrames,
        .bestStageIndex = bestStageIndex,
        .bestWorld = getStageWorld(bestStageIndex),
        .bestLevel = getStageLevel(bestStageIndex),
        .bestAbsoluteX = bestAbsoluteX,
        .currentWorld = state.world,
        .currentLevel = state.level,
        .currentAbsoluteX = state.absoluteX,
        .currentLives = state.lives,
        .endReason = endReason,
        .done = done,
    };
}

} // namespace

void NesSuperMarioBrosEvaluator::reset()
{
    hasBestProgress_ = false;
    hasLastLives_ = false;
    gameplayFrameCount_ = 0;
    lastProgressFrame_ = 0;
    bestStageIndex_ = 0;
    bestAbsoluteX_ = 0;
    lastLives_ = 0;
    distanceRewardTotal_ = 0.0;
    levelClearRewardTotal_ = 0.0;
}

void NesSuperMarioBrosEvaluator::restoreProgress(
    uint32_t bestStageIndex,
    uint16_t bestAbsoluteX,
    double distanceRewardTotal,
    double levelClearRewardTotal,
    uint64_t gameplayFrames,
    uint64_t gameplayFramesSinceProgress)
{
    reset();
    hasBestProgress_ = true;
    gameplayFrameCount_ = gameplayFrames;
    lastProgressFrame_ = gameplayFrames >= gameplayFramesSinceProgress
        ? (gameplayFrames - gameplayFramesSinceProgress)
        : 0u;
    bestStageIndex_ = bestStageIndex;
    bestAbsoluteX_ = bestAbsoluteX;
    distanceRewardTotal_ = distanceRewardTotal;
    levelClearRewardTotal_ = levelClearRewardTotal;
}

NesSuperMarioBrosEvaluatorOutput NesSuperMarioBrosEvaluator::evaluate(
    const NesSuperMarioBrosEvaluatorInput& input)
{
    NesSuperMarioBrosEvaluatorOutput output;
    output.snapshot = makeSnapshot(
        input.state,
        gameplayFrameCount_,
        lastProgressFrame_,
        bestStageIndex_,
        bestAbsoluteX_,
        distanceRewardTotal_,
        levelClearRewardTotal_,
        kSmbNoProgressTimeoutFrames,
        SmbEpisodeEndReason::None,
        false);
    if (input.state.phase != SmbPhase::Gameplay) {
        return output;
    }

    gameplayFrameCount_ += input.advancedFrames;

    const uint32_t currentStageIndex = getStageIndex(input.state);
    if (!hasBestProgress_) {
        hasBestProgress_ = true;
        bestStageIndex_ = currentStageIndex;
        bestAbsoluteX_ = input.state.absoluteX;
        lastProgressFrame_ = gameplayFrameCount_;
    }

    if (!hasLastLives_) {
        hasLastLives_ = true;
        lastLives_ = input.state.lives;
    }
    else if (input.state.lives < lastLives_) {
        lastLives_ = input.state.lives;
        output.done = true;
        output.endReason = SmbEpisodeEndReason::LifeLost;
        output.snapshot = makeSnapshot(
            input.state,
            gameplayFrameCount_,
            lastProgressFrame_,
            bestStageIndex_,
            bestAbsoluteX_,
            distanceRewardTotal_,
            levelClearRewardTotal_,
            kSmbNoProgressTimeoutFrames,
            output.endReason,
            true);
        return output;
    }
    else {
        lastLives_ = input.state.lives;
    }

    if (input.state.lifeState == SmbLifeState::Dead && input.state.lives == 0u) {
        output.done = true;
        output.endReason = SmbEpisodeEndReason::LifeLost;
        output.snapshot = makeSnapshot(
            input.state,
            gameplayFrameCount_,
            lastProgressFrame_,
            bestStageIndex_,
            bestAbsoluteX_,
            distanceRewardTotal_,
            levelClearRewardTotal_,
            kSmbNoProgressTimeoutFrames,
            output.endReason,
            true);
        return output;
    }

    if (input.state.lifeState == SmbLifeState::Alive) {
        bool frontierImproved = false;

        if (currentStageIndex > bestStageIndex_) {
            output.levelClearRewardDelta =
                kLevelClearReward * static_cast<double>(currentStageIndex - bestStageIndex_);
            output.rewardDelta += output.levelClearRewardDelta;
            levelClearRewardTotal_ += output.levelClearRewardDelta;
            bestStageIndex_ = currentStageIndex;
            bestAbsoluteX_ = 0;
            frontierImproved = true;
        }

        if (currentStageIndex == bestStageIndex_ && input.state.absoluteX > bestAbsoluteX_) {
            output.distanceRewardDelta =
                kDistanceReward * static_cast<double>(input.state.absoluteX - bestAbsoluteX_);
            output.rewardDelta += output.distanceRewardDelta;
            distanceRewardTotal_ += output.distanceRewardDelta;
            bestAbsoluteX_ = input.state.absoluteX;
            frontierImproved = true;
        }

        if (frontierImproved) {
            lastProgressFrame_ = gameplayFrameCount_;
        }
    }

    if ((gameplayFrameCount_ - lastProgressFrame_) >= kNoProgressTimeoutFrames) {
        output.done = true;
        output.endReason = SmbEpisodeEndReason::NoProgressTimeout;
    }

    output.snapshot = makeSnapshot(
        input.state,
        gameplayFrameCount_,
        lastProgressFrame_,
        bestStageIndex_,
        bestAbsoluteX_,
        distanceRewardTotal_,
        levelClearRewardTotal_,
        kSmbNoProgressTimeoutFrames,
        output.endReason,
        output.done);
    return output;
}

} // namespace DirtSim
