#pragma once

#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesSmolnesScenarioDriver.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim {

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
    Scenario::EnumType scenarioId,
    const ScenarioConfig& config,
    const std::vector<uint8_t>& controllerScript,
    const std::vector<NesRamProbeAddress>& cpuAddresses,
    double deltaTimeSeconds);

class NesRamProbeStepper {
public:
    NesRamProbeStepper(
        Scenario::EnumType scenarioId,
        const ScenarioConfig& config,
        std::vector<NesRamProbeAddress> cpuAddresses,
        double deltaTimeSeconds);

    NesRamProbeStepper(const NesRamProbeStepper&) = delete;
    NesRamProbeStepper& operator=(const NesRamProbeStepper&) = delete;

    const std::vector<NesRamProbeAddress>& getCpuAddresses() const;

    uint8_t getControllerMask() const;
    const SmolnesRuntime::MemorySnapshot* getLastMemorySnapshot() const;
    bool isRuntimeReady() const;
    std::string getLastError() const;

    NesRamProbeFrame step(std::optional<uint8_t> controllerMask = std::nullopt);

private:
    std::vector<NesRamProbeAddress> cpuAddresses_;
    double deltaTimeSeconds_ = 0.0;
    uint64_t frameIndex_ = 0;
    uint8_t controllerMask_ = 0;
    std::optional<SmolnesRuntime::MemorySnapshot> lastMemorySnapshot_;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame_;
    Timers timers_;
    NesSmolnesScenarioDriver driver_;
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
    explicit FlappyParatroopaProbeStepper(
        const Config::NesFlappyParatroopa& config, double deltaTimeSeconds);

    FlappyParatroopaProbeStepper(const FlappyParatroopaProbeStepper&) = delete;
    FlappyParatroopaProbeStepper& operator=(const FlappyParatroopaProbeStepper&) = delete;

    uint8_t getControllerMask() const;
    const SmolnesRuntime::MemorySnapshot* getLastMemorySnapshot() const;
    bool isRuntimeReady() const;
    std::string getLastError() const;

    std::optional<FlappyParatroopaGameState> step(
        std::optional<uint8_t> controllerMask = std::nullopt);

private:
    NesRamProbeStepper stepper_;
};

} // namespace DirtSim
