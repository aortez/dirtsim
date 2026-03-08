#pragma once

#include <array>
#include <cstdint>

namespace DirtSim {

// 2C02G hardware-measured NES palette converted to RGB565.
// Source: Ricoh 2C02G PPU NTSC decode (Mesen/nesdev wiki reference).
inline constexpr std::array<uint16_t, 64> kNesRgb565Palette = {
    25388, 365,   4367,  14511, 24684, 28743, 28801, 22720, 12608, 2464,  480,   482,   456,
    0,     0,     0,     44405, 4886,  17049, 31257, 41397, 47534, 47590, 39488, 27360, 15200,
    2976,  967,   911,   0,     0,     0,     65535, 23967, 36127, 50335, 62526, 64535, 64591,
    60617, 48453, 34246, 22058, 15953, 17945, 19049, 0,     0,     65535, 48895, 52927, 59039,
    65151, 65116, 65145, 63158, 59092, 53013, 46902, 44857, 44860, 46518, 0,     0,
};

} // namespace DirtSim
