#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesFlappyParatroopaRamExtractor.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"

#include <algorithm>

namespace DirtSim {

namespace {
constexpr float kFlappyBirdCenterXPx = 64.0f;
constexpr float kFlappyFrameHeightPx = 240.0f;
constexpr float kFlappyFrameWidthPx = 256.0f;
constexpr int16_t kFlappyDefaultTilePlayerScreenX = 128;
constexpr int16_t kFlappyDefaultTilePlayerScreenY =
    120 - static_cast<int16_t>(NesTileFrame::TopCropPixels);
constexpr uint8_t kNesStateTitle = 0;
constexpr uint8_t kNesStateWaiting = 1;
constexpr uint8_t kNesStateGameOver = 7;
constexpr uint8_t kNesStateFadeIn = 8;
constexpr uint8_t kNesStateTitleFade = 9;
constexpr uint32_t kNesStartPulsePeriodFrames = 12;
constexpr uint32_t kNesStartPulseWidthFrames = 2;
constexpr uint32_t kNesWaitingFlapPulsePeriodFrames = 8;
constexpr uint32_t kNesWaitingFlapPulseWidthFrames = 1;

bool isTitleLikeNesState(uint8_t gameState)
{
    return gameState == kNesStateTitle || gameState == kNesStateGameOver
        || gameState == kNesStateFadeIn || gameState == kNesStateTitleFade;
}

constexpr size_t kFlappyFeatureBirdYNormalized = 1;
constexpr size_t kFlappyFeatureBirdVelocityNormalized = 2;
constexpr size_t kFlappyFeatureScrollXNormalized = 7;
constexpr size_t kFlappyFeatureScrollNt = 8;
constexpr size_t kFlappyFeatureScoreNormalized = 10;

float computeFlappyBirdCenterYPx(const NesFlappyBirdState& state)
{
    return state.birdY + 8.0f + (state.birdYFraction / 256.0f);
}

float normalizeViewCoordinate(float value, float extent)
{
    if (extent <= 0.0f) {
        return 0.5f;
    }

    return std::clamp(value / extent, 0.0f, 1.0f);
}

std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> makeFlappySpecialSenses(
    const std::array<float, NesPolicyLayout::InputCount>& ramFeatures)
{
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> senses{};
    senses.fill(0.0);

    senses[0] = static_cast<double>(ramFeatures.at(kFlappyFeatureBirdYNormalized));
    senses[1] = static_cast<double>(ramFeatures.at(kFlappyFeatureBirdVelocityNormalized));
    senses[2] = static_cast<double>(ramFeatures.at(kFlappyFeatureScoreNormalized));

    const double scrollX =
        static_cast<double>(ramFeatures.at(kFlappyFeatureScrollXNormalized)) * 255.0;
    const double scrollNt = ramFeatures.at(kFlappyFeatureScrollNt) >= 0.5f ? 1.0 : 0.0;
    const double scrollPosition = scrollX + (scrollNt * 256.0);
    senses[3] = std::clamp(scrollPosition / 511.0, 0.0, 1.0);
    return senses;
}

class NesFlappyParatroopaGameAdapter final : public NesGameAdapter {
public:
    void reset(const std::string& runtimeRomId) override
    {
        extractor_.emplace(runtimeRomId);
        evaluator_.emplace();
        evaluator_->reset();
        paletteClusterer_.reset(runtimeRomId);
        startPulseFrameCounter_ = 0;
        waitingFlapPulseFrameCounter_ = 0;
        cachedSpecialSenses_.fill(0.0);
        cachedSelfViewX_ = 0.5f;
        cachedSelfViewY_ = 0.5f;
        cachedTilePlayerScreenX_ = kFlappyDefaultTilePlayerScreenX;
        cachedTilePlayerScreenY_ = kFlappyDefaultTilePlayerScreenY;
    }

    NesGameAdapterControllerOutput resolveControllerMask(
        const NesGameAdapterControllerInput& input) override
    {
        NesGameAdapterControllerOutput output;
        output.resolvedControllerMask = input.inferredControllerMask;
        const uint8_t gameState = input.lastGameState.value_or(kNesStateTitle);
        if (isTitleLikeNesState(gameState)) {
            const bool pressStart =
                (startPulseFrameCounter_ % kNesStartPulsePeriodFrames) < kNesStartPulseWidthFrames;
            output.resolvedControllerMask = pressStart ? NesPolicyLayout::ButtonStart : 0u;
            output.source = NesGameAdapterControllerSource::ScriptedSetup;
            ++startPulseFrameCounter_;
            waitingFlapPulseFrameCounter_ = 0;
            return output;
        }

        startPulseFrameCounter_ = 0;
        if (gameState == kNesStateWaiting) {
            const bool pressFlap =
                (waitingFlapPulseFrameCounter_ % kNesWaitingFlapPulsePeriodFrames)
                < kNesWaitingFlapPulseWidthFrames;
            output.resolvedControllerMask = pressFlap ? NesPolicyLayout::ButtonA : 0u;
            output.source = NesGameAdapterControllerSource::ScriptedSetup;
            ++waitingFlapPulseFrameCounter_;
            return output;
        }

        waitingFlapPulseFrameCounter_ = 0;
        return output;
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        if (input.paletteFrame != nullptr) {
            paletteClusterer_.observeFrame(*input.paletteFrame);
        }

        cachedSpecialSenses_.fill(0.0);
        cachedSelfViewX_ = 0.5f;
        cachedSelfViewY_ = 0.5f;
        cachedTilePlayerScreenX_ = kFlappyDefaultTilePlayerScreenX;
        cachedTilePlayerScreenY_ = kFlappyDefaultTilePlayerScreenY;

        NesGameAdapterFrameOutput output;
        if (!extractor_.has_value() || !evaluator_.has_value() || !extractor_->isSupported()) {
            output.rewardDelta += static_cast<double>(input.advancedFrames);
            return output;
        }

        if (!input.memorySnapshot.has_value()) {
            return output;
        }

        const std::optional<NesFlappyBirdEvaluatorInput> evaluatorInput =
            extractor_->extract(input.memorySnapshot.value(), input.controllerMask);
        if (!evaluatorInput.has_value()) {
            return output;
        }

        const NesFlappyBirdEvaluatorOutput evaluation =
            evaluator_->evaluate(evaluatorInput.value());
        cachedSpecialSenses_ = makeFlappySpecialSenses(evaluation.features);
        cachedSelfViewX_ = normalizeViewCoordinate(kFlappyBirdCenterXPx, kFlappyFrameWidthPx);
        const float birdCenterYPx = computeFlappyBirdCenterYPx(evaluatorInput->state);
        cachedSelfViewY_ = normalizeViewCoordinate(birdCenterYPx, kFlappyFrameHeightPx);
        cachedTilePlayerScreenX_ = static_cast<int16_t>(kFlappyBirdCenterXPx);
        cachedTilePlayerScreenY_ = static_cast<int16_t>(
            static_cast<int16_t>(birdCenterYPx)
            - static_cast<int16_t>(NesTileFrame::TopCropPixels));
        output.done = evaluation.done;
        output.gameState = evaluation.gameState;
        output.rewardDelta = evaluation.rewardDelta;
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        return makeNesDuckSensoryData(
            paletteClusterer_,
            input.paletteFrame,
            input.deltaTimeSeconds,
            cachedSpecialSenses_,
            0.0f,
            cachedSelfViewX_,
            cachedSelfViewY_,
            input.controllerMask);
    }

    NesTileSensoryBuilderInput makeNesTileSensoryBuilderInput(
        const NesGameAdapterSensoryInput& input) const override
    {
        return NesTileSensoryBuilderInput{
            .playerScreenX = cachedTilePlayerScreenX_,
            .playerScreenY = cachedTilePlayerScreenY_,
            .facingX = 0.0f,
            .selfViewX = cachedSelfViewX_,
            .selfViewY = cachedSelfViewY_,
            .controllerMask = input.controllerMask,
            .specialSenses = cachedSpecialSenses_,
            .deltaTimeSeconds = input.deltaTimeSeconds,
        };
    }

private:
    std::optional<NesFlappyParatroopaRamExtractor> extractor_ = std::nullopt;
    std::optional<NesFlappyBirdEvaluator> evaluator_ = std::nullopt;
    NesPaletteClusterer paletteClusterer_;
    uint32_t startPulseFrameCounter_ = 0;
    uint32_t waitingFlapPulseFrameCounter_ = 0;
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> cachedSpecialSenses_{};
    float cachedSelfViewX_ = 0.5f;
    float cachedSelfViewY_ = 0.5f;
    int16_t cachedTilePlayerScreenX_ = kFlappyDefaultTilePlayerScreenX;
    int16_t cachedTilePlayerScreenY_ = kFlappyDefaultTilePlayerScreenY;
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesFlappyParatroopaGameAdapter()
{
    return std::make_unique<NesFlappyParatroopaGameAdapter>();
}

} // namespace DirtSim
