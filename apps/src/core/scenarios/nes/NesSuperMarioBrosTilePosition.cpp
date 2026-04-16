#include "core/scenarios/nes/NesSuperMarioBrosTilePosition.h"

#include "core/scenarios/nes/NesTileFrame.h"

#include <cstdint>

namespace DirtSim {

namespace {

constexpr int16_t kSmbNametablePixelPeriod = 512;

int16_t wrappedNametableDelta(uint16_t value, uint16_t origin)
{
    int16_t delta = static_cast<int16_t>(
        static_cast<int32_t>(value & 0x01FFu) - static_cast<int32_t>(origin & 0x01FFu));
    if (delta < -(kSmbNametablePixelPeriod / 2)) {
        delta = static_cast<int16_t>(delta + kSmbNametablePixelPeriod);
    }
    if (delta > (kSmbNametablePixelPeriod / 2 - 1)) {
        delta = static_cast<int16_t>(delta - kSmbNametablePixelPeriod);
    }
    return delta;
}

} // namespace

int16_t makeNesSuperMarioBrosPlayerTileScreenX(
    const NesSuperMarioBrosState& state, uint16_t tileFrameScrollX)
{
    return wrappedNametableDelta(state.absoluteX, tileFrameScrollX);
}

int16_t makeNesSuperMarioBrosPlayerTileScreenY(const NesSuperMarioBrosState& state)
{
    return static_cast<int16_t>(
        static_cast<int16_t>(state.playerYScreen)
        - static_cast<int16_t>(NesTileFrame::TopCropPixels));
}

} // namespace DirtSim
