#include "NesScenario.h"

#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

namespace {

constexpr std::array<uint16_t, 6> kSmolnesSupportedMappers = { 0, 1, 2, 3, 4, 7 };

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

} // namespace

namespace DirtSim {

NesScenario::NesScenario()
{
    metadata_.name = "NES";
    metadata_.description = "NES ROM runner scaffold for smolnes-compatible mapper workflows";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 47;
    metadata_.requiredHeight = 30;
    runtime_ = std::make_unique<SmolnesRuntime>();
}

NesScenario::~NesScenario()
{
    stopRuntime();
}

const ScenarioMetadata& NesScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig NesScenario::getConfig() const
{
    return config_;
}

void NesScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    if (!std::holds_alternative<Config::Nes>(newConfig)) {
        LOG_ERROR(Scenario, "NesScenario: Invalid config type provided");
        return;
    }

    config_ = std::get<Config::Nes>(newConfig);
    LOG_INFO(Scenario, "NesScenario: Config updated");
}

void NesScenario::setup(World& world)
{
    stopRuntime();
    world.getData().scenario_video_frame.reset();
    controller1State_ = 0;

    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }
    world.getOrganismManager().clear();

    for (int x = 0; x < world.getData().width; ++x) {
        world.getData()
            .at(x, world.getData().height - 1)
            .replaceMaterial(Material::EnumType::Wall, 1.0);
    }
    for (int y = 0; y < world.getData().height; ++y) {
        world.getData().at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0);
        world.getData()
            .at(world.getData().width - 1, y)
            .replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    lastRomCheck_ = inspectRom(config_.romPath);
    if (lastRomCheck_.isCompatible()) {
        LOG_INFO(
            Scenario,
            "NesScenario: ROM '{}' compatible (mapper={}, prg16k={}, chr8k={})",
            config_.romPath,
            lastRomCheck_.mapper,
            static_cast<uint32_t>(lastRomCheck_.prgBanks16k),
            static_cast<uint32_t>(lastRomCheck_.chrBanks8k));
        if (!runtime_) {
            runtime_ = std::make_unique<SmolnesRuntime>();
        }

        if (!runtime_->start(config_.romPath)) {
            LOG_ERROR(
                Scenario,
                "NesScenario: Failed to start smolnes runtime: {}",
                runtime_->getLastError());
        }
        else {
            runtime_->setController1State(controller1State_);
        }
        return;
    }

    const char* statusText = romCheckStatusToString(lastRomCheck_.status);
    if (config_.requireSmolnesMapper
        && lastRomCheck_.status == NesRomCheckStatus::UnsupportedMapper) {
        LOG_ERROR(
            Scenario,
            "NesScenario: ROM '{}' rejected ({}, mapper={})",
            config_.romPath,
            statusText,
            lastRomCheck_.mapper);
        return;
    }

    LOG_WARN(
        Scenario,
        "NesScenario: ROM '{}' check failed ({}, mapper={})",
        config_.romPath,
        statusText,
        lastRomCheck_.mapper);
}

void NesScenario::reset(World& world)
{
    setup(world);
}

void NesScenario::tick(World& world, double /*deltaTime*/)
{
    if (!runtime_ || !runtime_->isRunning()) {
        return;
    }
    if (!runtime_->isHealthy()) {
        LOG_ERROR(Scenario, "NesScenario: smolnes runtime unhealthy: {}", runtime_->getLastError());
        stopRuntime();
        return;
    }

    const uint64_t renderedFrames = runtime_->getRenderedFrameCount();
    if (renderedFrames >= config_.maxEpisodeFrames) {
        return;
    }

    const uint32_t maxFramesPerTick = std::max<uint32_t>(1, config_.frameSkip);
    const uint64_t framesRemaining = config_.maxEpisodeFrames - renderedFrames;
    const uint32_t framesToRun =
        static_cast<uint32_t>(std::min<uint64_t>(maxFramesPerTick, framesRemaining));

    constexpr uint32_t tickTimeoutMs = 2000;
    runtime_->setController1State(controller1State_);
    if (!runtime_->runFrames(framesToRun, tickTimeoutMs)) {
        LOG_ERROR(
            Scenario,
            "NesScenario: smolnes frame step failed after {} frames: {}",
            runtime_->getRenderedFrameCount(),
            runtime_->getLastError());
        world.getData().scenario_video_frame.reset();
        stopRuntime();
        return;
    }

    auto frame = runtime_->copyLatestFrame();
    if (frame.has_value()) {
        world.getData().scenario_video_frame = std::move(frame.value());
    }
}

const NesRomCheckResult& NesScenario::getLastRomCheck() const
{
    return lastRomCheck_;
}

bool NesScenario::isRuntimeHealthy() const
{
    return runtime_ && runtime_->isHealthy();
}

bool NesScenario::isRuntimeRunning() const
{
    return runtime_ && runtime_->isRunning();
}

uint64_t NesScenario::getRuntimeRenderedFrameCount() const
{
    if (!runtime_) {
        return 0;
    }
    return runtime_->getRenderedFrameCount();
}

std::string NesScenario::getRuntimeLastError() const
{
    if (!runtime_) {
        return {};
    }
    return runtime_->getLastError();
}

void NesScenario::setController1State(uint8_t buttonMask)
{
    controller1State_ = buttonMask;
    if (runtime_ && runtime_->isRunning()) {
        runtime_->setController1State(controller1State_);
    }
}

NesRomCheckResult NesScenario::inspectRom(const std::filesystem::path& romPath)
{
    NesRomCheckResult result{};
    if (!std::filesystem::exists(romPath)) {
        result.status = NesRomCheckStatus::FileNotFound;
        result.message = "ROM path does not exist.";
        return result;
    }

    std::ifstream romFile(romPath, std::ios::binary);
    if (!romFile.is_open()) {
        result.status = NesRomCheckStatus::ReadError;
        result.message = "Failed to open ROM file.";
        return result;
    }

    std::array<uint8_t, 16> header{};
    romFile.read(
        reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (romFile.gcount() != static_cast<std::streamsize>(header.size())) {
        result.status = NesRomCheckStatus::ReadError;
        result.message = "Failed to read iNES header.";
        return result;
    }

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        result.status = NesRomCheckStatus::InvalidHeader;
        result.message = "ROM is missing iNES magic bytes.";
        return result;
    }

    result.prgBanks16k = header[4];
    result.chrBanks8k = header[5];
    const uint8_t flags6 = header[6];
    const uint8_t flags7 = header[7];
    result.mapper = static_cast<uint16_t>((flags6 >> 4) | (flags7 & 0xF0));
    result.hasBattery = (flags6 & 0x02) != 0;
    result.hasTrainer = (flags6 & 0x04) != 0;
    result.verticalMirroring = (flags6 & 0x01) != 0;

    if (!isMapperSupportedBySmolnes(result.mapper)) {
        result.status = NesRomCheckStatus::UnsupportedMapper;
        result.message = "Mapper is unsupported by smolnes.";
        return result;
    }

    result.status = NesRomCheckStatus::Compatible;
    result.message = "ROM is compatible with smolnes mapper support.";
    return result;
}

bool NesScenario::isMapperSupportedBySmolnes(uint16_t mapper)
{
    for (const uint16_t supportedMapper : kSmolnesSupportedMappers) {
        if (supportedMapper == mapper) {
            return true;
        }
    }
    return false;
}

void NesScenario::stopRuntime()
{
    if (!runtime_) {
        return;
    }
    runtime_->stop();
}

} // namespace DirtSim
