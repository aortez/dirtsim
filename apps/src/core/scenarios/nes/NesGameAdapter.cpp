#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/Assert.h"
#include "core/MaterialType.h"
#include "core/organisms/evolution/NesDuckSpecialSenseLayout.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesRomProfileExtractor.h"

#include <algorithm>
#include <cmath>

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
constexpr float kNesDuckVelocityScale = 10.0f;

static_assert(
    NesPolicyLayout::InputCount == NesDuckSpecialSenseLayout::FlappyMappedCount,
    "NesGameAdapter: Flappy feature count must match special-sense mapping");

bool isTitleLikeNesState(uint8_t gameState)
{
    return gameState == kNesStateTitle || gameState == kNesStateGameOver
        || gameState == kNesStateFadeIn || gameState == kNesStateTitleFade;
}

void mapFlappyParatroopaFeaturesToSpecialSenses(
    const std::array<float, NesPolicyLayout::InputCount>& features,
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT>& specialSenses)
{
    const auto writeSense =
        [&features, &specialSenses](NesDuckSpecialSenseLayout::Slot slot, int featureIndex) {
            specialSenses[static_cast<size_t>(slot)] =
                static_cast<double>(features[static_cast<size_t>(featureIndex)]);
        };

    writeSense(NesDuckSpecialSenseLayout::Bias, NesDuckSpecialSenseLayout::Bias);
    writeSense(
        NesDuckSpecialSenseLayout::BirdYNormalized, NesDuckSpecialSenseLayout::BirdYNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::BirdVelocityNormalized,
        NesDuckSpecialSenseLayout::BirdVelocityNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::NextPipeDistanceNormalized,
        NesDuckSpecialSenseLayout::NextPipeDistanceNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::NextPipeTopNormalized,
        NesDuckSpecialSenseLayout::NextPipeTopNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::NextPipeBottomNormalized,
        NesDuckSpecialSenseLayout::NextPipeBottomNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::BirdGapOffsetNormalized,
        NesDuckSpecialSenseLayout::BirdGapOffsetNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::ScrollXNormalized, NesDuckSpecialSenseLayout::ScrollXNormalized);
    writeSense(NesDuckSpecialSenseLayout::ScrollNt, NesDuckSpecialSenseLayout::ScrollNt);
    writeSense(
        NesDuckSpecialSenseLayout::GameStateNormalized,
        NesDuckSpecialSenseLayout::GameStateNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::ScoreNormalized, NesDuckSpecialSenseLayout::ScoreNormalized);
    writeSense(
        NesDuckSpecialSenseLayout::PrevFlapPressed, NesDuckSpecialSenseLayout::PrevFlapPressed);
}

class NesFlappyParatroopaGameAdapter final : public NesGameAdapter {
public:
    void reset(const std::string& runtimeRomId) override
    {
        extractor_.emplace(runtimeRomId);
        evaluator_.emplace();
        evaluator_->reset();
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
        DuckSensoryData sensory{};
        sensory.actual_width = DuckSensoryData::GRID_SIZE;
        sensory.actual_height = DuckSensoryData::GRID_SIZE;
        sensory.scale_factor = 1.0;
        sensory.world_offset = { 0, 0 };
        sensory.position = { DuckSensoryData::GRID_SIZE / 2, DuckSensoryData::GRID_SIZE / 2 };
        sensory.delta_time_seconds = input.deltaTimeSeconds;

        const auto setDominantMaterial = [&sensory](int x, int y, Material::EnumType materialType) {
            if (x < 0 || x >= DuckSensoryData::GRID_SIZE || y < 0
                || y >= DuckSensoryData::GRID_SIZE) {
                return;
            }
            auto& histogram =
                sensory.material_histograms[static_cast<size_t>(y)][static_cast<size_t>(x)];
            histogram.fill(0.0);
            const size_t materialIndex = static_cast<size_t>(materialType);
            DIRTSIM_ASSERT(
                materialIndex < histogram.size(),
                "NesGameAdapter: Material index out of range for duck sensory histogram");
            histogram[materialIndex] = 1.0;
        };

        for (int y = 0; y < DuckSensoryData::GRID_SIZE; ++y) {
            for (int x = 0; x < DuckSensoryData::GRID_SIZE; ++x) {
                setDominantMaterial(x, y, Material::EnumType::Air);
            }
        }

        const int birdX = 3;
        const float birdYNormalized = std::clamp(
            input.policyInputs[static_cast<size_t>(NesDuckSpecialSenseLayout::BirdYNormalized)],
            0.0f,
            1.0f);
        const int birdY = std::clamp(
            static_cast<int>(
                std::lround(birdYNormalized * static_cast<float>(DuckSensoryData::GRID_SIZE - 1))),
            0,
            DuckSensoryData::GRID_SIZE - 1);
        setDominantMaterial(birdX, birdY, Material::EnumType::Wood);

        const float pipeDistanceNormalized = std::clamp(
            input.policyInputs[static_cast<size_t>(
                NesDuckSpecialSenseLayout::NextPipeDistanceNormalized)],
            0.0f,
            1.0f);
        const float pipeTopNormalized = std::clamp(
            input.policyInputs[static_cast<size_t>(
                NesDuckSpecialSenseLayout::NextPipeTopNormalized)],
            0.0f,
            1.0f);
        const float pipeBottomNormalized = std::clamp(
            input.policyInputs[static_cast<size_t>(
                NesDuckSpecialSenseLayout::NextPipeBottomNormalized)],
            0.0f,
            1.0f);
        const int pipeX = std::clamp(
            birdX
                + static_cast<int>(std::lround(
                    pipeDistanceNormalized
                    * static_cast<float>(DuckSensoryData::GRID_SIZE - 1 - birdX))),
            birdX + 1,
            DuckSensoryData::GRID_SIZE - 1);
        const int gapTop = std::clamp(
            static_cast<int>(std::lround(
                pipeTopNormalized * static_cast<float>(DuckSensoryData::GRID_SIZE - 1))),
            0,
            DuckSensoryData::GRID_SIZE - 1);
        const int gapBottom = std::clamp(
            static_cast<int>(std::lround(
                pipeBottomNormalized * static_cast<float>(DuckSensoryData::GRID_SIZE - 1))),
            gapTop,
            DuckSensoryData::GRID_SIZE - 1);
        for (int pipeColumn = pipeX;
             pipeColumn <= std::min(pipeX + 1, DuckSensoryData::GRID_SIZE - 1);
             ++pipeColumn) {
            for (int y = 0; y < DuckSensoryData::GRID_SIZE; ++y) {
                if (y >= gapTop && y <= gapBottom) {
                    continue;
                }
                setDominantMaterial(pipeColumn, y, Material::EnumType::Wall);
            }
        }

        const float birdVelocityNormalized = std::clamp(
            input.policyInputs[static_cast<size_t>(
                NesDuckSpecialSenseLayout::BirdVelocityNormalized)],
            -1.0f,
            1.0f);
        sensory.velocity.x = 0.0;
        sensory.velocity.y = static_cast<double>(birdVelocityNormalized * kNesDuckVelocityScale);

        sensory.special_senses.fill(0.0);
        if (extractor_.has_value() && extractor_->isSupported()) {
            mapFlappyParatroopaFeaturesToSpecialSenses(input.policyInputs, sensory.special_senses);
        }

        if ((input.controllerMask & NesPolicyLayout::ButtonLeft) != 0u) {
            sensory.facing_x = -1.0f;
            sensory.velocity.x = -1.0;
        }
        else if ((input.controllerMask & NesPolicyLayout::ButtonRight) != 0u) {
            sensory.facing_x = 1.0f;
            sensory.velocity.x = 1.0;
        }
        else {
            sensory.facing_x = 1.0f;
        }

        sensory.on_ground =
            input.lastGameState.has_value() && input.lastGameState.value() == kNesStateWaiting;
        return sensory;
    }

private:
    std::optional<NesRomProfileExtractor> extractor_ = std::nullopt;
    std::optional<NesFlappyBirdEvaluator> evaluator_ = std::nullopt;
    uint32_t startPulseFrameCounter_ = 0;
    uint32_t waitingFlapPulseFrameCounter_ = 0;
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesFlappyParatroopaGameAdapter()
{
    return std::make_unique<NesFlappyParatroopaGameAdapter>();
}

} // namespace DirtSim
