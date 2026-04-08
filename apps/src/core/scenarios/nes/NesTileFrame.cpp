#include "core/scenarios/nes/NesTileFrame.h"

#include <cstddef>
#include <cstdint>

namespace DirtSim {

namespace {

constexpr uint16_t kFullFrameHeightPixels = 240u;
constexpr uint16_t kFullFrameWidthPixels = 256u;
constexpr uint16_t kPatternBytesPerTile = 16u;
constexpr uint16_t kTileSizePixels = 8u;
constexpr uint16_t kTopCropPixels = 8u;

size_t mirroredNametableOffset(uint16_t logicalOffset, uint8_t mirror)
{
    switch (mirror) {
        case 0u:
            return logicalOffset % 1024u;
        case 1u:
            return (logicalOffset % 1024u) + 1024u;
        case 2u:
            return logicalOffset & 2047u;
        case 3u:
            return ((logicalOffset / 2u) & 1024u) | (logicalOffset % 1024u);
        default:
            return logicalOffset & 2047u;
    }
}

uint8_t readNametableTileIdAtWorldPixel(
    const NesPpuSnapshot& snapshot, uint16_t scrollX, uint16_t scrollY, uint16_t x, uint16_t y)
{
    const uint16_t worldX = static_cast<uint16_t>((scrollX + x) % 512u);
    const uint16_t worldY = static_cast<uint16_t>((scrollY + y) % 480u);

    const uint16_t tileX = static_cast<uint16_t>(worldX / kTileSizePixels);
    const uint16_t tileY = static_cast<uint16_t>(worldY / kTileSizePixels);
    const uint16_t nametableX = static_cast<uint16_t>((tileX / 32u) & 0x01u);
    const uint16_t nametableY = static_cast<uint16_t>((tileY / 30u) & 0x01u);
    const uint16_t localTileX = static_cast<uint16_t>(tileX % 32u);
    const uint16_t localTileY = static_cast<uint16_t>(tileY % 30u);
    const uint16_t logicalOffset = static_cast<uint16_t>(
        nametableY * 0x800u + nametableX * 0x400u + localTileY * 32u + localTileX);
    return snapshot.vram[mirroredNametableOffset(logicalOffset, snapshot.mirror)];
}

size_t tilePatternBaseOffset(const NesPpuSnapshot& snapshot, uint8_t tileId)
{
    const size_t patternBase = (snapshot.ppuCtrl & 0x10u) != 0u ? 0x1000u : 0u;
    return patternBase + static_cast<size_t>(tileId) * kPatternBytesPerTile;
}

uint64_t tilePatternHash(const NesPpuSnapshot& snapshot, uint8_t tileId)
{
    constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    const size_t base = tilePatternBaseOffset(snapshot, tileId);
    uint64_t hash = kFnvOffsetBasis;
    for (size_t i = 0; i < kPatternBytesPerTile; ++i) {
        hash ^= snapshot.chr[base + i];
        hash *= kFnvPrime;
    }
    return hash;
}

uint8_t tilePatternPixel(
    const NesPpuSnapshot& snapshot, uint8_t tileId, uint8_t patternX, uint8_t patternY)
{
    const size_t base = tilePatternBaseOffset(snapshot, tileId);
    const uint8_t lo = snapshot.chr[base + patternY];
    const uint8_t hi = snapshot.chr[base + 8u + patternY];
    const uint8_t bit = static_cast<uint8_t>(7u - patternX);
    return static_cast<uint8_t>(((lo >> bit) & 0x01u) | (((hi >> bit) & 0x01u) << 1u));
}

uint16_t scrollXFromV(const NesPpuSnapshot& snapshot)
{
    return static_cast<uint16_t>(
        (((snapshot.v >> 10u) & 0x01u) * kFullFrameWidthPixels)
        + ((snapshot.v & 0x001Fu) * kTileSizePixels) + (snapshot.fineX & 0x07u));
}

uint16_t scrollYFromV(const NesPpuSnapshot& snapshot)
{
    return static_cast<uint16_t>(
        (((snapshot.v >> 11u) & 0x01u) * kFullFrameHeightPixels)
        + (((snapshot.v >> 5u) & 0x001Fu) * kTileSizePixels) + ((snapshot.v >> 12u) & 0x07u));
}

} // namespace

NesTileFrame makeNesTileFrame(const NesPpuSnapshot& snapshot)
{
    NesTileFrame frame;
    frame.frameId = snapshot.frameId;
    frame.scrollX = scrollXFromV(snapshot);
    frame.scrollY = scrollYFromV(snapshot);

    for (uint16_t y = 0; y < NesTileFrame::VisibleHeightPixels; ++y) {
        const uint16_t fullY = static_cast<uint16_t>(y + kTopCropPixels);
        const size_t rowBase = static_cast<size_t>(y) * NesTileFrame::VisibleWidthPixels;
        for (uint16_t x = 0; x < NesTileFrame::VisibleWidthPixels; ++x) {
            const uint8_t tileId =
                readNametableTileIdAtWorldPixel(snapshot, frame.scrollX, frame.scrollY, x, fullY);
            const uint16_t worldX = static_cast<uint16_t>((frame.scrollX + x) % 512u);
            const uint16_t worldY = static_cast<uint16_t>((frame.scrollY + fullY) % 480u);
            const uint8_t patternX = static_cast<uint8_t>(worldX % kTileSizePixels);
            const uint8_t patternY = static_cast<uint8_t>(worldY % kTileSizePixels);
            frame.patternPixels[rowBase + x] =
                tilePatternPixel(snapshot, tileId, patternX, patternY);
        }
    }

    for (uint16_t gy = 0; gy < NesTileFrame::VisibleTileRows; ++gy) {
        for (uint16_t gx = 0; gx < NesTileFrame::VisibleTileColumns; ++gx) {
            const uint16_t sampleX = static_cast<uint16_t>(gx * kTileSizePixels + 4u);
            const uint16_t sampleY =
                static_cast<uint16_t>(gy * kTileSizePixels + 4u + kTopCropPixels);
            const uint8_t tileId = readNametableTileIdAtWorldPixel(
                snapshot, frame.scrollX, frame.scrollY, sampleX, sampleY);
            const size_t cellIndex =
                static_cast<size_t>(gy) * NesTileFrame::VisibleTileColumns + gx;
            frame.tileIds[cellIndex] = tileId;
            frame.tilePatternHashes[cellIndex] = tilePatternHash(snapshot, tileId);
        }
    }

    return frame;
}

} // namespace DirtSim
