#pragma once

#include <cstdint>
#include <string>

namespace DirtSim {

class SmolnesRuntime {
public:
    bool start(const std::string& romPath);
    bool runFrames(uint32_t frameCount, uint32_t timeoutMs);
    void stop();

    bool isHealthy() const;
    bool isRunning() const;
    uint64_t getRenderedFrameCount() const;
    std::string getLastError() const;
};

} // namespace DirtSim
