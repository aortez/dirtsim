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

enum class FlappyParatroopaGamePhase : uint8_t {
    Mode0 = 0,
    Mode1 = 1,
    Playing = 2,
    Dying = 3,
    Mode4 = 4,
    Mode5 = 5,
    Mode6 = 6,
    GameOver = 7,
    Attract = 8,
    StartTransition = 9,
    Unknown = 255,
};

const char* toString(FlappyParatroopaGamePhase phase);
FlappyParatroopaGamePhase flappyParatroopaGamePhaseFromByte(uint8_t value);

struct FlappyParatroopaGameState {
    FlappyParatroopaGamePhase gamePhase = FlappyParatroopaGamePhase::Unknown;
    uint8_t gamePhaseRaw = 0;
    uint8_t birdX = 0;
    uint8_t birdY = 0;
    uint8_t birdVelocityHigh = 0;
    uint8_t scrollX = 0;
    uint8_t scrollNt = 0;
    uint8_t scoreOnes = 0;
    uint8_t scoreTens = 0;
    uint8_t scoreHundreds = 0;
    uint8_t nt0Pipe0Gap = 0;
    uint8_t nt0Pipe1Gap = 0;
    uint8_t nt1Pipe0Gap = 0;
    uint8_t nt1Pipe1Gap = 0;
};

class FlappyParatroopaProbeStepper {
public:
    FlappyParatroopaProbeStepper(
        NesFlappyParatroopaScenario& scenario, World& world, double deltaTimeSeconds);

    FlappyParatroopaProbeStepper(const FlappyParatroopaProbeStepper&) = delete;
    FlappyParatroopaProbeStepper& operator=(const FlappyParatroopaProbeStepper&) = delete;

    uint8_t getControllerMask() const;
    const SmolnesRuntime::MemorySnapshot* getLastMemorySnapshot() const;

    std::optional<FlappyParatroopaGameState> step(
        std::optional<uint8_t> controllerMask = std::nullopt);

private:
    NesRamProbeStepper stepper_;
};

} // namespace DirtSim
