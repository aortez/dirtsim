#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"

namespace DirtSim {

namespace {

constexpr uint32_t kLevelsPerWorld = 4;

uint32_t getStageIndex(const NesSuperMarioBrosState& state)
{
    return (static_cast<uint32_t>(state.world) * kLevelsPerWorld) + state.level;
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
}

NesSuperMarioBrosEvaluatorOutput NesSuperMarioBrosEvaluator::evaluate(
    const NesSuperMarioBrosEvaluatorInput& input)
{
    NesSuperMarioBrosEvaluatorOutput output;
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
        return output;
    }
    else {
        lastLives_ = input.state.lives;
    }

    if (input.state.lifeState == SmbLifeState::Dead && input.state.lives == 0u) {
        output.done = true;
        return output;
    }

    if (input.state.lifeState == SmbLifeState::Alive) {
        bool frontierImproved = false;

        if (currentStageIndex > bestStageIndex_) {
            output.rewardDelta +=
                kLevelClearReward * static_cast<double>(currentStageIndex - bestStageIndex_);
            bestStageIndex_ = currentStageIndex;
            bestAbsoluteX_ = 0;
            frontierImproved = true;
        }

        if (currentStageIndex == bestStageIndex_ && input.state.absoluteX > bestAbsoluteX_) {
            output.rewardDelta +=
                kDistanceReward * static_cast<double>(input.state.absoluteX - bestAbsoluteX_);
            bestAbsoluteX_ = input.state.absoluteX;
            frontierImproved = true;
        }

        if (frontierImproved) {
            lastProgressFrame_ = gameplayFrameCount_;
        }
    }

    if ((gameplayFrameCount_ - lastProgressFrame_) >= kNoProgressTimeoutFrames) {
        output.done = true;
    }

    return output;
}

} // namespace DirtSim
