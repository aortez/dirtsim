#include "core/scenarios/nes/NesTileSensoryData.h"

#include "core/organisms/evolution/NesPolicyLayout.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

size_t relativeTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::RelativeTileColumns + column;
}

} // namespace

TEST(NesTileSensoryDataTest, DefaultsAreNeutral)
{
    const NesTileSensoryData sensory;

    EXPECT_EQ(sensory.tileFrame.frameId, 0u);
    EXPECT_EQ(sensory.facingX, 0.0f);
    EXPECT_EQ(sensory.selfViewX, 0.5f);
    EXPECT_EQ(sensory.selfViewY, 0.5f);
    EXPECT_EQ(sensory.previousControlX, 0.0f);
    EXPECT_EQ(sensory.previousControlY, 0.0f);
    EXPECT_FALSE(sensory.previousA);
    EXPECT_FALSE(sensory.previousB);
    EXPECT_EQ(sensory.energy, 1.0f);
    EXPECT_EQ(sensory.health, 1.0f);
    EXPECT_EQ(sensory.deltaTimeSeconds, 0.0);
    for (double sense : sensory.specialSenses) {
        EXPECT_EQ(sense, 0.0);
    }
}

TEST(NesTileSensoryDataTest, ControllerMaskMapsDirectionalButtonsAndAB)
{
    NesTileSensoryData sensory;

    setNesTilePreviousControlFromControllerMask(
        sensory,
        NesPolicyLayout::ButtonLeft | NesPolicyLayout::ButtonUp | NesPolicyLayout::ButtonA);

    EXPECT_EQ(sensory.previousControlX, -1.0f);
    EXPECT_EQ(sensory.previousControlY, -1.0f);
    EXPECT_TRUE(sensory.previousA);
    EXPECT_FALSE(sensory.previousB);

    setNesTilePreviousControlFromControllerMask(
        sensory,
        NesPolicyLayout::ButtonRight | NesPolicyLayout::ButtonDown | NesPolicyLayout::ButtonB);

    EXPECT_EQ(sensory.previousControlX, 1.0f);
    EXPECT_EQ(sensory.previousControlY, 1.0f);
    EXPECT_FALSE(sensory.previousA);
    EXPECT_TRUE(sensory.previousB);
}

TEST(NesTileSensoryDataTest, ConflictingDirectionsResetAxesToZero)
{
    NesTileSensoryData sensory;
    sensory.previousControlX = 1.0f;
    sensory.previousControlY = -1.0f;

    setNesTilePreviousControlFromControllerMask(
        sensory,
        NesPolicyLayout::ButtonLeft | NesPolicyLayout::ButtonRight | NesPolicyLayout::ButtonUp
            | NesPolicyLayout::ButtonDown);

    EXPECT_EQ(sensory.previousControlX, 0.0f);
    EXPECT_EQ(sensory.previousControlY, 0.0f);
    EXPECT_FALSE(sensory.previousA);
    EXPECT_FALSE(sensory.previousB);
}

TEST(NesTileSensoryDataTest, TileFrameIsStoredWithoutHistogramPayload)
{
    NesTileSensoryData sensory;
    sensory.tileFrame.frameId = 123u;
    sensory.tileFrame.playerScreenX = 40;
    sensory.tileFrame.playerScreenY = 80;
    sensory.tileFrame.playerTileColumn = 5;
    sensory.tileFrame.playerTileRow = 10;
    sensory.tileFrame.tokens[relativeTileIndex(
        NesPlayerRelativeTileFrame::AnchorTileColumn, NesPlayerRelativeTileFrame::AnchorTileRow)] =
        42u;

    EXPECT_EQ(sensory.tileFrame.frameId, 123u);
    EXPECT_EQ(sensory.tileFrame.playerScreenX, 40);
    EXPECT_EQ(sensory.tileFrame.playerScreenY, 80);
    EXPECT_EQ(sensory.tileFrame.playerTileColumn, 5);
    EXPECT_EQ(sensory.tileFrame.playerTileRow, 10);
    EXPECT_EQ(
        sensory.tileFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        42u);
}

TEST(NesTileSensoryDataTest, StoresRamDerivedScalarSenses)
{
    NesTileSensoryData sensory;
    sensory.facingX = -1.0f;
    sensory.selfViewX = 0.25f;
    sensory.selfViewY = 0.75f;
    sensory.specialSenses[0u] = 0.125;
    sensory.specialSenses[17u] = -1.0;
    sensory.energy = 0.5f;
    sensory.health = 0.25f;
    sensory.deltaTimeSeconds = 1.0 / 60.0;

    EXPECT_EQ(sensory.facingX, -1.0f);
    EXPECT_EQ(sensory.selfViewX, 0.25f);
    EXPECT_EQ(sensory.selfViewY, 0.75f);
    EXPECT_EQ(sensory.specialSenses[0u], 0.125);
    EXPECT_EQ(sensory.specialSenses[17u], -1.0);
    EXPECT_EQ(sensory.energy, 0.5f);
    EXPECT_EQ(sensory.health, 0.25f);
    EXPECT_DOUBLE_EQ(sensory.deltaTimeSeconds, 1.0 / 60.0);
}
