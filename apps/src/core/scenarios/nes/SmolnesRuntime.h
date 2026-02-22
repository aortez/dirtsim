#pragma once

#include "core/RenderMessage.h"

#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

class SmolnesRuntime {
public:
    bool start(const std::string& romPath);
    bool runFrames(uint32_t frameCount, uint32_t timeoutMs);
    void stop();
    void setController1State(uint8_t buttonMask);

    bool isHealthy() const;
    bool isRunning() const;
    uint64_t getRenderedFrameCount() const;
    std::optional<ScenarioVideoFrame> copyLatestFrame() const;
    std::string getLastError() const;
};

} // namespace DirtSim
