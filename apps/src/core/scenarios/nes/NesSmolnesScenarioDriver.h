#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/ScenarioId.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesControllerTelemetry.h"
#include "core/scenarios/nes/NesPpuSnapshot.h"
#include "core/scenarios/nes/NesRomValidation.h"
#include "core/scenarios/nes/NesScenarioRuntime.h"
#include "core/scenarios/nes/NesSuperMarioBrosResponseProbe.h"
#include "core/scenarios/nes/NesSuperMarioBrosResponseTelemetry.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {

class NesAudioPlayer;

/**
 * Shared, composition-based driver for NES scenarios that run via SmolNES.
 *
 * This is deliberately World-free: it owns the emulator runtime and exposes
 * snapshots + controller input. Callers can decide how to surface frames (UI,
 * training, headless runs, etc.).
 */
class NesSmolnesScenarioDriver final : public NesScenarioRuntime {
public:
    struct RuntimeConfig {
        std::function<std::unique_ptr<SmolnesRuntime>()> runtimeFactory;
    };

    struct StepResult {
        uint64_t advancedFrames = 0;
        uint64_t renderedFramesAfter = 0;
        uint64_t renderedFramesBefore = 0;
        uint8_t controllerMask = 0;
        bool runtimeHealthy = false;
        bool runtimeRunning = false;
        std::optional<NesControllerTelemetry> controllerTelemetry = std::nullopt;
        std::optional<NesSuperMarioBrosResponseTelemetry> smbResponseTelemetry = std::nullopt;
        std::optional<SmolnesRuntime::MemorySnapshot> memorySnapshot = std::nullopt;
        std::optional<NesPaletteFrame> paletteFrame = std::nullopt;
        std::optional<ScenarioVideoFrame> scenarioVideoFrame = std::nullopt;
        std::string lastError;
    };

    explicit NesSmolnesScenarioDriver(Scenario::EnumType scenarioId);
    NesSmolnesScenarioDriver(Scenario::EnumType scenarioId, RuntimeConfig runtimeConfig);
    ~NesSmolnesScenarioDriver() override;

    NesSmolnesScenarioDriver(const NesSmolnesScenarioDriver&) = delete;
    NesSmolnesScenarioDriver& operator=(const NesSmolnesScenarioDriver&) = delete;
    NesSmolnesScenarioDriver(NesSmolnesScenarioDriver&&) noexcept;
    NesSmolnesScenarioDriver& operator=(NesSmolnesScenarioDriver&&) noexcept;

    Scenario::EnumType getScenarioId() const { return scenarioId_; }

    ScenarioConfig getConfig() const { return config_; }
    Result<std::monostate, std::string> setConfig(const ScenarioConfig& config);

    const NesRomCheckResult& getLastRomCheck() const { return lastRomCheck_; }

    Result<std::monostate, std::string> setup();
    Result<std::monostate, std::string> reset();

    StepResult step(Timers& timers, uint8_t buttonMask);
    void tick(Timers& timers, std::optional<ScenarioVideoFrame>& scenarioVideoFrame);

    bool isRuntimeHealthy() const override;
    bool isRuntimeRunning() const override;
    uint64_t getRuntimeRenderedFrameCount() const override;
    std::optional<ScenarioVideoFrame> copyRuntimeFrameSnapshot() const override;
    std::optional<NesPaletteFrame> copyRuntimePaletteFrame() const override;
    std::optional<SmolnesRuntime::MemorySnapshot> copyRuntimeMemorySnapshot() const override;
    std::optional<NesPpuSnapshot> copyRuntimePpuSnapshot() const;
    std::optional<SmolnesRuntime::Savestate> copyRuntimeSavestate() const;
    std::optional<SmolnesRuntime::ProfilingSnapshot> copyRuntimeProfilingSnapshot() const;
    std::optional<SmolnesRuntime::ApuSnapshot> copyRuntimeApuSnapshot() const;
    uint32_t copyRuntimeApuSamples(float* buffer, uint32_t maxSamples) const;
    bool loadRuntimeSavestate(const SmolnesRuntime::Savestate& savestate, uint32_t timeoutMs);
    std::string getRuntimeResolvedRomId() const override;
    std::string getRuntimeLastError() const override;
    std::optional<NesControllerTelemetry> getLastControllerTelemetry() const;
    std::optional<NesSuperMarioBrosResponseTelemetry> getLastSmbResponseTelemetry() const;
    void setController1State(uint8_t buttonMask) override;
    void setLiveController1State(uint8_t buttonMask, uint64_t observedTimestampNs);

    void setApuEnabled(bool enabled);
    void setAudioPlaybackEnabled(bool enabled);
    void setPixelOutputEnabled(bool enabled);
    void setRgbaOutputEnabled(bool enabled);
    void setAudioVolumePercent(int percent);
    void setDetailedTimingEnabled(bool enabled);
    void setLiveServerPacingEnabled(bool enabled);
    void setSmbResponseProbeEnabled(bool enabled);

private:
    static RuntimeConfig makeDefaultRuntimeConfig();
    void applyRuntimePacingMode();
    void stopRuntime();
    void updateRuntimeProfilingTimers(Timers& timers);
    NesConfigValidationResult validateConfig() const;

    Scenario::EnumType scenarioId_ = Scenario::EnumType::Empty;
    ScenarioConfig config_;
    NesRomCheckResult lastRomCheck_;
    std::string runtimeResolvedRomId_;
    RuntimeConfig runtimeConfig_;
    std::unique_ptr<SmolnesRuntime> runtime_;
    std::unique_ptr<NesAudioPlayer> audioPlayer_;
    std::optional<NesControllerTelemetry> lastControllerTelemetry_;
    std::optional<NesSuperMarioBrosResponseTelemetry> lastSmbResponseTelemetry_;
    std::optional<SmolnesRuntime::ProfilingSnapshot> lastRuntimeProfilingSnapshot_;
    bool audioPlaybackEnabled_ = false;
    bool liveServerPacingEnabled_ = false;
    bool smbResponseProbeEnabled_ = false;
    uint8_t controller1State_ = 0;
    NesSuperMarioBrosResponseProbe smbResponseProbe_;
};

} // namespace DirtSim
