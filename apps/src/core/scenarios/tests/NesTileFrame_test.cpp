#include "core/scenarios/nes/NesTileFrame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

size_t tileCellIndex(uint16_t x, uint16_t y)
{
    return static_cast<size_t>(y) * NesTileFrame::VisibleTileColumns + x;
}

void setSolidTilePattern(NesPpuSnapshot& snapshot, uint8_t tileId, uint8_t color)
{
    const size_t base = static_cast<size_t>(tileId) * 16u;
    const uint8_t lo = (color & 0x01u) != 0u ? 0xFFu : 0x00u;
    const uint8_t hi = (color & 0x02u) != 0u ? 0xFFu : 0x00u;
    for (size_t row = 0; row < 8u; ++row) {
        snapshot.chr[base + row] = lo;
        snapshot.chr[base + 8u + row] = hi;
    }
}

} // namespace

TEST(NesTileFrameTest, ExtractsVisiblePixelsFromSyntheticSnapshot)
{
    NesPpuSnapshot snapshot;
    snapshot.frameId = 7u;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 1u);
    snapshot.vram[32u] = 1u;

    const NesTileFrame frame = makeNesTileFrame(snapshot);

    EXPECT_EQ(frame.frameId, 7u);
    EXPECT_EQ(frame.scrollX, 0u);
    EXPECT_EQ(frame.scrollY, 0u);
    EXPECT_EQ(frame.tileIds[tileCellIndex(0u, 0u)], 1u);
    EXPECT_EQ(frame.patternPixels[0u], 1u);
    EXPECT_EQ(frame.patternPixels[7u], 1u);
    EXPECT_EQ(frame.patternPixels[8u], 0u);
    EXPECT_NE(
        frame.tilePatternHashes[tileCellIndex(0u, 0u)],
        frame.tilePatternHashes[tileCellIndex(1u, 0u)]);
}

TEST(NesTileFrameTest, FineXScrollMovesVisibleTileBoundary)
{
    NesPpuSnapshot snapshot;
    snapshot.fineX = 7u;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 1u);
    setSolidTilePattern(snapshot, 2u, 2u);
    snapshot.vram[32u] = 1u;
    snapshot.vram[33u] = 2u;

    const NesTileFrame frame = makeNesTileFrame(snapshot);

    EXPECT_EQ(frame.patternPixels[0u], 1u);
    EXPECT_EQ(frame.patternPixels[1u], 2u);
    EXPECT_EQ(frame.tileIds[tileCellIndex(0u, 0u)], 2u);
}

TEST(NesTileFrameTest, HashesMatchForDifferentTileIdsWithSamePattern)
{
    NesPpuSnapshot snapshot;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 3u);
    setSolidTilePattern(snapshot, 2u, 3u);
    snapshot.vram[32u] = 1u;
    snapshot.vram[33u] = 2u;

    const NesTileFrame frame = makeNesTileFrame(snapshot);

    EXPECT_EQ(frame.tileIds[tileCellIndex(0u, 0u)], 1u);
    EXPECT_EQ(frame.tileIds[tileCellIndex(1u, 0u)], 2u);
    EXPECT_EQ(
        frame.tilePatternHashes[tileCellIndex(0u, 0u)],
        frame.tilePatternHashes[tileCellIndex(1u, 0u)]);
}
