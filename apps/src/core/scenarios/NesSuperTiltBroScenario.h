#pragma once

#include "NesConfig.h"
#include "core/Timers.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/nes/NesRomValidation.h"
#include "core/scenarios/nes/NesScenarioRuntime.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace DirtSim {

class NesSuperTiltBroScenario : public ScenarioRunner, public NesScenarioRuntime {
public:
    NesSuperTiltBroScenario();
    ~NesSuperTiltBroScenario() override;

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

    const NesRomCheckResult& getLastRomCheck() const;
    bool isRuntimeHealthy() const override;
    bool isRuntimeRunning() const override;
    uint64_t getRuntimeRenderedFrameCount() const override;
    std::optional<ScenarioVideoFrame> copyRuntimeFrameSnapshot() const override;
    std::optional<NesPaletteFrame> copyRuntimePaletteFrame() const override;
    std::string getRuntimeResolvedRomId() const override;
    std::string getRuntimeLastError() const override;
    std::optional<SmolnesRuntime::MemorySnapshot> copyRuntimeMemorySnapshot() const override;
    void setController1State(uint8_t buttonMask) override;

    static NesRomCheckResult inspectRom(const std::filesystem::path& romPath);
    static std::vector<NesRomCatalogEntry> scanRomCatalog(const std::filesystem::path& romDir);
    static std::string makeRomId(const std::string& rawName);
    static NesConfigValidationResult validateConfig(const Config::NesSuperTiltBro& config);
    static bool isMapperSupportedBySmolnes(uint16_t mapper);

private:
    void stopRuntime();
    void updateRuntimeProfilingTimers(Timers& timers);

    ScenarioMetadata metadata_;
    Config::NesSuperTiltBro config_;
    NesRomCheckResult lastRomCheck_;
    std::string runtimeResolvedRomId_;
    std::unique_ptr<SmolnesRuntime> runtime_;
    std::optional<SmolnesRuntime::ProfilingSnapshot> lastRuntimeProfilingSnapshot_;
    uint8_t controller1State_ = 0;
};

} // namespace DirtSim
