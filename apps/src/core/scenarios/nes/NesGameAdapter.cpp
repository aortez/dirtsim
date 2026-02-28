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
        lastPlayerADamages_.reset();
        lastPlayerBDamages_.reset();
        lastPlayerAStocks_.reset();
        lastPlayerBStocks_.reset();
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

        if (!input.memorySnapshot.has_value()) {
            return output;
        }

        const SmolnesRuntime::MemorySnapshot& snapshot = input.memorySnapshot.value();
        const uint8_t playerADamages = snapshot.cpuRam[kPlayerADamagesAddr];
        const uint8_t playerBDamages = snapshot.cpuRam[kPlayerBDamagesAddr];
        const uint8_t playerAStocks = snapshot.cpuRam[kPlayerAStocksAddr];
        const uint8_t playerBStocks = snapshot.cpuRam[kPlayerBStocksAddr];

        const bool stocksLookValid = playerAStocks <= kMaxStocksInMatch
            && playerBStocks <= kMaxStocksInMatch && !(playerAStocks == 0u && playerBStocks == 0u);
        const bool inMatch = advancedFrameCount_ >= kSetupScriptEndFrame && stocksLookValid;
        output.gameState = inMatch ? std::optional<uint8_t>(1u) : std::optional<uint8_t>(0u);
        if (!inMatch) {
            lastPlayerADamages_.reset();
            lastPlayerBDamages_.reset();
            lastPlayerAStocks_.reset();
            lastPlayerBStocks_.reset();
            return output;
        }

        if (playerAStocks == 0u || playerBStocks == 0u) {
            output.done = true;
        }

        if (!lastPlayerADamages_.has_value() || !lastPlayerBDamages_.has_value()
            || !lastPlayerAStocks_.has_value() || !lastPlayerBStocks_.has_value()) {
            lastPlayerADamages_ = playerADamages;
            lastPlayerBDamages_ = playerBDamages;
            lastPlayerAStocks_ = playerAStocks;
            lastPlayerBStocks_ = playerBStocks;
            return output;
        }

        const uint8_t prevPlayerADamages = lastPlayerADamages_.value();
        const uint8_t prevPlayerBDamages = lastPlayerBDamages_.value();
        const uint8_t prevPlayerAStocks = lastPlayerAStocks_.value();
        const uint8_t prevPlayerBStocks = lastPlayerBStocks_.value();

        const int playerAStockLoss =
            std::max(0, static_cast<int>(prevPlayerAStocks) - static_cast<int>(playerAStocks));
        const int playerBStockLoss =
            std::max(0, static_cast<int>(prevPlayerBStocks) - static_cast<int>(playerBStocks));

        output.rewardDelta += kStockReward * static_cast<double>(playerBStockLoss);
        output.rewardDelta -= kStockReward * static_cast<double>(playerAStockLoss);

        if (playerAStockLoss == 0 && playerBStockLoss == 0) {
            const int playerADamageGain = std::max(
                0, static_cast<int>(playerADamages) - static_cast<int>(prevPlayerADamages));
            const int playerBDamageGain = std::max(
                0, static_cast<int>(playerBDamages) - static_cast<int>(prevPlayerBDamages));

            output.rewardDelta += kDamageReward * static_cast<double>(playerBDamageGain);
            output.rewardDelta -= kDamageReward * static_cast<double>(playerADamageGain);
        }

        lastPlayerADamages_ = playerADamages;
        lastPlayerBDamages_ = playerBDamages;
        lastPlayerAStocks_ = playerAStocks;
        lastPlayerBStocks_ = playerBStocks;
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
    static constexpr uint64_t kSetupScriptEndFrame = 1200;
    static constexpr size_t kPlayerADamagesAddr = 0x48;
    static constexpr size_t kPlayerBDamagesAddr = 0x49;
    static constexpr size_t kPlayerAStocksAddr = 0x54;
    static constexpr size_t kPlayerBStocksAddr = 0x55;
    static constexpr uint8_t kMaxStocksInMatch = 5u;

    static constexpr double kDamageReward = 1.0;
    static constexpr double kStockReward = 600.0;

    static uint8_t scriptedSetupMaskForFrame(uint64_t frameIndex)
    {
        constexpr uint64_t kStartPressWidthFrames = 1;
        constexpr std::array<uint64_t, 6> kStartPressFrames = {
            120u, 240u, 360u, 480u, 1000u, 1120u
        };
        for (const uint64_t pressFrame : kStartPressFrames) {
            if (frameIndex >= pressFrame && frameIndex < (pressFrame + kStartPressWidthFrames)) {
                return NesPolicyLayout::ButtonStart;
            }
        }

        return 0u;
    }

    NesPaletteClusterer paletteClusterer_;
    uint64_t advancedFrameCount_ = 0;
    std::optional<uint8_t> lastPlayerADamages_ = std::nullopt;
    std::optional<uint8_t> lastPlayerBDamages_ = std::nullopt;
    std::optional<uint8_t> lastPlayerAStocks_ = std::nullopt;
    std::optional<uint8_t> lastPlayerBStocks_ = std::nullopt;
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
