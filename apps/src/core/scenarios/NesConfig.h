#pragma once

#include <cstdint>
#include <string>
#include <zpp_bits.h>

namespace DirtSim::Config {

struct Nes {
    using serialize = zpp::bits::members<4>;

    std::string romPath = "roms/Nim & Nom v1.2.nes";
    uint32_t frameSkip = 4;
    uint32_t maxEpisodeFrames = 108000;
    bool requireSmolnesMapper = true;
};

} // namespace DirtSim::Config
