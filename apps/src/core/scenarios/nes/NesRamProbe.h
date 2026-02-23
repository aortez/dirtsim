#pragma once

#include "core/scenarios/NesFlappyParatroopaScenario.h"

#include <cstdint>
#include <filesystem>
#include <optional>
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

class NesRamProbeStepper {
public:
    NesRamProbeStepper(
        NesFlappyParatroopaScenario& scenario,
        World& world,
        std::vector<NesRamProbeAddress> cpuAddresses,
        double deltaTimeSeconds);

    NesRamProbeStepper(const NesRamProbeStepper&) = delete;
    NesRamProbeStepper& operator=(const NesRamProbeStepper&) = delete;

    const std::vector<NesRamProbeAddress>& getCpuAddresses() const;

    uint8_t getControllerMask() const;
    const SmolnesRuntime::MemorySnapshot* getLastMemorySnapshot() const;

    NesRamProbeFrame step(std::optional<uint8_t> controllerMask = std::nullopt);

private:
    NesFlappyParatroopaScenario& scenario_;
    World& world_;
    std::vector<NesRamProbeAddress> cpuAddresses_;
    double deltaTimeSeconds_ = 0.0;
    uint64_t frameIndex_ = 0;
    uint8_t controllerMask_ = 0;
    std::optional<SmolnesRuntime::MemorySnapshot> lastMemorySnapshot_;
};

} // namespace DirtSim
