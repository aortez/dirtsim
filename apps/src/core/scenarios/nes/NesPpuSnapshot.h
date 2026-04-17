#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace DirtSim {

struct NesPpuSnapshot {
    static constexpr size_t ChrBytes = 8192u;
    static constexpr size_t OamBytes = 256u;
    static constexpr size_t VramBytes = 2048u;

    uint64_t frameId = 0;
    uint16_t v = 0;
    uint8_t fineX = 0;
    uint8_t mirror = 0;
    uint8_t ppuCtrl = 0;
    uint8_t ppuMask = 0;
    std::array<uint8_t, ChrBytes> chr{};
    std::array<uint8_t, OamBytes> oam{};
    std::array<uint8_t, VramBytes> vram{};
};

} // namespace DirtSim
