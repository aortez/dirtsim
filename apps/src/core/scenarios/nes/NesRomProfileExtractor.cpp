#include "NesRomProfileExtractor.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace DirtSim {

namespace {
constexpr uint16_t kBirdYFracAddr = 0x00;
constexpr uint16_t kBirdYAddr = 0x01;
constexpr uint16_t kBirdVelocityLoAddr = 0x02;
constexpr uint16_t kBirdVelocityHiAddr = 0x03;
constexpr uint16_t kScrollXAddr = 0x08;
constexpr uint16_t kScrollNtAddr = 0x09;
constexpr uint16_t kGameStateAddr = 0x0A;
constexpr uint16_t kNt0Pipe0GapAddr = 0x12;
constexpr uint16_t kNt0Pipe1GapAddr = 0x13;
constexpr uint16_t kNt1Pipe0GapAddr = 0x14;
constexpr uint16_t kNt1Pipe1GapAddr = 0x15;
constexpr uint16_t kScoreOnesAddr = 0x19;
constexpr uint16_t kScoreTensAddr = 0x1A;
constexpr uint16_t kScoreHundredsAddr = 0x1B;

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

int decodeScoreDigit(uint8_t value)
{
    return std::min<int>(value, 9);
}

int decodeScore(const SmolnesRuntime::MemorySnapshot& snapshot)
{
    const int ones = decodeScoreDigit(snapshot.cpuRam.at(kScoreOnesAddr));
    const int tens = decodeScoreDigit(snapshot.cpuRam.at(kScoreTensAddr));
    const int hundreds = decodeScoreDigit(snapshot.cpuRam.at(kScoreHundredsAddr));
    return (hundreds * 100) + (tens * 10) + ones;
}

bool isDoneState(uint8_t gameState)
{
    return gameState >= kStateDying && gameState <= kStateGameOver;
}

PipeSample selectUpcomingPipe(const SmolnesRuntime::MemorySnapshot& snapshot)
{
    const uint8_t scrollX = snapshot.cpuRam.at(kScrollXAddr);
    const uint8_t scrollNt = snapshot.cpuRam.at(kScrollNtAddr) & 0x01u;

    PipeSample nearPipe;
    PipeSample farPipe;
    nearPipe.screenX = 128.0f - static_cast<float>(scrollX);
    farPipe.screenX = 256.0f - static_cast<float>(scrollX);

    if (scrollNt == 0u) {
        nearPipe.gapRow = snapshot.cpuRam.at(kNt0Pipe1GapAddr);
        farPipe.gapRow = snapshot.cpuRam.at(kNt1Pipe0GapAddr);
    }
    else {
        nearPipe.gapRow = snapshot.cpuRam.at(kNt1Pipe1GapAddr);
        farPipe.gapRow = snapshot.cpuRam.at(kNt0Pipe0GapAddr);
    }

    if ((nearPipe.screenX + kPipeWidthPx) >= kBirdLeftPx) {
        return nearPipe;
    }
    return farPipe;
}

NesRomFrameExtraction extractFlappyFeatures(
    const SmolnesRuntime::MemorySnapshot& snapshot, uint8_t previousControllerMask)
{
    NesRomFrameExtraction output;
    output.features.fill(0.0f);

    const uint8_t gameState = snapshot.cpuRam.at(kGameStateAddr);
    output.gameState = gameState;
    output.done = isDoneState(gameState);

    const float birdY = static_cast<float>(snapshot.cpuRam.at(kBirdYAddr));
    const float birdYFrac = static_cast<float>(snapshot.cpuRam.at(kBirdYFracAddr));
    const int8_t birdVelHi = static_cast<int8_t>(snapshot.cpuRam.at(kBirdVelocityHiAddr));
    const float birdVelocity = static_cast<float>(birdVelHi)
        + (static_cast<float>(snapshot.cpuRam.at(kBirdVelocityLoAddr)) / 256.0f);

    const PipeSample nextPipe = selectUpcomingPipe(snapshot);
    const float nextPipeTopPx = static_cast<float>(nextPipe.gapRow) * 8.0f;
    const float nextPipeBottomPx = nextPipeTopPx + kGapHeightPx;
    const float nextPipeCenterPx = (nextPipeTopPx + nextPipeBottomPx) * 0.5f;
    const float birdCenterPx = birdY + kBirdCenterYOffsetPx + (birdYFrac / 256.0f);

    const int score = decodeScore(snapshot);

    output.features.at(static_cast<size_t>(FeatureIndex::Bias)) = 1.0f;
    output.features.at(static_cast<size_t>(FeatureIndex::BirdYNormalized)) =
        clamp01((birdY - kCeilingY) / std::max(1.0f, kGroundY - kCeilingY));
    output.features.at(static_cast<size_t>(FeatureIndex::BirdVelocityNormalized)) =
        clampSigned1(birdVelocity / kVelocityScale);
    output.features.at(static_cast<size_t>(FeatureIndex::NextPipeDistanceNormalized)) =
        clamp01((nextPipe.screenX - kBirdLeftPx) / kVisiblePipeDistancePx);
    output.features.at(static_cast<size_t>(FeatureIndex::NextPipeTopNormalized)) =
        clamp01(nextPipeTopPx / kGroundY);
    output.features.at(static_cast<size_t>(FeatureIndex::NextPipeBottomNormalized)) =
        clamp01(nextPipeBottomPx / kGroundY);
    output.features.at(static_cast<size_t>(FeatureIndex::BirdGapOffsetNormalized)) =
        clampSigned1((birdCenterPx - nextPipeCenterPx) / kGapHeightPx);
    output.features.at(static_cast<size_t>(FeatureIndex::ScrollXNormalized)) =
        static_cast<float>(snapshot.cpuRam.at(kScrollXAddr)) / 255.0f;
    output.features.at(static_cast<size_t>(FeatureIndex::ScrollNt)) =
        static_cast<float>(snapshot.cpuRam.at(kScrollNtAddr) & 0x01u);
    output.features.at(static_cast<size_t>(FeatureIndex::GameStateNormalized)) =
        clamp01(static_cast<float>(gameState) / 9.0f);
    output.features.at(static_cast<size_t>(FeatureIndex::ScoreNormalized)) =
        clamp01(static_cast<float>(score) / 999.0f);
    output.features.at(static_cast<size_t>(FeatureIndex::PrevFlapPressed)) =
        (previousControllerMask & NesPolicyLayout::ButtonA) != 0u ? 1.0f : 0.0f;

    return output;
}
} // namespace

NesRomProfileExtractor::NesRomProfileExtractor(std::string romId)
{
    const std::string normalizedRomId = normalizeRomId(romId);
    if (normalizedRomId == NesPolicyLayout::FlappyParatroopaWorldUnlRomId) {
        profile_ = Profile::FlappyParatroopaWorldUnl;
    }
}

bool NesRomProfileExtractor::isSupported() const
{
    return profile_ != Profile::Unsupported;
}

void NesRomProfileExtractor::reset()
{
    didApplyDeathPenalty_ = false;
    hasLastScore_ = false;
    lastScore_ = 0;
}

NesRomFrameExtraction NesRomProfileExtractor::extract(
    const SmolnesRuntime::MemorySnapshot& snapshot, uint8_t previousControllerMask)
{
    NesRomFrameExtraction output;
    output.features.fill(0.0f);

    if (profile_ == Profile::Unsupported) {
        return output;
    }

    output = extractFlappyFeatures(snapshot, previousControllerMask);
    const int score = decodeScore(snapshot);

    if (hasLastScore_) {
        if (score > lastScore_) {
            output.rewardDelta += static_cast<double>(score - lastScore_);
        }
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

std::string NesRomProfileExtractor::normalizeRomId(const std::string& rawRomId)
{
    std::string normalized;
    normalized.reserve(rawRomId.size());

    bool pendingSeparator = false;
    for (char ch : rawRomId) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            if (pendingSeparator && !normalized.empty() && normalized.back() != '-') {
                normalized.push_back('-');
            }
            normalized.push_back(static_cast<char>(std::tolower(uch)));
            pendingSeparator = false;
            continue;
        }
        pendingSeparator = true;
    }

    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }

    return normalized;
}

} // namespace DirtSim
