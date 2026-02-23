#pragma once

#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

/**
 * Runtime surface used by scenario-driven NES training.
 */
class NesScenarioRuntime {
public:
    virtual ~NesScenarioRuntime() = default;

    virtual bool isRuntimeHealthy() const = 0;
    virtual bool isRuntimeRunning() const = 0;
    virtual uint64_t getRuntimeRenderedFrameCount() const = 0;
    virtual std::optional<ScenarioVideoFrame> copyRuntimeFrameSnapshot() const = 0;
    virtual std::optional<SmolnesRuntime::MemorySnapshot> copyRuntimeMemorySnapshot() const = 0;
    virtual std::string getRuntimeResolvedRomId() const = 0;
    virtual std::string getRuntimeLastError() const = 0;
    virtual void setController1State(uint8_t buttonMask) = 0;
};

} // namespace DirtSim
