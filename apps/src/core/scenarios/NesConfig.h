#pragma once

#include <cstdint>
#include <string>
#include <zpp_bits.h>

namespace DirtSim::Config {

struct NesFlappyParatroopa {
    using serialize = zpp::bits::members<5>;

    std::string romId = "flappy-paratroopa-world-unl";
    std::string romDirectory = "testdata/roms";
    std::string romPath = "testdata/roms/Flappy.Paratroopa.World.Unl.nes";
    uint32_t maxEpisodeFrames = 108000;
    bool requireSmolnesMapper = true;
};

} // namespace DirtSim::Config
