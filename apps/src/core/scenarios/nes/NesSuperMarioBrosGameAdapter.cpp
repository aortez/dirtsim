#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/LoggingChannels.h"
#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"
#include "core/scenarios/nes/NesSuperMarioBrosTilePosition.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace DirtSim {

namespace {

constexpr double kNesFrameHeight = 240.0;
constexpr double kNesFrameWidth = 256.0;
constexpr int16_t kSmbDefaultTilePlayerScreenX = 128;
constexpr int16_t kSmbDefaultTilePlayerScreenY =
    120 - static_cast<int16_t>(NesTileFrame::TopCropPixels);

double normalizeSmb(double value, double maxValue)
{
    return std::clamp(value / maxValue, 0.0, 1.0);
}

double normalizeSmbSigned(double value, double maxMagnitude)
{
    return std::clamp(value / maxMagnitude, -1.0, 1.0);
}

float normalizeViewCoordinate(double value, double extent)
{
    if (extent <= 0.0) {
        return 0.5f;
    }

    return static_cast<float>(std::clamp(value / extent, 0.0, 1.0));
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
    senses[14] = state.secondEnemyPresent ? 1.0 : 0.0;
    senses[15] = normalizeSmb(static_cast<double>(state.world), 7.0);
    senses[16] = normalizeSmb(static_cast<double>(state.level), 3.0);
    senses[17] = static_cast<double>(state.movementX);

    return senses;
}

class NesSuperMarioBrosGameAdapter final : public NesGameAdapter {
public:
    void reset(const std::string& runtimeRomId) override
    {
        paletteClusterer_.reset(runtimeRomId);
        advancedFrameCount_ = 0;
        cachedFacingX_ = 0.0f;
        evaluator_.reset();
        cachedSpecialSenses_.fill(0.0);
        cachedTileGameplayState_ = std::nullopt;
        cachedTilePlayerScreenX_ = kSmbDefaultTilePlayerScreenX;
        cachedTilePlayerScreenY_ = kSmbDefaultTilePlayerScreenY;
        setupFailureLogged_ = false;
    }

    NesGameAdapterControllerOutput resolveControllerMask(
        const NesGameAdapterControllerInput& input) override
    {
        const NesSuperMarioBrosSetupDecision decision = resolveNesSuperMarioBrosSetupDecision(
            advancedFrameCount_, input.lastGameState, input.inferredControllerMask);

        NesGameAdapterControllerOutput output;
        output.resolvedControllerMask = decision.controllerMask;
        output.source = decision.usingSetupScript ? NesGameAdapterControllerSource::ScriptedSetup
                                                  : NesGameAdapterControllerSource::InferredPolicy;
        if (decision.usingSetupScript) {
            output.sourceFrameIndex = decision.frameIndex;
        }
        return output;
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        if (input.paletteFrame != nullptr) {
            paletteClusterer_.observeFrame(*input.paletteFrame);
        }

        advancedFrameCount_ += input.advancedFrames;
        cachedFacingX_ = 0.0f;
        cachedSpecialSenses_.fill(0.0);
        cachedSelfViewX_ = 0.5f;
        cachedSelfViewY_ = 0.5f;
        cachedTileGameplayState_ = std::nullopt;
        cachedTilePlayerScreenX_ = kSmbDefaultTilePlayerScreenX;
        cachedTilePlayerScreenY_ = kSmbDefaultTilePlayerScreenY;

        NesGameAdapterFrameOutput output;
        if (!input.memorySnapshot.has_value()) {
            return output;
        }

        const NesSuperMarioBrosState state = extractor_.extract(
            input.memorySnapshot.value(),
            advancedFrameCount_ >= getNesSuperMarioBrosSetupScriptEndFrame());
        output.gameState = state.phase == SmbPhase::Gameplay ? std::optional<uint8_t>(1u)
                                                             : std::optional<uint8_t>(0u);
        output.debugState = NesGameAdapterDebugState{
            .advancedFrameCount = advancedFrameCount_,
            .level = state.level,
            .lifeState = static_cast<uint8_t>(state.lifeState),
            .lives = state.lives,
            .phase = static_cast<uint8_t>(state.phase),
            .playerXScreen = state.playerXScreen,
            .playerYScreen = state.playerYScreen,
            .powerupState = static_cast<uint8_t>(state.powerupState),
            .world = state.world,
            .absoluteX = state.absoluteX,
            .setupFailure = state.phase != SmbPhase::Gameplay
                && advancedFrameCount_ >= getNesSuperMarioBrosSetupFailureFrame(),
            .setupScriptActive = state.phase != SmbPhase::Gameplay,
        };

        if (state.phase != SmbPhase::Gameplay
            && advancedFrameCount_ >= getNesSuperMarioBrosSetupFailureFrame()) {
            if (!setupFailureLogged_) {
                LOG_ERROR(
                    Scenario,
                    "NesSuperMarioBrosGameAdapter: Failed to reach gameplay by frame {}. "
                    "Ending evaluation early.",
                    advancedFrameCount_);
                setupFailureLogged_ = true;
            }
            output.done = true;
            return output;
        }

        if (state.phase == SmbPhase::Gameplay) {
            cachedFacingX_ = state.facingX;
            cachedSpecialSenses_ = makeSmbSpecialSenses(state);
            cachedSelfViewX_ =
                normalizeViewCoordinate(static_cast<double>(state.playerXScreen), kNesFrameWidth);
            cachedSelfViewY_ =
                normalizeViewCoordinate(static_cast<double>(state.playerYScreen), kNesFrameHeight);
            cachedTileGameplayState_ = state;
            cachedTilePlayerScreenX_ = static_cast<int16_t>(state.playerXScreen);
            cachedTilePlayerScreenY_ = makeNesSuperMarioBrosPlayerTileScreenY(state);
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
            paletteClusterer_,
            input.paletteFrame,
            input.deltaTimeSeconds,
            cachedSpecialSenses_,
            cachedFacingX_,
            cachedSelfViewX_,
            cachedSelfViewY_,
            input.controllerMask);
    }

    NesTileSensoryBuilderInput makeNesTileSensoryBuilderInput(
        const NesGameAdapterSensoryInput& input) const override
    {
        int16_t playerScreenX = cachedTilePlayerScreenX_;
        int16_t playerScreenY = cachedTilePlayerScreenY_;
        if (cachedTileGameplayState_.has_value()) {
            playerScreenY =
                makeNesSuperMarioBrosPlayerTileScreenY(cachedTileGameplayState_.value());
            if (input.tileFrameScrollX.has_value()) {
                playerScreenX = makeNesSuperMarioBrosPlayerTileScreenX(
                    cachedTileGameplayState_.value(), input.tileFrameScrollX.value());
            }
        }

        return NesTileSensoryBuilderInput{
            .playerScreenX = playerScreenX,
            .playerScreenY = playerScreenY,
            .facingX = cachedFacingX_,
            .selfViewX = cachedSelfViewX_,
            .selfViewY = cachedSelfViewY_,
            .controllerMask = input.controllerMask,
            .specialSenses = cachedSpecialSenses_,
            .deltaTimeSeconds = input.deltaTimeSeconds,
        };
    }

private:
    NesPaletteClusterer paletteClusterer_;
    NesSuperMarioBrosRamExtractor extractor_;
    NesSuperMarioBrosEvaluator evaluator_;
    uint64_t advancedFrameCount_ = 0;
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> cachedSpecialSenses_{};
    float cachedFacingX_ = 0.0f;
    float cachedSelfViewX_ = 0.5f;
    float cachedSelfViewY_ = 0.5f;
    std::optional<NesSuperMarioBrosState> cachedTileGameplayState_ = std::nullopt;
    int16_t cachedTilePlayerScreenX_ = kSmbDefaultTilePlayerScreenX;
    int16_t cachedTilePlayerScreenY_ = kSmbDefaultTilePlayerScreenY;
    bool setupFailureLogged_ = false;
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesSuperMarioBrosGameAdapter()
{
    return std::make_unique<NesSuperMarioBrosGameAdapter>();
}

} // namespace DirtSim
