#include "NesSuperTiltBroScenario.h"

#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"

#include <algorithm>
#include <utility>

namespace {

const char* romCheckStatusToString(DirtSim::NesRomCheckStatus status)
{
    switch (status) {
        case DirtSim::NesRomCheckStatus::Compatible:
            return "compatible";
        case DirtSim::NesRomCheckStatus::FileNotFound:
            return "file_not_found";
        case DirtSim::NesRomCheckStatus::InvalidHeader:
            return "invalid_header";
        case DirtSim::NesRomCheckStatus::ReadError:
            return "read_error";
        case DirtSim::NesRomCheckStatus::UnsupportedMapper:
            return "unsupported_mapper";
    }
    return "unknown";
}

std::string describeRomSource(const DirtSim::Config::NesSuperTiltBro& config)
{
    if (!config.romId.empty()) {
        return "romId '" + config.romId + "'";
    }
    return "romPath '" + config.romPath + "'";
}

} // namespace

namespace DirtSim {

NesSuperTiltBroScenario::NesSuperTiltBroScenario() : driver_(Scenario::EnumType::NesSuperTiltBro)
{
    metadata_.kind = ScenarioKind::NesWorld;
    metadata_.name = "NES Super Tilt Bro";
    metadata_.description = "NES Super Tilt Bro (UNROM no-network) training scenario";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 47;
    metadata_.requiredHeight = 30;
}

NesSuperTiltBroScenario::~NesSuperTiltBroScenario() = default;

const ScenarioMetadata& NesSuperTiltBroScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig NesSuperTiltBroScenario::getConfig() const
{
    return config_;
}

void NesSuperTiltBroScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    if (!std::holds_alternative<Config::NesSuperTiltBro>(newConfig)) {
        LOG_ERROR(Scenario, "NesSuperTiltBroScenario: Invalid config type provided");
        return;
    }

    config_ = std::get<Config::NesSuperTiltBro>(newConfig);
    const auto driverResult = driver_.setConfig(newConfig);
    if (driverResult.isError()) {
        LOG_ERROR(
            Scenario,
            "NesSuperTiltBroScenario: Failed to apply driver config: {}",
            driverResult.errorValue());
    }
    LOG_INFO(Scenario, "NesSuperTiltBroScenario: Config updated");
}

void NesSuperTiltBroScenario::setup(World& world)
{
    world.getData().scenario_video_frame.reset();

    const auto driverConfigResult = driver_.setConfig(ScenarioConfig{ config_ });
    if (driverConfigResult.isError()) {
        LOG_ERROR(
            Scenario,
            "NesSuperTiltBroScenario: {} rejected: {}",
            describeRomSource(config_),
            driverConfigResult.errorValue());
        return;
    }

    const auto setupResult = driver_.setup();
    if (setupResult.isError()) {
        const char* statusText = romCheckStatusToString(driver_.getLastRomCheck().status);
        LOG_ERROR(
            Scenario,
            "NesSuperTiltBroScenario: {} invalid ({}, mapper={}): {}",
            describeRomSource(config_),
            statusText,
            driver_.getLastRomCheck().mapper,
            setupResult.errorValue());
    }
}

void NesSuperTiltBroScenario::reset(World& world)
{
    setup(world);
}

void NesSuperTiltBroScenario::tick(World& world, double /*deltaTime*/)
{
    driver_.tick(world.getTimers(), world.getData().scenario_video_frame);
}

const NesRomCheckResult& NesSuperTiltBroScenario::getLastRomCheck() const
{
    return driver_.getLastRomCheck();
}

bool NesSuperTiltBroScenario::isRuntimeHealthy() const
{
    return driver_.isRuntimeHealthy();
}

bool NesSuperTiltBroScenario::isRuntimeRunning() const
{
    return driver_.isRuntimeRunning();
}

uint64_t NesSuperTiltBroScenario::getRuntimeRenderedFrameCount() const
{
    return driver_.getRuntimeRenderedFrameCount();
}

std::optional<ScenarioVideoFrame> NesSuperTiltBroScenario::copyRuntimeFrameSnapshot() const
{
    return driver_.copyRuntimeFrameSnapshot();
}

std::optional<NesPaletteFrame> NesSuperTiltBroScenario::copyRuntimePaletteFrame() const
{
    return driver_.copyRuntimePaletteFrame();
}

std::string NesSuperTiltBroScenario::getRuntimeResolvedRomId() const
{
    return driver_.getRuntimeResolvedRomId();
}

std::string NesSuperTiltBroScenario::getRuntimeLastError() const
{
    return driver_.getRuntimeLastError();
}

std::optional<SmolnesRuntime::MemorySnapshot> NesSuperTiltBroScenario::copyRuntimeMemorySnapshot()
    const
{
    return driver_.copyRuntimeMemorySnapshot();
}

void NesSuperTiltBroScenario::setController1State(uint8_t buttonMask)
{
    driver_.setController1State(buttonMask);
}

NesRomCheckResult NesSuperTiltBroScenario::inspectRom(const std::filesystem::path& romPath)
{
    return inspectNesRom(romPath);
}

std::vector<NesRomCatalogEntry> NesSuperTiltBroScenario::scanRomCatalog(
    const std::filesystem::path& romDir)
{
    return scanNesRomCatalog(romDir);
}

std::string NesSuperTiltBroScenario::makeRomId(const std::string& rawName)
{
    return makeNesRomId(rawName);
}

NesConfigValidationResult NesSuperTiltBroScenario::validateConfig(
    const Config::NesSuperTiltBro& config)
{
    return validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
}

bool NesSuperTiltBroScenario::isMapperSupportedBySmolnes(uint16_t mapper)
{
    return isNesMapperSupportedBySmolnes(mapper);
}

} // namespace DirtSim
