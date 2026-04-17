#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"

#include <cstddef>
#include <cstdint>

namespace DirtSim {

namespace {

int16_t tileCoordinateForPixel(int16_t pixel)
{
    if (pixel >= 0) {
        return static_cast<int16_t>(pixel / NesPlayerRelativeTileFrame::TileSizePixels);
    }

    return static_cast<int16_t>(
        -(((-pixel) + static_cast<int16_t>(NesPlayerRelativeTileFrame::TileSizePixels) - 1)
          / NesPlayerRelativeTileFrame::TileSizePixels));
}

size_t relativeTileIndex(int16_t column, int16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::RelativeTileColumns
        + static_cast<size_t>(column);
}

size_t screenTileIndex(int16_t column, int16_t row)
{
    return static_cast<size_t>(row) * NesPlayerRelativeTileFrame::ScreenTileColumns
        + static_cast<size_t>(column);
}

} // namespace

NesPlayerRelativeTileFrame makeNesPlayerRelativeTileFrame(
    const NesTileTokenFrame& tokenFrame, int16_t playerScreenX, int16_t playerScreenY)
{
    NesPlayerRelativeTileFrame relativeFrame{
        .frameId = tokenFrame.frameId,
        .scrollX = tokenFrame.scrollX,
        .scrollY = tokenFrame.scrollY,
        .playerScreenX = playerScreenX,
        .playerScreenY = playerScreenY,
        .playerTileColumn = tileCoordinateForPixel(playerScreenX),
        .playerTileRow = tileCoordinateForPixel(playerScreenY),
    };
    relativeFrame.tokens.fill(NesTileTokenizer::VoidToken);

    for (int16_t screenRow = 0;
         screenRow < static_cast<int16_t>(NesPlayerRelativeTileFrame::ScreenTileRows);
         ++screenRow) {
        for (int16_t screenColumn = 0;
             screenColumn < static_cast<int16_t>(NesPlayerRelativeTileFrame::ScreenTileColumns);
             ++screenColumn) {
            const int16_t relativeColumn = static_cast<int16_t>(
                static_cast<int16_t>(NesPlayerRelativeTileFrame::AnchorTileColumn) + screenColumn
                - relativeFrame.playerTileColumn);
            const int16_t relativeRow = static_cast<int16_t>(
                static_cast<int16_t>(NesPlayerRelativeTileFrame::AnchorTileRow) + screenRow
                - relativeFrame.playerTileRow);
            if (relativeColumn < 0
                || relativeColumn
                    >= static_cast<int16_t>(NesPlayerRelativeTileFrame::RelativeTileColumns)
                || relativeRow < 0
                || relativeRow
                    >= static_cast<int16_t>(NesPlayerRelativeTileFrame::RelativeTileRows)) {
                continue;
            }

            relativeFrame.tokens[relativeTileIndex(relativeColumn, relativeRow)] =
                tokenFrame.tokens[screenTileIndex(screenColumn, screenRow)];
        }
    }

    return relativeFrame;
}

} // namespace DirtSim
