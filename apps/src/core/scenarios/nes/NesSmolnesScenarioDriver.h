#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/ScenarioId.h"
#include "core/Timers.h"
#include "core/scenarios/nes/NesRomValidation.h"
#include "core/scenarios/nes/NesScenarioRuntime.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {

/**
 * Shared, composition-based driver for NES scenarios that run via SmolNES.
 *
 * This is deliberately World-free: it owns the emulator runtime and exposes
 * snapshots + controller input. Callers can decide how to surface frames (UI,
 * training, headless runs, etc.).
 */
class NesSmolnesScenarioDriver final : public NesScenarioRuntime {
public:
    explicit NesSmolnesScenarioDriver(Scenario::EnumType scenarioId);
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
    void reset();

    void tick(Timers& timers, std::optional<ScenarioVideoFrame>& scenarioVideoFrame);

    bool isRuntimeHealthy() const override;
    bool isRuntimeRunning() const override;
    uint64_t getRuntimeRenderedFrameCount() const override;
    std::optional<ScenarioVideoFrame> copyRuntimeFrameSnapshot() const override;
    std::optional<NesPaletteFrame> copyRuntimePaletteFrame() const override;
    std::optional<SmolnesRuntime::MemorySnapshot> copyRuntimeMemorySnapshot() const override;
    std::string getRuntimeResolvedRomId() const override;
    std::string getRuntimeLastError() const override;
    void setController1State(uint8_t buttonMask) override;

private:
    void stopRuntime();
    void updateRuntimeProfilingTimers(Timers& timers);
    NesConfigValidationResult validateConfig() const;

    Scenario::EnumType scenarioId_ = Scenario::EnumType::Empty;
    ScenarioConfig config_;
    NesRomCheckResult lastRomCheck_;
    std::string runtimeResolvedRomId_;
    std::unique_ptr<SmolnesRuntime> runtime_;
    std::optional<SmolnesRuntime::ProfilingSnapshot> lastRuntimeProfilingSnapshot_;
    uint8_t controller1State_ = 0;
};

} // namespace DirtSim
