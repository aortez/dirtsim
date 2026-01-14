#include "ColorNames.h"

namespace ColorNames {

// Light sources.
uint32_t warmSunlight()
{
    return 0xFFF2D9FF;
}
uint32_t coolMoonlight()
{
    return 0xC4D4FFFF;
}
uint32_t torchOrange()
{
    return 0xFFCC66FF;
}
uint32_t candleYellow()
{
    return 0xFFE4B3FF;
}

// Ambient presets.
uint32_t dayAmbient()
{
    return 0x1A1A1EFF;
}
uint32_t duskAmbient()
{
    return 0x2D1A2DFF;
}
uint32_t nightAmbient()
{
    return 0x0A0A12FF;
}
uint32_t caveAmbient()
{
    return 0x050508FF;
}

// Material base colors.
uint32_t air()
{
    return 0x00000000;
}
uint32_t dirt()
{
    return 0x8B6914FF;
}
uint32_t leaf()
{
    return 0x228B22FF;
}
uint32_t metal()
{
    return 0xA0A0A0FF;
}
uint32_t root()
{
    return 0x5C4033FF;
}
uint32_t sand()
{
    return 0xE6D5ACFF;
}
uint32_t seed()
{
    return 0x90EE90FF;
}
uint32_t stone()
{
    return 0x696969FF;
}
uint32_t water()
{
    return 0x3399FFFF;
}
uint32_t wood()
{
    return 0x6B4423FF;
}

// Material emissions.
uint32_t lavaGlow()
{
    return 0xFF4D1AFF;
}
uint32_t seedGlow()
{
    return 0x80FF80FF;
}
uint32_t stormGlow()
{
    return 0xAADDFFFF;
}

// Utility.
uint32_t white()
{
    return 0xFFFFFFFF;
}
uint32_t black()
{
    return 0x000000FF;
}
uint32_t transparent()
{
    return 0x00000000;
}

} // namespace ColorNames
