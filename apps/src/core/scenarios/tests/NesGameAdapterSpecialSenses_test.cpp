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
