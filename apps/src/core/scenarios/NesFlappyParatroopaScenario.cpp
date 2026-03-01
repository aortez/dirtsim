#include "NesFlappyParatroopaScenario.h"

#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"

#include <algorithm>
#include <cctype>
#include <system_error>
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

std::string describeRomSource(const DirtSim::Config::NesFlappyParatroopa& config)
{
    if (!config.romId.empty()) {
        return "romId '" + config.romId + "'";
    }
    return "romPath '" + config.romPath + "'";
}

} // namespace

namespace DirtSim {

NesFlappyParatroopaScenario::NesFlappyParatroopaScenario()
    : driver_(Scenario::EnumType::NesFlappyParatroopa)
{
    metadata_.kind = ScenarioKind::NesWorld;
    metadata_.name = "NES Flappy Paratroopa";
    metadata_.description = "NES Flappy Paratroopa World training scenario";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 47;
    metadata_.requiredHeight = 30;
}

NesFlappyParatroopaScenario::~NesFlappyParatroopaScenario() = default;

const ScenarioMetadata& NesFlappyParatroopaScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig NesFlappyParatroopaScenario::getConfig() const
{
    return config_;
}

void NesFlappyParatroopaScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    if (!std::holds_alternative<Config::NesFlappyParatroopa>(newConfig)) {
        LOG_ERROR(Scenario, "NesFlappyParatroopaScenario: Invalid config type provided");
        return;
    }

    config_ = std::get<Config::NesFlappyParatroopa>(newConfig);
    const auto driverResult = driver_.setConfig(newConfig);
    if (driverResult.isError()) {
        LOG_ERROR(
            Scenario,
            "NesFlappyParatroopaScenario: Failed to apply driver config: {}",
            driverResult.errorValue());
    }
    LOG_INFO(Scenario, "NesFlappyParatroopaScenario: Config updated");
}

void NesFlappyParatroopaScenario::setup(World& world)
{
    world.getData().scenario_video_frame.reset();

    const auto driverConfigResult = driver_.setConfig(ScenarioConfig{ config_ });
    if (driverConfigResult.isError()) {
        LOG_ERROR(
            Scenario,
            "NesFlappyParatroopaScenario: {} rejected: {}",
            describeRomSource(config_),
            driverConfigResult.errorValue());
        return;
    }

    const auto setupResult = driver_.setup();
    if (setupResult.isError()) {
        const char* statusText = romCheckStatusToString(driver_.getLastRomCheck().status);
        LOG_ERROR(
            Scenario,
            "NesFlappyParatroopaScenario: {} invalid ({}, mapper={}): {}",
            describeRomSource(config_),
            statusText,
            driver_.getLastRomCheck().mapper,
            setupResult.errorValue());
    }
}

void NesFlappyParatroopaScenario::reset(World& world)
{
    setup(world);
}

void NesFlappyParatroopaScenario::tick(World& world, double /*deltaTime*/)
{
    driver_.tick(world.getTimers(), world.getData().scenario_video_frame);
}

const NesRomCheckResult& NesFlappyParatroopaScenario::getLastRomCheck() const
{
    return driver_.getLastRomCheck();
}

bool NesFlappyParatroopaScenario::isRuntimeHealthy() const
{
    return driver_.isRuntimeHealthy();
}

bool NesFlappyParatroopaScenario::isRuntimeRunning() const
{
    return driver_.isRuntimeRunning();
}

uint64_t NesFlappyParatroopaScenario::getRuntimeRenderedFrameCount() const
{
    return driver_.getRuntimeRenderedFrameCount();
}

std::optional<ScenarioVideoFrame> NesFlappyParatroopaScenario::copyRuntimeFrameSnapshot() const
{
    return driver_.copyRuntimeFrameSnapshot();
}

std::optional<NesPaletteFrame> NesFlappyParatroopaScenario::copyRuntimePaletteFrame() const
{
    return driver_.copyRuntimePaletteFrame();
}

std::string NesFlappyParatroopaScenario::getRuntimeResolvedRomId() const
{
    return driver_.getRuntimeResolvedRomId();
}

std::string NesFlappyParatroopaScenario::getRuntimeLastError() const
{
    return driver_.getRuntimeLastError();
}

std::optional<SmolnesRuntime::MemorySnapshot> NesFlappyParatroopaScenario::
    copyRuntimeMemorySnapshot() const
{
    return driver_.copyRuntimeMemorySnapshot();
}

void NesFlappyParatroopaScenario::setController1State(uint8_t buttonMask)
{
    driver_.setController1State(buttonMask);
}

std::vector<NesRomCatalogEntry> NesFlappyParatroopaScenario::scanRomCatalog(
    const std::filesystem::path& romDir)
{
    return scanNesRomCatalog(romDir);
}

std::string NesFlappyParatroopaScenario::makeRomId(const std::string& rawName)
{
    return makeNesRomId(rawName);
}

NesConfigValidationResult NesFlappyParatroopaScenario::validateConfig(
    const Config::NesFlappyParatroopa& config)
{
    return validateNesRomSelection(config.romId, config.romDirectory, config.romPath);
}

NesRomCheckResult NesFlappyParatroopaScenario::inspectRom(const std::filesystem::path& romPath)
{
    return inspectNesRom(romPath);
}

bool NesFlappyParatroopaScenario::isMapperSupportedBySmolnes(uint16_t mapper)
{
    return isNesMapperSupportedBySmolnes(mapper);
}

} // namespace DirtSim
