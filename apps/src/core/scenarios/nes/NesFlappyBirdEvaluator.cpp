#include "NesFlappyBirdEvaluator.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {
constexpr uint8_t kStateDying = 3;
constexpr uint8_t kStateGameOver = 7;

constexpr float kBirdCenterYOffsetPx = 8.0f;
constexpr float kBirdLeftPx = 56.0f;
constexpr float kCeilingY = 8.0f;
constexpr float kGapHeightPx = 64.0f;
constexpr float kGroundY = 184.0f;
constexpr float kPipeWidthPx = 32.0f;
constexpr float kVelocityScale = 6.0f;
constexpr float kVisiblePipeDistancePx = 256.0f;
constexpr double kDeathPenalty = -1.0;

enum class FeatureIndex : int {
    Bias = 0,
    BirdYNormalized = 1,
    BirdVelocityNormalized = 2,
    NextPipeDistanceNormalized = 3,
    NextPipeTopNormalized = 4,
    NextPipeBottomNormalized = 5,
    BirdGapOffsetNormalized = 6,
    ScrollXNormalized = 7,
    ScrollNt = 8,
    GameStateNormalized = 9,
    ScoreNormalized = 10,
    PrevFlapPressed = 11,
};

struct PipeSample {
    float screenX = 0.0f;
    uint8_t gapRow = 0;
};

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float clampSigned1(float value)
{
    return std::clamp(value, -1.0f, 1.0f);
}

bool isDoneState(uint8_t gameState)
{
    return gameState >= kStateDying && gameState <= kStateGameOver;
}

PipeSample selectUpcomingPipe(const NesFlappyBirdState& state)
{
    const uint8_t scrollX = state.scrollX;
    const uint8_t scrollNt = state.scrollNt & 0x01u;

    PipeSample nearPipe;
    PipeSample farPipe;
    nearPipe.screenX = 128.0f - static_cast<float>(scrollX);
    farPipe.screenX = 256.0f - static_cast<float>(scrollX);

    if (scrollNt == 0u) {
        nearPipe.gapRow = state.nt0Pipe1Gap;
        farPipe.gapRow = state.nt1Pipe0Gap;
    }
    else {
        nearPipe.gapRow = state.nt1Pipe1Gap;
        farPipe.gapRow = state.nt0Pipe0Gap;
    }

    if ((nearPipe.screenX + kPipeWidthPx) >= kBirdLeftPx) {
        return nearPipe;
    }
    return farPipe;
}

NesFlappyBirdEvaluatorOutput evaluateState(const NesFlappyBirdEvaluatorInput& input)
{
    NesFlappyBirdEvaluatorOutput output;
    output.features.fill(0.0f);
    output.gameState = input.state.gameState;
    output.done = isDoneState(output.gameState);

    const PipeSample nextPipe = selectUpcomingPipe(input.state);
    const float nextPipeTopPx = static_cast<float>(nextPipe.gapRow) * 8.0f;
    const float nextPipeBottomPx = nextPipeTopPx + kGapHeightPx;
    const float nextPipeCenterPx = (nextPipeTopPx + nextPipeBottomPx) * 0.5f;
    const float birdCenterPx =
        input.state.birdY + kBirdCenterYOffsetPx + (input.state.birdYFraction / 256.0f);

    output.features.at(static_cast<size_t>(FeatureIndex::Bias)) = 1.0f;
    output.features.at(static_cast<size_t>(FeatureIndex::BirdYNormalized)) =
        clamp01((input.state.birdY - kCeilingY) / std::max(1.0f, kGroundY - kCeilingY));
    output.features.at(static_cast<size_t>(FeatureIndex::BirdVelocityNormalized)) =
        clampSigned1(input.state.birdVelocity / kVelocityScale);
    output.features.at(static_cast<size_t>(FeatureIndex::NextPipeDistanceNormalized)) =
        clamp01((nextPipe.screenX - kBirdLeftPx) / kVisiblePipeDistancePx);
    output.features.at(static_cast<size_t>(FeatureIndex::NextPipeTopNormalized)) =
        clamp01(nextPipeTopPx / kGroundY);
    output.features.at(static_cast<size_t>(FeatureIndex::NextPipeBottomNormalized)) =
        clamp01(nextPipeBottomPx / kGroundY);
    output.features.at(static_cast<size_t>(FeatureIndex::BirdGapOffsetNormalized)) =
        clampSigned1((birdCenterPx - nextPipeCenterPx) / kGapHeightPx);
    output.features.at(static_cast<size_t>(FeatureIndex::ScrollXNormalized)) =
        static_cast<float>(input.state.scrollX) / 255.0f;
    output.features.at(static_cast<size_t>(FeatureIndex::ScrollNt)) =
        static_cast<float>(input.state.scrollNt & 0x01u);
    output.features.at(static_cast<size_t>(FeatureIndex::GameStateNormalized)) =
        clamp01(static_cast<float>(output.gameState) / 9.0f);
    output.features.at(static_cast<size_t>(FeatureIndex::ScoreNormalized)) =
        clamp01(static_cast<float>(input.state.score) / 999.0f);
    output.features.at(static_cast<size_t>(FeatureIndex::PrevFlapPressed)) =
        (input.previousControllerMask & NesPolicyLayout::ButtonA) != 0u ? 1.0f : 0.0f;

    return output;
}
} // namespace

void NesFlappyBirdEvaluator::reset()
{
    didApplyDeathPenalty_ = false;
    hasLastScore_ = false;
    lastScore_ = 0;
}

NesFlappyBirdEvaluatorOutput NesFlappyBirdEvaluator::evaluate(
    const NesFlappyBirdEvaluatorInput& input)
{
    NesFlappyBirdEvaluatorOutput output = evaluateState(input);

    const int score = input.state.score;
    if (hasLastScore_ && score > lastScore_) {
        output.rewardDelta += static_cast<double>(score - lastScore_);
    }
    lastScore_ = score;
    hasLastScore_ = true;

    if (!output.done) {
        didApplyDeathPenalty_ = false;
    }
    else if (!didApplyDeathPenalty_) {
        output.rewardDelta += kDeathPenalty;
        didApplyDeathPenalty_ = true;
    }

    return output;
}

} // namespace DirtSim
