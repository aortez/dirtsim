#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"

#include <algorithm>

namespace DirtSim {

namespace {
constexpr int16_t kDefaultTilePlayerScreenX = 128;
constexpr int16_t kDefaultTilePlayerScreenY =
    120 - static_cast<int16_t>(NesTileFrame::TopCropPixels);
constexpr uint64_t kSetupScriptEndFrame = 1200;
constexpr size_t kPlayerADamagesAddr = 0x48;
constexpr size_t kPlayerBDamagesAddr = 0x49;
constexpr size_t kPlayerAStocksAddr = 0x54;
constexpr size_t kPlayerBStocksAddr = 0x55;
constexpr uint8_t kStbMaxStocks = 5u;

double normalizeStbStocks(uint8_t stocks)
{
    return std::clamp(static_cast<double>(stocks) / static_cast<double>(kStbMaxStocks), 0.0, 1.0);
}

double normalizeStbDamage(uint8_t damage)
{
    return std::clamp(static_cast<double>(damage) / 255.0, 0.0, 1.0);
}

std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> makeStbSpecialSenses(
    uint8_t playerAStocks, uint8_t playerBStocks, uint8_t playerADamages, uint8_t playerBDamages)
{
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> senses{};
    senses.fill(0.0);

    senses[0] = normalizeStbStocks(playerAStocks);
    senses[1] = normalizeStbStocks(playerBStocks);
    senses[2] = normalizeStbDamage(playerADamages);
    senses[3] = normalizeStbDamage(playerBDamages);
    return senses;
}

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
        cachedSpecialSenses_.fill(0.0);
        cachedSelfViewX_ = 0.5f;
        cachedSelfViewY_ = 0.5f;
        cachedTilePlayerScreenX_ = kDefaultTilePlayerScreenX;
        cachedTilePlayerScreenY_ = kDefaultTilePlayerScreenY;
    }

    NesGameAdapterControllerOutput resolveControllerMask(
        const NesGameAdapterControllerInput& input) override
    {
        NesGameAdapterControllerOutput output;
        if (advancedFrameCount_ < kSetupScriptEndFrame) {
            output.resolvedControllerMask = scriptedSetupMaskForFrame(advancedFrameCount_);
            output.source = NesGameAdapterControllerSource::ScriptedSetup;
            output.sourceFrameIndex = advancedFrameCount_;
            return output;
        }

        output.resolvedControllerMask = input.inferredControllerMask;
        return output;
    }

    NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) override
    {
        if (input.paletteFrame != nullptr) {
            paletteClusterer_.observeFrame(*input.paletteFrame);
        }

        advancedFrameCount_ += input.advancedFrames;
        cachedSpecialSenses_.fill(0.0);
        cachedSelfViewX_ = 0.5f;
        cachedSelfViewY_ = 0.5f;
        cachedTilePlayerScreenX_ = kDefaultTilePlayerScreenX;
        cachedTilePlayerScreenY_ = kDefaultTilePlayerScreenY;

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

        const bool stocksLookValid = playerAStocks <= kStbMaxStocks
            && playerBStocks <= kStbMaxStocks && !(playerAStocks == 0u && playerBStocks == 0u);
        const bool inMatch = advancedFrameCount_ >= kSetupScriptEndFrame && stocksLookValid;
        output.gameState = inMatch ? std::optional<uint8_t>(1u) : std::optional<uint8_t>(0u);
        if (!inMatch) {
            lastPlayerADamages_.reset();
            lastPlayerBDamages_.reset();
            lastPlayerAStocks_.reset();
            lastPlayerBStocks_.reset();
            return output;
        }

        cachedSpecialSenses_ =
            makeStbSpecialSenses(playerAStocks, playerBStocks, playerADamages, playerBDamages);

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
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> cachedSpecialSenses_{};
    float cachedSelfViewX_ = 0.5f;
    float cachedSelfViewY_ = 0.5f;
    int16_t cachedTilePlayerScreenX_ = kDefaultTilePlayerScreenX;
    int16_t cachedTilePlayerScreenY_ = kDefaultTilePlayerScreenY;
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesSuperTiltBroGameAdapter()
{
    return std::make_unique<NesSuperTiltBroGameAdapter>();
}

} // namespace DirtSim
