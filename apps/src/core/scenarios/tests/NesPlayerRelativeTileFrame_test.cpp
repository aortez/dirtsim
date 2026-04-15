#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

size_t relativeTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::RelativeTileColumns + column;
}

size_t screenTileIndex(uint16_t column, uint16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::ScreenTileColumns + column;
}

NesTileTokenizer::TileToken tokenAt(uint16_t column, uint16_t row)
{
    return static_cast<NesTileTokenizer::TileToken>(
        row * NesPlayerRelativeTileFrame::ScreenTileColumns + column + 1u);
}

NesTileTokenFrame makeScreenTokenFrame()
{
    NesTileTokenFrame frame{
        .frameId = 77u,
        .scrollX = 9u,
        .scrollY = 18u,
    };

    for (uint16_t row = 0; row < NesPlayerRelativeTileFrame::ScreenTileRows; ++row) {
        for (uint16_t column = 0; column < NesPlayerRelativeTileFrame::ScreenTileColumns;
             ++column) {
            frame.tokens[screenTileIndex(column, row)] = tokenAt(column, row);
        }
    }

    return frame;
}

size_t nonVoidTokenCount(const NesPlayerRelativeTileFrame& frame)
{
    return static_cast<size_t>(
        std::count_if(frame.tokens.begin(), frame.tokens.end(), [](auto token) {
            return token != NesTileTokenizer::VoidToken;
        }));
}

int16_t pixelForTile(uint16_t tile)
{
    return static_cast<int16_t>(tile * NesPlayerRelativeTileFrame::TileSizePixels + 4u);
}

} // namespace

TEST(NesPlayerRelativeTileFrameTest, DimensionsCoverEveryVisibleTileOffset)
{
    EXPECT_EQ(NesPlayerRelativeTileFrame::RelativeTileColumns, 63u);
    EXPECT_EQ(NesPlayerRelativeTileFrame::RelativeTileRows, 55u);
    EXPECT_EQ(NesPlayerRelativeTileFrame::AnchorTileColumn, 31u);
    EXPECT_EQ(NesPlayerRelativeTileFrame::AnchorTileRow, 27u);
}

TEST(NesPlayerRelativeTileFrameTest, InteriorPlayerMapsScreenAroundAnchorWithoutLoss)
{
    const NesTileTokenFrame tokenFrame = makeScreenTokenFrame();

    const NesPlayerRelativeTileFrame relativeFrame =
        makeNesPlayerRelativeTileFrame(tokenFrame, pixelForTile(16u), pixelForTile(20u));

    EXPECT_EQ(relativeFrame.frameId, 77u);
    EXPECT_EQ(relativeFrame.scrollX, 9u);
    EXPECT_EQ(relativeFrame.scrollY, 18u);
    EXPECT_EQ(relativeFrame.playerTileColumn, 16);
    EXPECT_EQ(relativeFrame.playerTileRow, 20);
    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        tokenAt(16u, 20u));
    EXPECT_EQ(relativeFrame.tokens[relativeTileIndex(15u, 7u)], tokenAt(0u, 0u));
    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(46u, 34u)],
        tokenAt(
            NesPlayerRelativeTileFrame::ScreenTileColumns - 1u,
            NesPlayerRelativeTileFrame::ScreenTileRows - 1u));
    EXPECT_EQ(
        nonVoidTokenCount(relativeFrame),
        static_cast<size_t>(NesPlayerRelativeTileFrame::ScreenTileColumns)
            * NesPlayerRelativeTileFrame::ScreenTileRows);
}

TEST(NesPlayerRelativeTileFrameTest, TopLeftPlayerKeepsWholeScreen)
{
    const NesTileTokenFrame tokenFrame = makeScreenTokenFrame();

    const NesPlayerRelativeTileFrame relativeFrame =
        makeNesPlayerRelativeTileFrame(tokenFrame, pixelForTile(0u), pixelForTile(0u));

    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        tokenAt(0u, 0u));
    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::RelativeTileColumns - 1u,
            NesPlayerRelativeTileFrame::RelativeTileRows - 1u)],
        tokenAt(
            NesPlayerRelativeTileFrame::ScreenTileColumns - 1u,
            NesPlayerRelativeTileFrame::ScreenTileRows - 1u));
    EXPECT_EQ(
        nonVoidTokenCount(relativeFrame),
        static_cast<size_t>(NesPlayerRelativeTileFrame::ScreenTileColumns)
            * NesPlayerRelativeTileFrame::ScreenTileRows);
}

TEST(NesPlayerRelativeTileFrameTest, BottomRightPlayerKeepsWholeScreen)
{
    const NesTileTokenFrame tokenFrame = makeScreenTokenFrame();

    const NesPlayerRelativeTileFrame relativeFrame = makeNesPlayerRelativeTileFrame(
        tokenFrame,
        pixelForTile(NesPlayerRelativeTileFrame::ScreenTileColumns - 1u),
        pixelForTile(NesPlayerRelativeTileFrame::ScreenTileRows - 1u));

    EXPECT_EQ(relativeFrame.tokens[relativeTileIndex(0u, 0u)], tokenAt(0u, 0u));
    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        tokenAt(
            NesPlayerRelativeTileFrame::ScreenTileColumns - 1u,
            NesPlayerRelativeTileFrame::ScreenTileRows - 1u));
    EXPECT_EQ(
        nonVoidTokenCount(relativeFrame),
        static_cast<size_t>(NesPlayerRelativeTileFrame::ScreenTileColumns)
            * NesPlayerRelativeTileFrame::ScreenTileRows);
}

TEST(NesPlayerRelativeTileFrameTest, NegativePlayerPixelsUseFloorTileCoordinates)
{
    const NesTileTokenFrame tokenFrame = makeScreenTokenFrame();

    const NesPlayerRelativeTileFrame relativeFrame =
        makeNesPlayerRelativeTileFrame(tokenFrame, -1, -1);

    EXPECT_EQ(relativeFrame.playerTileColumn, -1);
    EXPECT_EQ(relativeFrame.playerTileRow, -1);
    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn + 1u,
            NesPlayerRelativeTileFrame::AnchorTileRow + 1u)],
        tokenAt(0u, 0u));
    EXPECT_EQ(
        relativeFrame.tokens[relativeTileIndex(
            NesPlayerRelativeTileFrame::AnchorTileColumn,
            NesPlayerRelativeTileFrame::AnchorTileRow)],
        NesTileTokenizer::VoidToken);
}
