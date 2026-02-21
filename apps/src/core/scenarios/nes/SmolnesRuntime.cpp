#include "SmolnesRuntime.h"

#include "SmolnesRuntimeBackend.h"

namespace DirtSim {

bool SmolnesRuntime::start(const std::string& romPath)
{
    return smolnesRuntimeStart(romPath.c_str());
}

bool SmolnesRuntime::runFrames(uint32_t frameCount, uint32_t timeoutMs)
{
    return smolnesRuntimeRunFrames(frameCount, timeoutMs);
}

void SmolnesRuntime::stop()
{
    smolnesRuntimeStop();
}

bool SmolnesRuntime::isHealthy() const
{
    return smolnesRuntimeIsHealthy();
}

bool SmolnesRuntime::isRunning() const
{
    return smolnesRuntimeIsRunning();
}

uint64_t SmolnesRuntime::getRenderedFrameCount() const
{
    return smolnesRuntimeGetRenderedFrameCount();
}

std::optional<ScenarioVideoFrame> SmolnesRuntime::copyLatestFrame() const
{
    ScenarioVideoFrame frame;
    frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
    frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
    frame.pixels.resize(SMOLNES_RUNTIME_FRAME_BYTES);

    if (!smolnesRuntimeCopyLatestFrame(
            reinterpret_cast<uint8_t*>(frame.pixels.data()),
            static_cast<uint32_t>(frame.pixels.size()),
            &frame.frame_id)) {
        return std::nullopt;
    }

    return frame;
}

std::string SmolnesRuntime::getLastError() const
{
    char buffer[256] = { 0 };
    smolnesRuntimeGetLastErrorCopy(buffer, sizeof(buffer));
    return std::string{ buffer };
}

} // namespace DirtSim
