#include "NesRamProbe.h"

#include "core/World.h"

#include <fstream>

namespace DirtSim {

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

    for (size_t i = 0; i < controllerScript.size(); ++i) {
        const uint8_t controllerMask = controllerScript[i];
        scenario.setController1State(controllerMask);
        scenario.tick(world, deltaTimeSeconds);

        NesRamProbeFrame frame;
        frame.frame = i;
        frame.controllerMask = controllerMask;
        frame.cpuRamValues.resize(cpuAddresses.size(), 0u);

        const auto snapshot = scenario.copyRuntimeMemorySnapshot();
        if (snapshot.has_value()) {
            for (size_t addrIndex = 0; addrIndex < cpuAddresses.size(); ++addrIndex) {
                const uint16_t address = cpuAddresses[addrIndex].address;
                if (address < snapshot->cpuRam.size()) {
                    frame.cpuRamValues[addrIndex] = snapshot->cpuRam[address];
                }
            }
        }

        trace.frames.push_back(frame);
    }

    return trace;
}

} // namespace DirtSim
