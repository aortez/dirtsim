#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"
#include "core/scenarios/nes/NesRomProfileExtractor.h"

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
        output.done = evaluation.done;
        output.features = evaluation.features;
        output.gameState = evaluation.gameState;
        output.rewardDelta = evaluation.rewardDelta;
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        if (input.paletteFrame == nullptr) {
            DuckSensoryData sensory{};
            sensory.delta_time_seconds = input.deltaTimeSeconds;
            return sensory;
        }

        DuckSensoryData sensory = makeNesDuckSensoryDataFromPaletteFrame(
            paletteClusterer_, *input.paletteFrame, input.deltaTimeSeconds);
        if ((input.controllerMask & NesPolicyLayout::ButtonLeft) != 0u) {
            sensory.facing_x = -1.0f;
        }
        else if ((input.controllerMask & NesPolicyLayout::ButtonRight) != 0u) {
            sensory.facing_x = 1.0f;
        }
        else {
            sensory.facing_x = 1.0f;
        }
        return sensory;
    }

private:
    std::optional<NesRomProfileExtractor> extractor_ = std::nullopt;
    std::optional<NesFlappyBirdEvaluator> evaluator_ = std::nullopt;
    NesPaletteClusterer paletteClusterer_;
    uint32_t startPulseFrameCounter_ = 0;
    uint32_t waitingFlapPulseFrameCounter_ = 0;
};

class NesSuperTiltBroGameAdapter final : public NesGameAdapter {
public:
    void reset(const std::string& runtimeRomId) override
    {
        paletteClusterer_.reset(runtimeRomId);
        advancedFrameCount_ = 0;
    }

    uint8_t resolveControllerMask(const NesGameAdapterControllerInput& input) override
    {
        if (advancedFrameCount_ < kSetupScriptEndFrame) {
            return scriptedSetupMaskForFrame(advancedFrameCount_);
        }

        return input.inferredControllerMask;
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        if (input.paletteFrame != nullptr) {
            paletteClusterer_.observeFrame(*input.paletteFrame);
        }

        advancedFrameCount_ += input.advancedFrames;

        NesGameAdapterFrameOutput output;
        output.rewardDelta += static_cast<double>(input.advancedFrames);
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        if (input.paletteFrame == nullptr) {
            DuckSensoryData sensory{};
            sensory.delta_time_seconds = input.deltaTimeSeconds;
            return sensory;
        }

        DuckSensoryData sensory = makeNesDuckSensoryDataFromPaletteFrame(
            paletteClusterer_, *input.paletteFrame, input.deltaTimeSeconds);
        if ((input.controllerMask & NesPolicyLayout::ButtonLeft) != 0u) {
            sensory.facing_x = -1.0f;
        }
        else if ((input.controllerMask & NesPolicyLayout::ButtonRight) != 0u) {
            sensory.facing_x = 1.0f;
        }
        else {
            sensory.facing_x = 1.0f;
        }
        return sensory;
    }

private:
    static constexpr uint64_t kSetupScriptEndFrame = 540;

    static uint8_t scriptedSetupMaskForFrame(uint64_t frameIndex)
    {
        constexpr uint64_t kBootWaitFrames = 120;
        if (frameIndex < kBootWaitFrames) {
            return 0u;
        }

        const uint64_t phase = (frameIndex - kBootWaitFrames) / 60;
        const uint64_t withinPhase = (frameIndex - kBootWaitFrames) % 60;

        if (withinPhase < 2) {
            return NesPolicyLayout::ButtonStart;
        }
        if (withinPhase >= 10 && withinPhase < 12) {
            return NesPolicyLayout::ButtonA;
        }

        if (phase % 2 == 1 && withinPhase >= 20 && withinPhase < 34) {
            return NesPolicyLayout::ButtonRight;
        }

        return 0u;
    }

    NesPaletteClusterer paletteClusterer_;
    uint64_t advancedFrameCount_ = 0;
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesFlappyParatroopaGameAdapter()
{
    return std::make_unique<NesFlappyParatroopaGameAdapter>();
}

std::unique_ptr<NesGameAdapter> createNesSuperTiltBroGameAdapter()
{
    return std::make_unique<NesSuperTiltBroGameAdapter>();
}

} // namespace DirtSim
