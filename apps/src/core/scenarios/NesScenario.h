#pragma once

#include "NesConfig.h"
#include "core/scenarios/Scenario.h"

#include <cstdint>
#include <filesystem>
#include <string>

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

class NesScenario : public ScenarioRunner {
public:
    NesScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

    const NesRomCheckResult& getLastRomCheck() const;

    static NesRomCheckResult inspectRom(const std::filesystem::path& romPath);
    static bool isMapperSupportedBySmolnes(uint16_t mapper);

private:
    ScenarioMetadata metadata_;
    Config::Nes config_;
    NesRomCheckResult lastRomCheck_;
};

} // namespace DirtSim
