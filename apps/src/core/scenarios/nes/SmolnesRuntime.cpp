#include "SmolnesRuntime.h"

#include "SmolnesRuntimeBackend.h"

namespace DirtSim {

namespace {

constexpr const char* kCreateRuntimeError = "Failed to allocate smolnes runtime backend instance.";

} // namespace

SmolnesRuntime::SmolnesRuntime() : runtimeHandle_(smolnesRuntimeCreate())
{}

SmolnesRuntime::~SmolnesRuntime()
{
    if (runtimeHandle_ == nullptr) {
        return;
    }

    smolnesRuntimeDestroy(runtimeHandle_);
    runtimeHandle_ = nullptr;
}

bool SmolnesRuntime::start(const std::string& romPath)
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeStart(runtimeHandle_, romPath.c_str());
}

bool SmolnesRuntime::runFrames(uint32_t frameCount, uint32_t timeoutMs)
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeRunFrames(runtimeHandle_, frameCount, timeoutMs);
}

void SmolnesRuntime::stop()
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeStop(runtimeHandle_);
}

void SmolnesRuntime::setController1State(uint8_t buttonMask)
{
    if (runtimeHandle_ == nullptr) {
        return;
    }
    smolnesRuntimeSetController1State(runtimeHandle_, buttonMask);
}

bool SmolnesRuntime::isHealthy() const
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeIsHealthy(runtimeHandle_);
}

bool SmolnesRuntime::isRunning() const
{
    if (runtimeHandle_ == nullptr) {
        return false;
    }
    return smolnesRuntimeIsRunning(runtimeHandle_);
}

uint64_t SmolnesRuntime::getRenderedFrameCount() const
{
    if (runtimeHandle_ == nullptr) {
        return 0;
    }
    return smolnesRuntimeGetRenderedFrameCount(runtimeHandle_);
}

std::optional<ScenarioVideoFrame> SmolnesRuntime::copyLatestFrame() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    ScenarioVideoFrame frame;
    frame.width = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_WIDTH);
    frame.height = static_cast<uint16_t>(SMOLNES_RUNTIME_FRAME_HEIGHT);
    frame.pixels.resize(SMOLNES_RUNTIME_FRAME_BYTES);

    if (!smolnesRuntimeCopyLatestFrame(
            runtimeHandle_,
            reinterpret_cast<uint8_t*>(frame.pixels.data()),
            static_cast<uint32_t>(frame.pixels.size()),
            &frame.frame_id)) {
        return std::nullopt;
    }

    return frame;
}

std::optional<SmolnesRuntime::MemorySnapshot> SmolnesRuntime::copyMemorySnapshot() const
{
    if (runtimeHandle_ == nullptr) {
        return std::nullopt;
    }

    MemorySnapshot snapshot{};
    if (!smolnesRuntimeCopyCpuRam(
            runtimeHandle_, snapshot.cpuRam.data(), SMOLNES_RUNTIME_CPU_RAM_BYTES)) {
        return std::nullopt;
    }
    if (!smolnesRuntimeCopyPrgRam(
            runtimeHandle_, snapshot.prgRam.data(), SMOLNES_RUNTIME_PRG_RAM_BYTES)) {
        return std::nullopt;
    }
    return snapshot;
}

std::string SmolnesRuntime::getLastError() const
{
    if (runtimeHandle_ == nullptr) {
        return std::string{ kCreateRuntimeError };
    }

    char buffer[256] = { 0 };
    smolnesRuntimeGetLastErrorCopy(runtimeHandle_, buffer, sizeof(buffer));
    return std::string{ buffer };
}

} // namespace DirtSim
