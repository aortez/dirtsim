#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/scenarios/nes/NesDuckSensoryBuilder.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"

#include <algorithm>
#include <cstdint>

namespace DirtSim {

namespace {

constexpr uint64_t kSetupScriptEndFrame = 300;

constexpr size_t kFacingDirectionAddr = 0x0700;
constexpr size_t kGameEngineSubroutineAddr = 0x0770;
constexpr size_t kHorizontalSpeedAddr = 0x0057;
constexpr size_t kLevelAddr = 0x0760;
constexpr size_t kLivesAddr = 0x075A;
constexpr size_t kPlayerStateAddr = 0x000E;
constexpr size_t kPlayerXPageAddr = 0x006D;
constexpr size_t kPlayerXScreenAddr = 0x0086;
constexpr size_t kPlayerYScreenAddr = 0x00CE;
constexpr size_t kPowerupStateAddr = 0x0756;
constexpr size_t kScoreBcdAddr = 0x07DD;
constexpr size_t kScoreBcdDigits = 6;
constexpr size_t kVerticalSpeedAddr = 0x009F;
constexpr size_t kWorldAddr = 0x075F;

constexpr uint8_t kFacingLeft = 2;
constexpr uint8_t kGameEngineGameplay = 1;
constexpr uint8_t kPlayerStateDying = 0x06;
constexpr uint8_t kPlayerStateDead = 0x0B;

uint32_t decodeBcdScore(const std::array<uint8_t, SMOLNES_RUNTIME_CPU_RAM_BYTES>& cpuRam)
{
    uint32_t score = 0;
    for (size_t i = 0; i < kScoreBcdDigits; ++i) {
        const uint8_t digit = cpuRam[kScoreBcdAddr + i] & 0x0F;
        score = score * 10 + digit;
    }
    return score;
}

double normalizeSmb(double value, double maxValue)
{
    return std::clamp(value / maxValue, 0.0, 1.0);
}

std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> makeSmbSpecialSenses(
    uint8_t world,
    uint8_t level,
    uint16_t playerAbsoluteX,
    uint8_t horizontalSpeed,
    uint8_t facingDirection,
    uint8_t verticalSpeed,
    uint8_t playerYScreen,
    uint8_t powerupState,
    uint8_t playerState,
    uint8_t lives)
{
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> senses{};
    senses.fill(0.0);

    const double progress = (static_cast<double>(world) * 4.0 + static_cast<double>(level)) / 32.0;
    senses[0] = std::clamp(progress, 0.0, 1.0);
    senses[1] = normalizeSmb(static_cast<double>(playerAbsoluteX), 4096.0);

    const double sign = (facingDirection == kFacingLeft) ? -1.0 : 1.0;
    senses[2] = std::clamp(sign * static_cast<double>(horizontalSpeed) / 40.0, -1.0, 1.0);

    senses[3] =
        std::clamp(static_cast<double>(static_cast<int8_t>(verticalSpeed)) / 128.0, -1.0, 1.0);

    if (powerupState >= 2) {
        senses[4] = 1.0;
    }
    else if (powerupState == 1) {
        senses[4] = 0.5;
    }

    const bool airborne = (playerState >= 1 && playerState <= 3);
    senses[5] = airborne ? 1.0 : 0.0;
    senses[6] = normalizeSmb(static_cast<double>(playerYScreen), 240.0);
    senses[7] = normalizeSmb(static_cast<double>(lives), 9.0);

    return senses;
}

class NesSuperMarioBrosGameAdapter final : public NesGameAdapter {
public:
    void reset(const std::string& runtimeRomId) override
    {
        paletteClusterer_.reset(runtimeRomId);
        advancedFrameCount_ = 0;
        lastAbsoluteX_ = 0;
        lastLives_.reset();
        lastScore_ = 0;
        lastWorld_ = 0;
        lastLevel_ = 0;
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
        output.rewardDelta += static_cast<double>(input.advancedFrames);

        if (!input.memorySnapshot.has_value()) {
            return output;
        }

        const SmolnesRuntime::MemorySnapshot& snapshot = input.memorySnapshot.value();
        const uint8_t gameEngine = snapshot.cpuRam[kGameEngineSubroutineAddr];
        const uint8_t playerState = snapshot.cpuRam[kPlayerStateAddr];
        const uint8_t playerXScreen = snapshot.cpuRam[kPlayerXScreenAddr];
        const uint8_t playerXPage = snapshot.cpuRam[kPlayerXPageAddr];
        const uint8_t horizontalSpeed = snapshot.cpuRam[kHorizontalSpeedAddr];
        const uint8_t facingDirection = snapshot.cpuRam[kFacingDirectionAddr];
        const uint8_t verticalSpeed = snapshot.cpuRam[kVerticalSpeedAddr];
        const uint8_t playerYScreen = snapshot.cpuRam[kPlayerYScreenAddr];
        const uint8_t powerupState = snapshot.cpuRam[kPowerupStateAddr];
        const uint8_t lives = snapshot.cpuRam[kLivesAddr];
        const uint8_t world = snapshot.cpuRam[kWorldAddr];
        const uint8_t level = snapshot.cpuRam[kLevelAddr];

        const bool inGameplay =
            advancedFrameCount_ >= kSetupScriptEndFrame && gameEngine == kGameEngineGameplay;
        output.gameState = inGameplay ? std::optional<uint8_t>(1u) : std::optional<uint8_t>(0u);

        if (!inGameplay) {
            return output;
        }

        const uint16_t absoluteX =
            (static_cast<uint16_t>(playerXPage) << 8) | static_cast<uint16_t>(playerXScreen);

        cachedSpecialSenses_ = makeSmbSpecialSenses(
            world,
            level,
            absoluteX,
            horizontalSpeed,
            facingDirection,
            verticalSpeed,
            playerYScreen,
            powerupState,
            playerState,
            lives);

        const bool isDying = playerState >= kPlayerStateDying;

        const uint32_t currentScore = decodeBcdScore(snapshot.cpuRam);
        if (currentScore > lastScore_) {
            output.rewardDelta += kScoreReward * static_cast<double>(currentScore - lastScore_);
        }
        lastScore_ = currentScore;

        if (!lastLives_.has_value()) {
            lastAbsoluteX_ = absoluteX;
            lastLives_ = lives;
            lastWorld_ = world;
            lastLevel_ = level;
            return output;
        }

        const uint8_t prevLives = lastLives_.value();

        if (!isDying) {
            const bool levelAdvanced =
                world > lastWorld_ || (world == lastWorld_ && level > lastLevel_);
            if (levelAdvanced) {
                output.rewardDelta += kLevelClearReward;
                lastAbsoluteX_ = 0;
            }
            else if (absoluteX > lastAbsoluteX_) {
                output.rewardDelta +=
                    kDistanceReward * static_cast<double>(absoluteX - lastAbsoluteX_);
            }
            lastAbsoluteX_ = absoluteX;
        }

        if (lives < prevLives) {
            output.rewardDelta -= kDeathPenalty;
            lastAbsoluteX_ = 0;
        }

        if (lives == 0 && playerState == kPlayerStateDead) {
            output.done = true;
        }

        lastLives_ = lives;
        lastWorld_ = world;
        lastLevel_ = level;
        return output;
    }

    DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const override
    {
        return makeNesDuckSensoryData(
            paletteClusterer_, input.paletteFrame, input.deltaTimeSeconds, cachedSpecialSenses_);
    }

private:
    static constexpr double kDistanceReward = 0.5;
    static constexpr double kDeathPenalty = 500.0;
    static constexpr double kLevelClearReward = 1000.0;
    static constexpr double kScoreReward = 0.025;

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
    uint64_t advancedFrameCount_ = 0;
    uint16_t lastAbsoluteX_ = 0;
    std::optional<uint8_t> lastLives_ = std::nullopt;
    uint32_t lastScore_ = 0;
    uint8_t lastWorld_ = 0;
    uint8_t lastLevel_ = 0;
    std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT> cachedSpecialSenses_{};
};

} // namespace

std::unique_ptr<NesGameAdapter> createNesSuperMarioBrosGameAdapter()
{
    return std::make_unique<NesSuperMarioBrosGameAdapter>();
}

} // namespace DirtSim
