#pragma once

#include "core/scenarios/NesFlappyParatroopaScenario.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace DirtSim {

class World;

struct NesRamProbeAddress {
    std::string label;
    uint16_t address = 0;
};

struct NesRamProbeFrame {
    uint64_t frame = 0;
    uint8_t controllerMask = 0;
    std::vector<uint8_t> cpuRamValues;
};

struct NesRamProbeTrace {
    std::vector<NesRamProbeAddress> cpuAddresses;
    std::vector<NesRamProbeFrame> frames;

    bool writeCsv(const std::filesystem::path& path) const;
};

NesRamProbeTrace captureNesRamProbeTrace(
    NesFlappyParatroopaScenario& scenario,
    World& world,
    const std::vector<uint8_t>& controllerScript,
    const std::vector<NesRamProbeAddress>& cpuAddresses,
    double deltaTimeSeconds);

} // namespace DirtSim
