#pragma once

#include "NesConfig.h"
#include "core/Timers.h"
#include "core/scenarios/Scenario.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace DirtSim {

enum class NesRomCheckStatus : uint8_t {
    Compatible = 0,
    FileNotFound,
    InvalidHeader,
    ReadError,
    UnsupportedMapper,
};

struct NesRomCheckResult {
    NesRomCheckStatus status = NesRomCheckStatus::FileNotFound;
    uint16_t mapper = 0;
    uint8_t prgBanks16k = 0;
    uint8_t chrBanks8k = 0;
    bool hasBattery = false;
    bool hasTrainer = false;
    bool verticalMirroring = false;
    std::string message;

    bool isCompatible() const { return status == NesRomCheckStatus::Compatible; }
};

struct NesRomCatalogEntry {
    std::string romId;
    std::filesystem::path romPath;
    std::string displayName;
    NesRomCheckResult check;
};

struct NesConfigValidationResult {
    bool valid = false;
    std::filesystem::path resolvedRomPath;
    std::string resolvedRomId;
    NesRomCheckResult romCheck;
    std::string message;
};

class NesFlappyParatroopaScenario : public ScenarioRunner {
public:
    NesFlappyParatroopaScenario();
    ~NesFlappyParatroopaScenario() override;

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

    const NesRomCheckResult& getLastRomCheck() const;
    bool isRuntimeHealthy() const;
    bool isRuntimeRunning() const;
    uint64_t getRuntimeRenderedFrameCount() const;
    std::string getRuntimeResolvedRomId() const;
    std::string getRuntimeLastError() const;
    std::optional<SmolnesRuntime::MemorySnapshot> copyRuntimeMemorySnapshot() const;
    void setController1State(uint8_t buttonMask);

    static NesRomCheckResult inspectRom(const std::filesystem::path& romPath);
    static std::vector<NesRomCatalogEntry> scanRomCatalog(const std::filesystem::path& romDir);
    static std::string makeRomId(const std::string& rawName);
    static NesConfigValidationResult validateConfig(const Config::NesFlappyParatroopa& config);
    static bool isMapperSupportedBySmolnes(uint16_t mapper);

private:
    void stopRuntime();
    void updateRuntimeProfilingTimers(Timers& timers);

    ScenarioMetadata metadata_;
    Config::NesFlappyParatroopa config_;
    NesRomCheckResult lastRomCheck_;
    std::string runtimeResolvedRomId_;
    std::unique_ptr<SmolnesRuntime> runtime_;
    std::optional<SmolnesRuntime::ProfilingSnapshot> lastRuntimeProfilingSnapshot_;
    uint8_t controller1State_ = 0;
};

} // namespace DirtSim
