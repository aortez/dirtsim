#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"

#include <algorithm>
#include <cstdint>

namespace DirtSim {

namespace {

constexpr uint64_t kSetupScriptEndFrame = 300;

double normalizeSmb(double value, double maxValue)
{
    return std::clamp(value / maxValue, 0.0, 1.0);
}

double normalizeSmbSigned(double value, double maxMagnitude)
{
    return std::clamp(value / maxMagnitude, -1.0, 1.0);
}

std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> makeSmbSpecialSenses(
    const NesSuperMarioBrosState& state)
{
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> senses{};
    senses.fill(0.0);

    const double progress =
        (static_cast<double>(state.world) * 4.0 + static_cast<double>(state.level)) / 32.0;
    senses[0] = std::clamp(progress, 0.0, 1.0);
    senses[1] = normalizeSmb(static_cast<double>(state.absoluteX), 4096.0);
    senses[2] = state.horizontalSpeedNormalized;
    senses[3] = state.verticalSpeedNormalized;

    if (state.powerupState == SmbPowerupState::Fire) {
        senses[4] = 1.0;
    }
    else if (state.powerupState == SmbPowerupState::Big) {
        senses[4] = 0.5;
    }

    senses[5] = state.airborne ? 1.0 : 0.0;
    senses[6] = normalizeSmb(static_cast<double>(state.playerYScreen), 240.0);
    senses[7] = normalizeSmb(static_cast<double>(state.lives), 9.0);
    senses[8] = normalizeSmb(static_cast<double>(state.playerXScreen), 255.0);
    senses[9] = normalizeSmbSigned(static_cast<double>(state.nearestEnemyDx), 255.0);
    senses[10] = normalizeSmbSigned(static_cast<double>(state.nearestEnemyDy), 240.0);
    senses[11] = normalizeSmbSigned(static_cast<double>(state.secondNearestEnemyDx), 255.0);
    senses[12] = normalizeSmbSigned(static_cast<double>(state.secondNearestEnemyDy), 240.0);
    senses[13] = state.enemyPresent ? 1.0 : 0.0;

    return senses;
}

class NesSuperMarioBrosGameAdapter final : public NesGameAdapter {
public:
    void reset(const std::string& runtimeRomId) override
    {
        paletteClusterer_.reset(runtimeRomId);
        advancedFrameCount_ = 0;
        evaluator_.reset();
        cachedSpecialSenses_.fill(0.0);
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
        cachedSpecialSenses_.fill(0.0);

        NesGameAdapterFrameOutput output;
        if (!input.memorySnapshot.has_value()) {
            return output;
        }

        const NesSuperMarioBrosState state = extractor_.extract(
            input.memorySnapshot.value(), advancedFrameCount_ >= kSetupScriptEndFrame);
        output.gameState = state.phase == SmbPhase::Gameplay ? std::optional<uint8_t>(1u)
                                                             : std::optional<uint8_t>(0u);

        if (state.phase == SmbPhase::Gameplay) {
            cachedSpecialSenses_ = makeSmbSpecialSenses(state);
        }

        const NesSuperMarioBrosEvaluatorInput evaluatorInput{
            .advancedFrames = input.advancedFrames,
            .state = state,
        };
        const NesSuperMarioBrosEvaluatorOutput evaluation = evaluator_.evaluate(evaluatorInput);
        output.done = evaluation.done;
        output.fitnessDetails = evaluation.snapshot;
        output.rewardDelta = evaluation.rewardDelta;
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        return makeNesDuckSensoryData(
            paletteClusterer_, input.paletteFrame, input.deltaTimeSeconds, cachedSpecialSenses_);
    }

private:
    static uint8_t scriptedSetupMaskForFrame(uint64_t frameIndex)
    {
        constexpr uint64_t kStartPressWidthFrames = 1;
        constexpr std::array<uint64_t, 2> kStartPressFrames = { 120u, 240u };
        for (const uint64_t pressFrame : kStartPressFrames) {
            if (frameIndex >= pressFrame && frameIndex < (pressFrame + kStartPressWidthFrames)) {
                return NesPolicyLayout::ButtonStart;
            }
        }

        return 0u;
    }

    NesPaletteClusterer paletteClusterer_;
    NesSuperMarioBrosRamExtractor extractor_;
    NesSuperMarioBrosEvaluator evaluator_;
    uint64_t advancedFrameCount_ = 0;
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> cachedSpecialSenses_{};
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesSuperMarioBrosGameAdapter()
{
    return std::make_unique<NesSuperMarioBrosGameAdapter>();
}

} // namespace DirtSim
