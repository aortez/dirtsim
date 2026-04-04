#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesFlappyParatroopaRamExtractor.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {
constexpr size_t kEnemySlotCount = 5;
constexpr size_t kSmbFacingDirectionAddr = 0x0033;
constexpr size_t kSmbMovementDirectionAddr = 0x0045;
constexpr std::array<size_t, kEnemySlotCount> kEnemyActiveAddrs = {
    0x000F, 0x0010, 0x0011, 0x0012, 0x0013
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyTypeAddrs = {
    0x0016, 0x0017, 0x0018, 0x0019, 0x001A
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyXPageAddrs = {
    0x006E, 0x006F, 0x0070, 0x0071, 0x0072
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyXScreenAddrs = {
    0x0087, 0x0088, 0x0089, 0x008A, 0x008B
};
constexpr std::array<size_t, kEnemySlotCount> kEnemyYScreenAddrs = {
    0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3
};

SmolnesRuntime::MemorySnapshot makeFlappySnapshot()
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x0A] = 1; // Waiting.
    snapshot.cpuRam[0x00] = 0;
    snapshot.cpuRam[0x01] = 100;
    snapshot.cpuRam[0x02] = 0;
    snapshot.cpuRam[0x03] = 0;
    snapshot.cpuRam[0x08] = 16;
    snapshot.cpuRam[0x09] = 0;
    snapshot.cpuRam[0x12] = 120;
    snapshot.cpuRam[0x13] = 140;
    snapshot.cpuRam[0x14] = 160;
    snapshot.cpuRam[0x15] = 180;
    snapshot.cpuRam[0x19] = 3;
    snapshot.cpuRam[0x1A] = 2;
    snapshot.cpuRam[0x1B] = 1;
    return snapshot;
}

SmolnesRuntime::MemorySnapshot makeStbSnapshot(
    uint8_t playerAStocks, uint8_t playerBStocks, uint8_t playerADamages, uint8_t playerBDamages)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x48] = playerADamages;
    snapshot.cpuRam[0x49] = playerBDamages;
    snapshot.cpuRam[0x54] = playerAStocks;
    snapshot.cpuRam[0x55] = playerBStocks;
    return snapshot;
}
SmolnesRuntime::MemorySnapshot makeSmbSnapshot(
    uint8_t world,
    uint8_t level,
    uint8_t playerXPage,
    uint8_t playerXScreen,
    uint8_t horizontalSpeed,
    uint8_t verticalSpeed,
    uint8_t playerYScreen,
    uint8_t powerupState,
    uint8_t playerState,
    uint8_t playerFloatState,
    uint8_t lives,
    uint8_t gameEngine)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x0770] = gameEngine;
    snapshot.cpuRam[0x000E] = playerState;
    snapshot.cpuRam[0x001D] = playerFloatState;
    snapshot.cpuRam[0x0086] = playerXScreen;
    snapshot.cpuRam[0x006D] = playerXPage;
    snapshot.cpuRam[0x075A] = lives;
    snapshot.cpuRam[0x075F] = world;
    snapshot.cpuRam[0x0760] = level;
    snapshot.cpuRam[0x0057] = horizontalSpeed;
    snapshot.cpuRam[0x009F] = verticalSpeed;
    snapshot.cpuRam[0x00CE] = playerYScreen;
    snapshot.cpuRam[0x0756] = powerupState;

    return snapshot;
}

void setEnemySlot(
    SmolnesRuntime::MemorySnapshot& snapshot,
    size_t slot,
    uint8_t active,
    uint8_t type,
    uint8_t xPage,
    uint8_t xScreen,
    uint8_t yScreen)
{
    ASSERT_LT(slot, kEnemySlotCount);
    snapshot.cpuRam[kEnemyActiveAddrs[slot]] = active;
    snapshot.cpuRam[kEnemyTypeAddrs[slot]] = type;
    snapshot.cpuRam[kEnemyXPageAddrs[slot]] = xPage;
    snapshot.cpuRam[kEnemyXScreenAddrs[slot]] = xScreen;
    snapshot.cpuRam[kEnemyYScreenAddrs[slot]] = yScreen;
}
} // namespace

TEST(NesGameAdapterSpecialSensesTest, FlappyAdapterExposesCuratedSpecialSenses)
{
    const SmolnesRuntime::MemorySnapshot snapshot = makeFlappySnapshot();

    NesFlappyParatroopaRamExtractor extractor(NesPolicyLayout::FlappyParatroopaWorldUnlRomId);
    ASSERT_TRUE(extractor.isSupported());

    NesFlappyBirdEvaluator evaluator;
    evaluator.reset();

    const auto evaluatorInput = extractor.extract(snapshot, 0);
    ASSERT_TRUE(evaluatorInput.has_value());
    const NesFlappyBirdEvaluatorOutput evaluation = evaluator.evaluate(evaluatorInput.value());

    std::unique_ptr<NesGameAdapter> adapter = createNesFlappyParatroopaGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset(NesPolicyLayout::FlappyParatroopaWorldUnlRomId);

    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 1,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(sensoryInput);

    ASSERT_GE(evaluation.features.size(), static_cast<size_t>(NesPolicyLayout::InputCount));
    const float birdYNorm = evaluation.features.at(1);
    const float birdVelNorm = evaluation.features.at(2);
    const float scoreNorm = evaluation.features.at(10);
    const double scrollX = static_cast<double>(evaluation.features.at(7)) * 255.0;
    const double scrollNt = evaluation.features.at(8) >= 0.5f ? 1.0 : 0.0;
    const double scrollPosition = scrollX + (scrollNt * 256.0);
    const double progress = std::clamp(scrollPosition / 511.0, 0.0, 1.0);

    EXPECT_NEAR(sensory.special_senses[0], static_cast<double>(birdYNorm), 1e-6);
    EXPECT_NEAR(sensory.special_senses[1], static_cast<double>(birdVelNorm), 1e-6);
    EXPECT_NEAR(sensory.special_senses[2], static_cast<double>(scoreNorm), 1e-6);
    EXPECT_NEAR(sensory.special_senses[3], progress, 1e-6);
}

TEST(NesGameAdapterSpecialSensesTest, SuperTiltBroAdapterExposesCuratedSpecialSenses)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperTiltBroGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("any-rom-id");

    const SmolnesRuntime::MemorySnapshot snapshot = makeStbSnapshot(3, 1, 64, 200);
    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 1200,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(sensoryInput);

    EXPECT_NEAR(sensory.special_senses[0], 3.0 / 5.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[1], 1.0 / 5.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[2], 64.0 / 255.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[3], 200.0 / 255.0, 1e-6);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterExposesCuratedSpecialSenses)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        1,
        2,
        0x03,
        0x80,
        25,
        static_cast<uint8_t>(static_cast<int8_t>(-40)),
        120,
        2,
        0x08,
        0x01,
        3,
        1);
    snapshot.cpuRam[kSmbFacingDirectionAddr] = 1u;
    snapshot.cpuRam[kSmbMovementDirectionAddr] = 1u;
    setEnemySlot(snapshot, 0, 1, 6, 0x03, 0x90, 110);
    setEnemySlot(snapshot, 1, 1, 6, 0x03, 0x50, 100);

    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 400,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(sensoryInput);

    const uint16_t absoluteX = (static_cast<uint16_t>(0x03) << 8) | 0x80;

    EXPECT_NEAR(sensory.facing_x, 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[0], (1.0 * 4.0 + 2.0) / 32.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[1], static_cast<double>(absoluteX) / 4096.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[2], 1.0 * 25.0 / 40.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[3], -40.0 / 128.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[4], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[5], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[6], 120.0 / 240.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[7], 3.0 / 9.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[8], 128.0 / 255.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[9], 16.0 / 255.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[10], -10.0 / 240.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[11], -48.0 / 255.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[12], -20.0 / 240.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[13], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[14], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[15], 1.0 / 7.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[16], 2.0 / 3.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[17], 1.0, 1e-6);

    for (int i = 18; i < DuckSensoryData::SPECIAL_SENSE_COUNT; ++i) {
        EXPECT_EQ(sensory.special_senses[i], 0.0) << "slot " << i << " should be zero";
    }
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterExposesLeftFacingFromRam)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x01, 0x20, 0, 0, 120, 0, 0x08, 0x00, 3, 1);
    snapshot.cpuRam[kSmbFacingDirectionAddr] = 2u;
    snapshot.cpuRam[kSmbMovementDirectionAddr] = 2u;

    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 400,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(sensoryInput);

    EXPECT_NEAR(sensory.facing_x, -1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[17], -1.0, 1e-6);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterSeparatesFacingFromMovementDirection)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x01, 0x20, 0, 0, 120, 0, 0x08, 0x00, 3, 1);
    snapshot.cpuRam[kSmbFacingDirectionAddr] = 1u;
    snapshot.cpuRam[kSmbMovementDirectionAddr] = 2u;

    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 400,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(sensoryInput);

    EXPECT_NEAR(sensory.facing_x, 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[17], -1.0, 1e-6);
}

TEST(
    NesGameAdapterSpecialSensesTest,
    SuperMarioBrosAdapterMapsPreviousControllerMaskToControlChannels)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    const uint8_t controllerMask = NesPolicyLayout::ButtonA | NesPolicyLayout::ButtonB
        | NesPolicyLayout::ButtonRight | NesPolicyLayout::ButtonUp;
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(
        {
            .controllerMask = controllerMask,
            .paletteFrame = nullptr,
            .lastGameState = std::nullopt,
            .deltaTimeSeconds = 0.016,
        });

    EXPECT_FLOAT_EQ(sensory.previous_control_x, 1.0f);
    EXPECT_FLOAT_EQ(sensory.previous_control_y, -1.0f);
    EXPECT_TRUE(sensory.previous_jump);
    EXPECT_TRUE(sensory.previous_run);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterMarksMissingSecondEnemyExplicitly)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    SmolnesRuntime::MemorySnapshot snapshot =
        makeSmbSnapshot(0, 0, 0x01, 0x20, 0, 0, 120, 0, 0x08, 0x00, 3, 1);
    snapshot.cpuRam[kSmbMovementDirectionAddr] = 0u;
    setEnemySlot(snapshot, 0, 1, 6, 0x01, 0x50, 118);

    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 400,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const DuckSensoryData sensory = adapter->makeDuckSensoryData(sensoryInput);

    EXPECT_NEAR(sensory.facing_x, 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[13], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[14], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[15], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[16], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[17], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[11], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[12], 0.0, 1e-6);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterPressesStartOnlyOnceDuringSetup)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    const SmolnesRuntime::MemorySnapshot nonGameplaySnapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0x00, 3, 0);

    size_t startPressCount = 0u;
    std::optional<uint8_t> lastGameState = std::nullopt;
    for (uint64_t frameIndex = 0; frameIndex < 420u; ++frameIndex) {
        const NesGameAdapterControllerOutput controllerOutput = adapter->resolveControllerMask(
            {
                .inferredControllerMask = NesPolicyLayout::ButtonRight,
                .lastGameState = lastGameState,
            });
        if (controllerOutput.resolvedControllerMask == NesPolicyLayout::ButtonStart) {
            ++startPressCount;
            EXPECT_EQ(frameIndex, 120u);
        }

        const NesGameAdapterFrameOutput output = adapter->evaluateFrame(
            {
                .advancedFrames = 1,
                .controllerMask = controllerOutput.resolvedControllerMask,
                .paletteFrame = nullptr,
                .memorySnapshot = nonGameplaySnapshot,
            });
        lastGameState = output.gameState;
    }

    EXPECT_EQ(startPressCount, 1u);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterStopsSetupInputsAfterGameplayStarts)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    const SmolnesRuntime::MemorySnapshot gameplaySnapshot =
        makeSmbSnapshot(0, 0, 0x01, 0x80, 0, 0, 120, 0, 0x08, 0x00, 3, 1);

    const NesGameAdapterFrameOutput gameplayOutput = adapter->evaluateFrame(
        {
            .advancedFrames = 400,
            .controllerMask = 0,
            .paletteFrame = nullptr,
            .memorySnapshot = gameplaySnapshot,
        });
    ASSERT_EQ(gameplayOutput.gameState, std::optional<uint8_t>(1u));

    const NesGameAdapterControllerOutput controllerOutput = adapter->resolveControllerMask(
        {
            .inferredControllerMask = NesPolicyLayout::ButtonRight,
            .lastGameState = gameplayOutput.gameState,
        });

    EXPECT_EQ(controllerOutput.resolvedControllerMask, NesPolicyLayout::ButtonRight);
    EXPECT_EQ(controllerOutput.source, NesGameAdapterControllerSource::InferredPolicy);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterEndsEvalIfGameplayNeverStarts)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    const SmolnesRuntime::MemorySnapshot nonGameplaySnapshot =
        makeSmbSnapshot(0, 0, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0x00, 3, 0);

    NesGameAdapterFrameOutput output;
    for (int i = 0; i < 500; ++i) {
        const NesGameAdapterControllerOutput controllerOutput = adapter->resolveControllerMask(
            {
                .inferredControllerMask = 0,
                .lastGameState = output.gameState,
            });
        output = adapter->evaluateFrame(
            {
                .advancedFrames = 1,
                .controllerMask = controllerOutput.resolvedControllerMask,
                .paletteFrame = nullptr,
                .memorySnapshot = nonGameplaySnapshot,
            });
    }

    EXPECT_TRUE(output.done);
    EXPECT_EQ(output.gameState, std::optional<uint8_t>(0u));
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterExposesDebugState)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        1,
        2,
        0x03,
        0x80,
        25,
        static_cast<uint8_t>(static_cast<int8_t>(-40)),
        120,
        2,
        0x08,
        0x01,
        3,
        1);

    const NesGameAdapterFrameOutput output = adapter->evaluateFrame(
        {
            .advancedFrames = 400,
            .controllerMask = 0,
            .paletteFrame = nullptr,
            .memorySnapshot = snapshot,
        });

    ASSERT_TRUE(output.debugState.has_value());
    EXPECT_EQ(output.debugState->advancedFrameCount, std::optional<uint64_t>(400u));
    EXPECT_EQ(output.debugState->phase, std::optional<uint8_t>(1u));
    EXPECT_EQ(output.debugState->lifeState, std::optional<uint8_t>(0u));
    EXPECT_EQ(output.debugState->world, std::optional<uint8_t>(1u));
    EXPECT_EQ(output.debugState->level, std::optional<uint8_t>(2u));
    EXPECT_EQ(output.debugState->absoluteX, std::optional<uint16_t>(0x0380u));
    EXPECT_EQ(output.debugState->playerXScreen, std::optional<uint8_t>(0x80u));
    EXPECT_EQ(output.debugState->playerYScreen, std::optional<uint8_t>(120u));
    EXPECT_EQ(output.debugState->lives, std::optional<uint8_t>(3u));
    EXPECT_EQ(output.debugState->powerupState, std::optional<uint8_t>(2u));
    EXPECT_EQ(output.debugState->setupFailure, std::optional<bool>(false));
    EXPECT_EQ(output.debugState->setupScriptActive, std::optional<bool>(false));
}
