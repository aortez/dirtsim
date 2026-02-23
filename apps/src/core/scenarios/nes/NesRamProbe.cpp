#include "NesRamProbe.h"

#include "core/World.h"

#include <fstream>
#include <utility>

namespace DirtSim {

NesRamProbeStepper::NesRamProbeStepper(
    NesFlappyParatroopaScenario& scenario,
    World& world,
    std::vector<NesRamProbeAddress> cpuAddresses,
    double deltaTimeSeconds)
    : scenario_(scenario),
      world_(world),
      cpuAddresses_(std::move(cpuAddresses)),
      deltaTimeSeconds_(deltaTimeSeconds)
{}

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

NesRamProbeFrame NesRamProbeStepper::step(std::optional<uint8_t> controllerMask)
{
    if (controllerMask.has_value()) {
        controllerMask_ = controllerMask.value();
    }

    scenario_.setController1State(controllerMask_);
    scenario_.tick(world_, deltaTimeSeconds_);

    NesRamProbeFrame frame;
    frame.frame = frameIndex_;
    frame.controllerMask = controllerMask_;
    frame.cpuRamValues.resize(cpuAddresses_.size(), 0u);

    lastMemorySnapshot_ = scenario_.copyRuntimeMemorySnapshot();
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
    NesFlappyParatroopaScenario& scenario,
    World& world,
    const std::vector<uint8_t>& controllerScript,
    const std::vector<NesRamProbeAddress>& cpuAddresses,
    double deltaTimeSeconds)
{
    NesRamProbeTrace trace;
    trace.cpuAddresses = cpuAddresses;
    trace.frames.reserve(controllerScript.size());

    NesRamProbeStepper stepper{ scenario, world, cpuAddresses, deltaTimeSeconds };
    for (uint8_t controllerMask : controllerScript) {
        trace.frames.push_back(stepper.step(controllerMask));
    }

    return trace;
}

} // namespace DirtSim
