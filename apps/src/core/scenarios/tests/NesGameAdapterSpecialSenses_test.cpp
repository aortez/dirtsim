#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesFlappyParatroopaRamExtractor.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <algorithm>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {
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
    uint8_t facingDirection,
    uint8_t verticalSpeed,
    uint8_t playerYScreen,
    uint8_t powerupState,
    uint8_t playerState,
    uint8_t lives,
    uint8_t gameEngine)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x0770] = gameEngine;
    snapshot.cpuRam[0x000E] = playerState;
    snapshot.cpuRam[0x0086] = playerXScreen;
    snapshot.cpuRam[0x006D] = playerXPage;
    snapshot.cpuRam[0x075A] = lives;
    snapshot.cpuRam[0x075F] = world;
    snapshot.cpuRam[0x0760] = level;
    snapshot.cpuRam[0x0057] = horizontalSpeed;
    snapshot.cpuRam[0x0700] = facingDirection;
    snapshot.cpuRam[0x009F] = verticalSpeed;
    snapshot.cpuRam[0x00CE] = playerYScreen;
    snapshot.cpuRam[0x0756] = powerupState;

    return snapshot;
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

    const SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        1, 2, 0x03, 0x80, 25, 1, static_cast<uint8_t>(static_cast<int8_t>(-40)), 120, 2, 2, 3, 1);

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

    EXPECT_NEAR(sensory.special_senses[0], (1.0 * 4.0 + 2.0) / 32.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[1], static_cast<double>(absoluteX) / 4096.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[2], 1.0 * 25.0 / 40.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[3], -40.0 / 128.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[4], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[5], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[6], 120.0 / 240.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[7], 3.0 / 9.0, 1e-6);

    for (int i = 8; i < DuckSensoryData::SPECIAL_SENSE_COUNT; ++i) {
        EXPECT_EQ(sensory.special_senses[i], 0.0) << "slot " << i << " should be zero";
    }
}
