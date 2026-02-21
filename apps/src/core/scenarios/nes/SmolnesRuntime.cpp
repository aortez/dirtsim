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

std::string SmolnesRuntime::getLastError() const
{
    char buffer[256] = { 0 };
    smolnesRuntimeGetLastErrorCopy(buffer, sizeof(buffer));
    return std::string{ buffer };
}

} // namespace DirtSim
