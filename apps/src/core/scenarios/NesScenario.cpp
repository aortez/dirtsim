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
#include <cctype>
#include <fstream>
#include <system_error>
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

std::string normalizeRomId(std::string rawName)
{
    std::string normalized;
    normalized.reserve(rawName.size());

    bool pendingSeparator = false;
    for (char ch : rawName) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            if (pendingSeparator && !normalized.empty() && normalized.back() != '-') {
                normalized.push_back('-');
            }
            normalized.push_back(static_cast<char>(std::tolower(uch)));
            pendingSeparator = false;
            continue;
        }
        pendingSeparator = true;
    }

    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }
    return normalized;
}

bool hasNesExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".nes";
}

std::filesystem::path resolveRomDirectory(const DirtSim::Config::Nes& config)
{
    if (!config.romDirectory.empty()) {
        return config.romDirectory;
    }
    if (!config.romPath.empty()) {
        const std::filesystem::path configuredPath = config.romPath;
        if (configuredPath.has_parent_path()) {
            return configuredPath.parent_path();
        }
    }
    return std::filesystem::path{ "testdata/roms" };
}

std::string describeRomSource(const DirtSim::Config::Nes& config)
{
    if (!config.romId.empty()) {
        return "romId '" + config.romId + "'";
    }
    return "romPath '" + config.romPath + "'";
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
    runtimeResolvedRomId_.clear();

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

    const NesConfigValidationResult validation = validateConfig(config_);
    lastRomCheck_ = validation.romCheck;
    if (!validation.valid) {
        const char* statusText = romCheckStatusToString(lastRomCheck_.status);
        LOG_ERROR(
            Scenario,
            "NesScenario: {} invalid ({}, mapper={}): {}",
            describeRomSource(config_),
            statusText,
            lastRomCheck_.mapper,
            validation.message);
        return;
    }

    LOG_INFO(
        Scenario,
        "NesScenario: ROM '{}' compatible (id='{}', mapper={}, prg16k={}, chr8k={})",
        validation.resolvedRomPath.string(),
        validation.resolvedRomId,
        lastRomCheck_.mapper,
        static_cast<uint32_t>(lastRomCheck_.prgBanks16k),
        static_cast<uint32_t>(lastRomCheck_.chrBanks8k));
    runtimeResolvedRomId_ = validation.resolvedRomId;
    if (!runtime_) {
        runtime_ = std::make_unique<SmolnesRuntime>();
    }

    if (!runtime_->start(validation.resolvedRomPath.string())) {
        LOG_ERROR(
            Scenario, "NesScenario: Failed to start smolnes runtime: {}", runtime_->getLastError());
    }
    else {
        runtime_->setController1State(controller1State_);
    }
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

    const uint64_t framesRemaining = config_.maxEpisodeFrames - renderedFrames;
    const uint32_t framesToRun = static_cast<uint32_t>(std::min<uint64_t>(1u, framesRemaining));

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

std::string NesScenario::getRuntimeResolvedRomId() const
{
    return runtimeResolvedRomId_;
}

std::string NesScenario::getRuntimeLastError() const
{
    if (!runtime_) {
        return {};
    }
    return runtime_->getLastError();
}

std::optional<SmolnesRuntime::MemorySnapshot> NesScenario::copyRuntimeMemorySnapshot() const
{
    if (!runtime_ || !runtime_->isRunning() || !runtime_->isHealthy()) {
        return std::nullopt;
    }
    return runtime_->copyMemorySnapshot();
}

void NesScenario::setController1State(uint8_t buttonMask)
{
    controller1State_ = buttonMask;
    if (runtime_ && runtime_->isRunning()) {
        runtime_->setController1State(controller1State_);
    }
}

std::vector<NesRomCatalogEntry> NesScenario::scanRomCatalog(const std::filesystem::path& romDir)
{
    std::vector<NesRomCatalogEntry> entries;

    std::error_code ec;
    if (romDir.empty() || !std::filesystem::exists(romDir, ec)
        || !std::filesystem::is_directory(romDir, ec)) {
        return entries;
    }

    for (std::filesystem::directory_iterator it(romDir, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }

        const std::filesystem::path romPath = it->path();
        if (!hasNesExtension(romPath)) {
            continue;
        }

        NesRomCatalogEntry entry;
        entry.romPath = romPath;
        entry.displayName = romPath.stem().string();
        entry.romId = makeRomId(entry.displayName);
        entry.check = inspectRom(romPath);
        entries.push_back(std::move(entry));
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const NesRomCatalogEntry& lhs, const NesRomCatalogEntry& rhs) {
            if (lhs.romId != rhs.romId) {
                return lhs.romId < rhs.romId;
            }
            return lhs.romPath.string() < rhs.romPath.string();
        });
    return entries;
}

std::string NesScenario::makeRomId(const std::string& rawName)
{
    return normalizeRomId(rawName);
}

NesConfigValidationResult NesScenario::validateConfig(const Config::Nes& config)
{
    NesConfigValidationResult validation{};

    std::filesystem::path resolvedRomPath;
    if (!config.romId.empty()) {
        const std::string requestedRomId = makeRomId(config.romId);
        if (requestedRomId.empty()) {
            validation.message = "romId must contain at least one alphanumeric character";
            validation.romCheck.status = NesRomCheckStatus::FileNotFound;
            validation.romCheck.message = validation.message;
            return validation;
        }

        const std::filesystem::path romDir = resolveRomDirectory(config);
        const std::vector<NesRomCatalogEntry> entries = scanRomCatalog(romDir);
        std::vector<std::filesystem::path> matchingPaths;
        for (const auto& entry : entries) {
            if (entry.romId == requestedRomId) {
                matchingPaths.push_back(entry.romPath);
            }
        }

        if (matchingPaths.empty()) {
            validation.message =
                "No ROM found for romId '" + config.romId + "' in '" + romDir.string() + "'";
            validation.romCheck.status = NesRomCheckStatus::FileNotFound;
            validation.romCheck.message = validation.message;
            return validation;
        }
        if (matchingPaths.size() > 1) {
            validation.message = "romId '" + config.romId + "' matched multiple ROM files in '"
                + romDir.string() + "'";
            validation.romCheck.status = NesRomCheckStatus::ReadError;
            validation.romCheck.message = validation.message;
            return validation;
        }

        resolvedRomPath = matchingPaths.front();
        validation.resolvedRomId = requestedRomId;
    }
    else {
        if (config.romPath.empty()) {
            validation.message = "romPath must not be empty when romId is not set";
            validation.romCheck.status = NesRomCheckStatus::FileNotFound;
            validation.romCheck.message = validation.message;
            return validation;
        }

        resolvedRomPath = config.romPath;
        validation.resolvedRomId = makeRomId(resolvedRomPath.stem().string());
    }

    validation.romCheck = inspectRom(resolvedRomPath);
    validation.resolvedRomPath = resolvedRomPath;
    validation.valid = validation.romCheck.isCompatible();
    if (!validation.valid) {
        validation.message =
            "ROM '" + resolvedRomPath.string() + "' rejected: " + validation.romCheck.message;
        return validation;
    }

    validation.message = "ROM is compatible";
    return validation;
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
