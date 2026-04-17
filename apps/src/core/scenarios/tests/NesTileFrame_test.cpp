#include "core/scenarios/nes/NesTileFrame.h"

#include "core/Timers.h"

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

TEST(NesTileFrameTest, InstrumentedExtractionMatchesUninstrumentedFrame)
{
    NesPpuSnapshot snapshot;
    snapshot.frameId = 12u;
    snapshot.fineX = 3u;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 1u);
    setSolidTilePattern(snapshot, 2u, 2u);
    snapshot.vram[32u] = 1u;
    snapshot.vram[33u] = 2u;

    Timers timers;
    const NesTileFrame baseline = makeNesTileFrame(snapshot);
    const NesTileFrame instrumented = makeNesTileFrame(snapshot, &timers);

    EXPECT_EQ(instrumented.frameId, baseline.frameId);
    EXPECT_EQ(instrumented.scrollX, baseline.scrollX);
    EXPECT_EQ(instrumented.scrollY, baseline.scrollY);
    EXPECT_EQ(instrumented.patternPixels, baseline.patternPixels);
    EXPECT_EQ(instrumented.tileIds, baseline.tileIds);
    EXPECT_EQ(instrumented.tilePatternHashes, baseline.tilePatternHashes);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_scroll_decode"), 1u);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_pattern_pixels"), 1u);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_tile_ids"), 1u);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_tile_hashes"), 1u);
}

TEST(NesTileFrameTest, OptionalPatternPixelsSkipsOnlyPatternPanelData)
{
    NesPpuSnapshot snapshot;
    snapshot.frameId = 18u;
    snapshot.fineX = 3u;
    snapshot.mirror = 2u;
    setSolidTilePattern(snapshot, 1u, 1u);
    setSolidTilePattern(snapshot, 2u, 2u);
    snapshot.vram[32u] = 1u;
    snapshot.vram[33u] = 2u;

    Timers timers;
    const NesTileFrame fullFrame = makeNesTileFrame(snapshot);
    const NesTileFrame tileOnlyFrame = makeNesTileFrame(
        snapshot, NesTileFrameBuildOptions{ .includePatternPixels = false }, &timers);
    const std::array<uint8_t, NesTileFrame::VisibleWidthPixels * NesTileFrame::VisibleHeightPixels>
        emptyPatternPixels{};

    EXPECT_EQ(tileOnlyFrame.frameId, fullFrame.frameId);
    EXPECT_EQ(tileOnlyFrame.scrollX, fullFrame.scrollX);
    EXPECT_EQ(tileOnlyFrame.scrollY, fullFrame.scrollY);
    EXPECT_EQ(tileOnlyFrame.tileIds, fullFrame.tileIds);
    EXPECT_EQ(tileOnlyFrame.tilePatternHashes, fullFrame.tilePatternHashes);
    EXPECT_EQ(tileOnlyFrame.patternPixels, emptyPatternPixels);
    EXPECT_NE(fullFrame.patternPixels, emptyPatternPixels);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_scroll_decode"), 1u);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_pattern_pixels"), 0u);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_tile_ids"), 1u);
    EXPECT_EQ(timers.getCallCount("nes_tile_frame_tile_hashes"), 1u);
}
