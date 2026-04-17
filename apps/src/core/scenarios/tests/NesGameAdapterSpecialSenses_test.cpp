#include "core/scenarios/nes/NesGameAdapter.h"

#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/NesFlappyParatroopaRamExtractor.h"
#include "core/scenarios/nes/NesSuperMarioBrosSpecialSenses.h"
#include "core/scenarios/nes/NesSuperMarioBrosTilePosition.h"
#include "core/scenarios/nes/NesTileSensoryBuilder.h"
#include "core/scenarios/nes/NesTileTokenizer.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>

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

size_t relativeTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::RelativeTileColumns + column;
}

size_t screenTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesTileFrame::VisibleTileColumns + column;
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
    EXPECT_NEAR(sensory.self_view_x, 64.0 / 256.0, 1e-6);
    EXPECT_NEAR(sensory.self_view_y, (100.0 + 8.0) / 240.0, 1e-6);
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
    EXPECT_NEAR(sensory.self_view_x, 0.5, 1e-6);
    EXPECT_NEAR(sensory.self_view_y, 0.5, 1e-6);
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
    EXPECT_NEAR(
        sensory.special_senses[SmbSpecialSenseIndex::StageProgress],
        (1.0 * 4.0 + 2.0) / 32.0,
        1e-6);
    EXPECT_NEAR(
        sensory.special_senses[SmbSpecialSenseIndex::AbsoluteX],
        static_cast<double>(absoluteX) / 4096.0,
        1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::HorizontalSpeed], 25.0 / 40.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::VerticalSpeed], -40.0 / 128.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::Powerup], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::Airborne], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::PlayerYScreen], 120.0 / 240.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::Lives], 3.0 / 9.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::PlayerXScreen], 128.0 / 255.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::NearestEnemyDx], 16.0 / 255.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::NearestEnemyDy], -10.0 / 240.0, 1e-6);
    EXPECT_NEAR(
        sensory.special_senses[SmbSpecialSenseIndex::SecondNearestEnemyDx], -48.0 / 255.0, 1e-6);
    EXPECT_NEAR(
        sensory.special_senses[SmbSpecialSenseIndex::SecondNearestEnemyDy], -20.0 / 240.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::EnemyPresent], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::SecondEnemyPresent], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::World], 1.0 / 7.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::Level], 2.0 / 3.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::MovementX], 1.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::AbsoluteXTilePhase8], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::PlayerYTilePhase8], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::AbsoluteXTilePhase16], 0.0, 1e-6);
    EXPECT_NEAR(sensory.special_senses[SmbSpecialSenseIndex::PlayerYTilePhase16], 8.0 / 15.0, 1e-6);
    EXPECT_NEAR(sensory.self_view_x, 128.0 / 256.0, 1e-6);
    EXPECT_NEAR(sensory.self_view_y, 120.0 / 240.0, 1e-6);

    for (int i = 22; i < DuckSensoryData::SPECIAL_SENSE_COUNT; ++i) {
        EXPECT_EQ(sensory.special_senses[i], 0.0) << "slot " << i << " should be zero";
    }
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosSpecialSensesExposeSubTilePhase)
{
    NesSuperMarioBrosState state;
    state.absoluteX = 0x012Du;
    state.playerYScreen = 123u;

    const SmbSpecialSenses senses = makeNesSuperMarioBrosSpecialSenses(state);

    EXPECT_NEAR(senses[SmbSpecialSenseIndex::AbsoluteXTilePhase8], 5.0 / 7.0, 1e-6);
    EXPECT_NEAR(senses[SmbSpecialSenseIndex::PlayerYTilePhase8], 3.0 / 7.0, 1e-6);
    EXPECT_NEAR(senses[SmbSpecialSenseIndex::AbsoluteXTilePhase16], 13.0 / 15.0, 1e-6);
    EXPECT_NEAR(senses[SmbSpecialSenseIndex::PlayerYTilePhase16], 11.0 / 15.0, 1e-6);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterBuildsTileSensoryInput)
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

    const NesGameAdapterFrameInput frameInput{
        .advancedFrames = 400,
        .controllerMask = 0,
        .paletteFrame = nullptr,
        .memorySnapshot = snapshot,
    };
    (void)adapter->evaluateFrame(frameInput);

    const uint8_t controllerMask = NesPolicyLayout::ButtonRight | NesPolicyLayout::ButtonA;
    const NesGameAdapterSensoryInput sensoryInput{
        .controllerMask = controllerMask,
        .paletteFrame = nullptr,
        .lastGameState = std::nullopt,
        .deltaTimeSeconds = 0.016,
    };
    const NesTileSensoryBuilderInput tileInput =
        adapter->makeNesTileSensoryBuilderInput(sensoryInput);

    EXPECT_EQ(tileInput.playerScreenX, 128);
    EXPECT_EQ(tileInput.playerScreenY, 120 - static_cast<int16_t>(NesTileFrame::TopCropPixels));
    EXPECT_FLOAT_EQ(tileInput.facingX, 1.0f);
    EXPECT_NEAR(tileInput.selfViewX, 128.0 / 256.0, 1e-6);
    EXPECT_NEAR(tileInput.selfViewY, 120.0 / 240.0, 1e-6);
    EXPECT_EQ(tileInput.controllerMask, controllerMask);
    EXPECT_NEAR(
        tileInput.specialSenses[SmbSpecialSenseIndex::StageProgress],
        (1.0 * 4.0 + 2.0) / 32.0,
        1e-6);
    EXPECT_NEAR(tileInput.specialSenses[SmbSpecialSenseIndex::MovementX], 1.0, 1e-6);
    EXPECT_NEAR(
        tileInput.specialSenses[SmbSpecialSenseIndex::PlayerYTilePhase16], 8.0 / 15.0, 1e-6);
    EXPECT_DOUBLE_EQ(tileInput.deltaTimeSeconds, 0.016);

    NesTileTokenizer tokenizer;
    const auto buildResult =
        tokenizer.buildVocabulary(std::vector<NesTileTokenizer::TilePatternHash>{ 10u, 20u });
    ASSERT_TRUE(buildResult.isValue()) << buildResult.errorValue();
    tokenizer.freeze();

    NesTileFrame tileFrame;
    tileFrame.tilePatternHashes.fill(20u);
    tileFrame.tilePatternHashes[screenTileIndex(16u, 14u)] = 10u;

    const auto sensoryResult = makeNesTileSensoryDataFromTileFrame(tileFrame, tokenizer, tileInput);

    ASSERT_TRUE(sensoryResult.isValue()) << sensoryResult.errorValue();
    const NesTileSensoryData& sensory = sensoryResult.value();
    EXPECT_EQ(sensory.tileFrame.playerTileColumn, 16);
    EXPECT_EQ(sensory.tileFrame.playerTileRow, 14);
    EXPECT_EQ(
        sensory.tileFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        1u);
    EXPECT_FLOAT_EQ(sensory.previousControlX, 1.0f);
    EXPECT_TRUE(sensory.previousA);
    EXPECT_FALSE(sensory.previousB);
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosTilePositionWrapsAbsoluteXAgainstTileScroll)
{
    NesSuperMarioBrosState state;
    state.playerYScreen = 120u;

    state.absoluteX = 0x0180u;
    EXPECT_EQ(makeNesSuperMarioBrosPlayerTileScreenX(state, 0x0100u), 128);

    state.absoluteX = 0x0208u;
    EXPECT_EQ(makeNesSuperMarioBrosPlayerTileScreenX(state, 0x0188u), 128);

    state.absoluteX = 0x0108u;
    EXPECT_EQ(makeNesSuperMarioBrosPlayerTileScreenX(state, 0x0188u), -128);

    EXPECT_EQ(
        makeNesSuperMarioBrosPlayerTileScreenY(state),
        120 - static_cast<int16_t>(NesTileFrame::TopCropPixels));
}

TEST(NesGameAdapterSpecialSensesTest, SuperMarioBrosAdapterBuildsTilePositionFromTileScroll)
{
    std::unique_ptr<NesGameAdapter> adapter = createNesSuperMarioBrosGameAdapter();
    ASSERT_NE(adapter, nullptr);
    adapter->reset("smb");

    SmolnesRuntime::MemorySnapshot snapshot = makeSmbSnapshot(
        1,
        2,
        0x02,
        0x20,
        25,
        static_cast<uint8_t>(static_cast<int8_t>(-40)),
        120,
        2,
        0x08,
        0x01,
        3,
        1);

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
        .tileFrameScrollX = 0x01A0u,
    };
    const NesTileSensoryBuilderInput tileInput =
        adapter->makeNesTileSensoryBuilderInput(sensoryInput);

    EXPECT_EQ(tileInput.playerScreenX, 128);
    EXPECT_EQ(tileInput.playerScreenY, 120 - static_cast<int16_t>(NesTileFrame::TopCropPixels));
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
