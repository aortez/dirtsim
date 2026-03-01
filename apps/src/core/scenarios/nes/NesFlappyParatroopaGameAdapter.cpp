#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesFlappyParatroopaRamExtractor.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"

#include <algorithm>

namespace DirtSim {

namespace {
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
    }

    uint8_t resolveControllerMask(const NesGameAdapterControllerInput& input) override
    {
        uint8_t controllerMask = input.inferredControllerMask;
        const uint8_t gameState = input.lastGameState.value_or(kNesStateTitle);
        if (isTitleLikeNesState(gameState)) {
            const bool pressStart =
                (startPulseFrameCounter_ % kNesStartPulsePeriodFrames) < kNesStartPulseWidthFrames;
            controllerMask = pressStart ? NesPolicyLayout::ButtonStart : 0u;
            ++startPulseFrameCounter_;
            waitingFlapPulseFrameCounter_ = 0;
            return controllerMask;
        }

        startPulseFrameCounter_ = 0;
        if (gameState == kNesStateWaiting) {
            const bool pressFlap =
                (waitingFlapPulseFrameCounter_ % kNesWaitingFlapPulsePeriodFrames)
                < kNesWaitingFlapPulseWidthFrames;
            controllerMask = pressFlap ? NesPolicyLayout::ButtonA : 0u;
            ++waitingFlapPulseFrameCounter_;
            return controllerMask;
        }

        waitingFlapPulseFrameCounter_ = 0;
        return controllerMask;
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        if (input.paletteFrame != nullptr) {
            paletteClusterer_.observeFrame(*input.paletteFrame);
        }

        cachedSpecialSenses_.fill(0.0);

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
        output.done = evaluation.done;
        output.gameState = evaluation.gameState;
        output.rewardDelta = evaluation.rewardDelta;
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        return makeNesDuckSensoryData(
            paletteClusterer_, input.paletteFrame, input.deltaTimeSeconds, cachedSpecialSenses_);
    }

private:
    std::optional<NesFlappyParatroopaRamExtractor> extractor_ = std::nullopt;
    std::optional<NesFlappyBirdEvaluator> evaluator_ = std::nullopt;
    NesPaletteClusterer paletteClusterer_;
    uint32_t startPulseFrameCounter_ = 0;
    uint32_t waitingFlapPulseFrameCounter_ = 0;
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> cachedSpecialSenses_{};
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesFlappyParatroopaGameAdapter()
{
    return std::make_unique<NesFlappyParatroopaGameAdapter>();
}

} // namespace DirtSim
