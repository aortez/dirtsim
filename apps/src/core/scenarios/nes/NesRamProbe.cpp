#include "NesRamProbe.h"

#include <fstream>
#include <utility>

namespace DirtSim {

namespace {
constexpr uint16_t kFlappyParatroopaGameStateAddr = 0x0A;
constexpr uint16_t kFlappyParatroopaScrollXAddr = 0x08;
constexpr uint16_t kFlappyParatroopaScrollNtAddr = 0x09;
constexpr uint16_t kFlappyParatroopaBirdYAddr = 0x01;
constexpr uint16_t kFlappyParatroopaBirdVelocityHighAddr = 0x03;
constexpr uint16_t kFlappyParatroopaBirdXAddr = 0x20;
constexpr uint16_t kFlappyParatroopaScoreOnesAddr = 0x19;
constexpr uint16_t kFlappyParatroopaScoreTensAddr = 0x1A;
constexpr uint16_t kFlappyParatroopaScoreHundredsAddr = 0x1B;
constexpr uint16_t kFlappyParatroopaNt0Pipe0GapAddr = 0x12;
constexpr uint16_t kFlappyParatroopaNt0Pipe1GapAddr = 0x13;
constexpr uint16_t kFlappyParatroopaNt1Pipe0GapAddr = 0x14;
constexpr uint16_t kFlappyParatroopaNt1Pipe1GapAddr = 0x15;

constexpr size_t kFlappyParatroopaGameStateIndex = 0;
constexpr size_t kFlappyParatroopaScrollXIndex = 1;
constexpr size_t kFlappyParatroopaScrollNtIndex = 2;
constexpr size_t kFlappyParatroopaBirdYIndex = 3;
constexpr size_t kFlappyParatroopaBirdVelocityHighIndex = 4;
constexpr size_t kFlappyParatroopaBirdXIndex = 5;
constexpr size_t kFlappyParatroopaScoreOnesIndex = 6;
constexpr size_t kFlappyParatroopaScoreTensIndex = 7;
constexpr size_t kFlappyParatroopaScoreHundredsIndex = 8;
constexpr size_t kFlappyParatroopaNt0Pipe0GapIndex = 9;
constexpr size_t kFlappyParatroopaNt0Pipe1GapIndex = 10;
constexpr size_t kFlappyParatroopaNt1Pipe0GapIndex = 11;
constexpr size_t kFlappyParatroopaNt1Pipe1GapIndex = 12;

std::vector<NesRamProbeAddress> makeFlappyParatroopaAddresses()
{
    return {
        NesRamProbeAddress{ .label = "game_state", .address = kFlappyParatroopaGameStateAddr },
        NesRamProbeAddress{ .label = "scroll_x", .address = kFlappyParatroopaScrollXAddr },
        NesRamProbeAddress{ .label = "scroll_nt", .address = kFlappyParatroopaScrollNtAddr },
        NesRamProbeAddress{ .label = "bird_y", .address = kFlappyParatroopaBirdYAddr },
        NesRamProbeAddress{
            .label = "bird_vel_hi",
            .address = kFlappyParatroopaBirdVelocityHighAddr,
        },
        NesRamProbeAddress{ .label = "bird_x", .address = kFlappyParatroopaBirdXAddr },
        NesRamProbeAddress{ .label = "score_ones", .address = kFlappyParatroopaScoreOnesAddr },
        NesRamProbeAddress{ .label = "score_tens", .address = kFlappyParatroopaScoreTensAddr },
        NesRamProbeAddress{
            .label = "score_hundreds",
            .address = kFlappyParatroopaScoreHundredsAddr,
        },
        NesRamProbeAddress{ .label = "nt0_pipe0_gap", .address = kFlappyParatroopaNt0Pipe0GapAddr },
        NesRamProbeAddress{ .label = "nt0_pipe1_gap", .address = kFlappyParatroopaNt0Pipe1GapAddr },
        NesRamProbeAddress{ .label = "nt1_pipe0_gap", .address = kFlappyParatroopaNt1Pipe0GapAddr },
        NesRamProbeAddress{ .label = "nt1_pipe1_gap", .address = kFlappyParatroopaNt1Pipe1GapAddr },
    };
}
} // namespace

NesRamProbeStepper::NesRamProbeStepper(
    Scenario::EnumType scenarioId,
    const ScenarioConfig& config,
    std::vector<NesRamProbeAddress> cpuAddresses,
    double deltaTimeSeconds)
    : cpuAddresses_(std::move(cpuAddresses)),
      deltaTimeSeconds_(deltaTimeSeconds),
      driver_(scenarioId)
{
    const auto setResult = driver_.setConfig(config);
    if (setResult.isError()) {
        return;
    }
    (void)driver_.setup();
}

const std::vector<NesRamProbeAddress>& NesRamProbeStepper::getCpuAddresses() const
{
    return cpuAddresses_;
}

uint8_t NesRamProbeStepper::getControllerMask() const
{
    return controllerMask_;
}

const SmolnesRuntime::MemorySnapshot* NesRamProbeStepper::getLastMemorySnapshot() const
{
    return lastMemorySnapshot_.has_value() ? &lastMemorySnapshot_.value() : nullptr;
}

bool NesRamProbeStepper::isRuntimeReady() const
{
    return driver_.isRuntimeRunning() && driver_.isRuntimeHealthy();
}

std::string NesRamProbeStepper::getLastError() const
{
    return driver_.getRuntimeLastError();
}

NesRamProbeFrame NesRamProbeStepper::step(std::optional<uint8_t> controllerMask)
{
    if (controllerMask.has_value()) {
        controllerMask_ = controllerMask.value();
    }

    (void)deltaTimeSeconds_;
    driver_.setController1State(controllerMask_);
    driver_.tick(timers_, scenarioVideoFrame_);

    NesRamProbeFrame frame;
    frame.frame = frameIndex_;
    frame.controllerMask = controllerMask_;
    frame.cpuRamValues.resize(cpuAddresses_.size(), 0u);

    lastMemorySnapshot_ = driver_.copyRuntimeMemorySnapshot();
    if (lastMemorySnapshot_.has_value()) {
        for (size_t addrIndex = 0; addrIndex < cpuAddresses_.size(); ++addrIndex) {
            const uint16_t address = cpuAddresses_[addrIndex].address;
            if (address < lastMemorySnapshot_->cpuRam.size()) {
                frame.cpuRamValues[addrIndex] = lastMemorySnapshot_->cpuRam[address];
            }
        }
    }

    frameIndex_++;
    return frame;
}

const char* toString(FlappyParatroopaGamePhase phase)
{
    switch (phase) {
        case FlappyParatroopaGamePhase::Mode0:
            return "Mode0";
        case FlappyParatroopaGamePhase::Mode1:
            return "Mode1";
        case FlappyParatroopaGamePhase::Playing:
            return "Playing";
        case FlappyParatroopaGamePhase::Dying:
            return "Dying";
        case FlappyParatroopaGamePhase::Mode4:
            return "Mode4";
        case FlappyParatroopaGamePhase::Mode5:
            return "Mode5";
        case FlappyParatroopaGamePhase::Mode6:
            return "Mode6";
        case FlappyParatroopaGamePhase::GameOver:
            return "GameOver";
        case FlappyParatroopaGamePhase::Attract:
            return "Attract";
        case FlappyParatroopaGamePhase::StartTransition:
            return "StartTransition";
        case FlappyParatroopaGamePhase::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

FlappyParatroopaGamePhase flappyParatroopaGamePhaseFromByte(uint8_t value)
{
    switch (value) {
        case 0u:
            return FlappyParatroopaGamePhase::Mode0;
        case 1u:
            return FlappyParatroopaGamePhase::Mode1;
        case 2u:
            return FlappyParatroopaGamePhase::Playing;
        case 3u:
            return FlappyParatroopaGamePhase::Dying;
        case 4u:
            return FlappyParatroopaGamePhase::Mode4;
        case 5u:
            return FlappyParatroopaGamePhase::Mode5;
        case 6u:
            return FlappyParatroopaGamePhase::Mode6;
        case 7u:
            return FlappyParatroopaGamePhase::GameOver;
        case 8u:
            return FlappyParatroopaGamePhase::Attract;
        case 9u:
            return FlappyParatroopaGamePhase::StartTransition;
        default:
            return FlappyParatroopaGamePhase::Unknown;
    }
}

FlappyParatroopaProbeStepper::FlappyParatroopaProbeStepper(
    const Config::NesFlappyParatroopa& config, double deltaTimeSeconds)
    : stepper_(
          Scenario::EnumType::NesFlappyParatroopa,
          ScenarioConfig{ config },
          makeFlappyParatroopaAddresses(),
          deltaTimeSeconds)
{}

uint8_t FlappyParatroopaProbeStepper::getControllerMask() const
{
    return stepper_.getControllerMask();
}

const SmolnesRuntime::MemorySnapshot* FlappyParatroopaProbeStepper::getLastMemorySnapshot() const
{
    return stepper_.getLastMemorySnapshot();
}

bool FlappyParatroopaProbeStepper::isRuntimeReady() const
{
    return stepper_.isRuntimeReady();
}

std::string FlappyParatroopaProbeStepper::getLastError() const
{
    return stepper_.getLastError();
}

std::optional<FlappyParatroopaGameState> FlappyParatroopaProbeStepper::step(
    std::optional<uint8_t> controllerMask)
{
    const NesRamProbeFrame frame = stepper_.step(controllerMask);

    if (frame.cpuRamValues.size() <= kFlappyParatroopaNt1Pipe1GapIndex) {
        return std::nullopt;
    }

    FlappyParatroopaGameState state;
    state.gamePhaseRaw = frame.cpuRamValues[kFlappyParatroopaGameStateIndex];
    state.gamePhase = flappyParatroopaGamePhaseFromByte(state.gamePhaseRaw);
    state.scrollX = frame.cpuRamValues[kFlappyParatroopaScrollXIndex];
    state.scrollNt = frame.cpuRamValues[kFlappyParatroopaScrollNtIndex];
    state.birdX = frame.cpuRamValues[kFlappyParatroopaBirdXIndex];
    state.birdY = frame.cpuRamValues[kFlappyParatroopaBirdYIndex];
    state.birdVelocityHigh = frame.cpuRamValues[kFlappyParatroopaBirdVelocityHighIndex];
    state.scoreOnes = frame.cpuRamValues[kFlappyParatroopaScoreOnesIndex];
    state.scoreTens = frame.cpuRamValues[kFlappyParatroopaScoreTensIndex];
    state.scoreHundreds = frame.cpuRamValues[kFlappyParatroopaScoreHundredsIndex];
    state.nt0Pipe0Gap = frame.cpuRamValues[kFlappyParatroopaNt0Pipe0GapIndex];
    state.nt0Pipe1Gap = frame.cpuRamValues[kFlappyParatroopaNt0Pipe1GapIndex];
    state.nt1Pipe0Gap = frame.cpuRamValues[kFlappyParatroopaNt1Pipe0GapIndex];
    state.nt1Pipe1Gap = frame.cpuRamValues[kFlappyParatroopaNt1Pipe1GapIndex];
    return std::optional<FlappyParatroopaGameState>{ state };
}

bool NesRamProbeTrace::writeCsv(const std::filesystem::path& path) const
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    stream << "frame,controller_mask";
    for (const NesRamProbeAddress& address : cpuAddresses) {
        const std::string label =
            address.label.empty() ? ("cpu_" + std::to_string(address.address)) : address.label;
        stream << "," << label;
    }
    stream << "\n";

    for (const NesRamProbeFrame& frame : frames) {
        stream << frame.frame << "," << static_cast<uint32_t>(frame.controllerMask);
        for (uint8_t value : frame.cpuRamValues) {
            stream << "," << static_cast<uint32_t>(value);
        }
        stream << "\n";
    }

    return stream.good();
}

NesRamProbeTrace captureNesRamProbeTrace(
    Scenario::EnumType scenarioId,
    const ScenarioConfig& config,
    const std::vector<uint8_t>& controllerScript,
    const std::vector<NesRamProbeAddress>& cpuAddresses,
    double deltaTimeSeconds)
{
    NesRamProbeTrace trace;
    trace.cpuAddresses = cpuAddresses;
    trace.frames.reserve(controllerScript.size());

    NesRamProbeStepper stepper{ scenarioId, config, cpuAddresses, deltaTimeSeconds };
    for (uint8_t controllerMask : controllerScript) {
        trace.frames.push_back(stepper.step(controllerMask));
    }

    return trace;
}

} // namespace DirtSim
