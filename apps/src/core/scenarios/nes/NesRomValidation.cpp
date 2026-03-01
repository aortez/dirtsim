#include "core/scenarios/nes/NesRomValidation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>

namespace DirtSim {

namespace {

constexpr std::array<uint16_t, 6> kSmolnesSupportedMappers = { 0, 1, 2, 3, 4, 7 };

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

std::filesystem::path resolveRomDirectory(
    const std::string& romDirectory, const std::string& romPath)
{
    if (!romDirectory.empty()) {
        return romDirectory;
    }
    if (!romPath.empty()) {
        const std::filesystem::path configuredPath = romPath;
        if (configuredPath.has_parent_path()) {
            return configuredPath.parent_path();
        }
    }
    return std::filesystem::path{ "testdata/roms" };
}

} // namespace

NesRomCheckResult inspectNesRom(const std::filesystem::path& romPath)
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

    if (!isNesMapperSupportedBySmolnes(result.mapper)) {
        result.status = NesRomCheckStatus::UnsupportedMapper;
        result.message = "Mapper is unsupported by smolnes.";
        return result;
    }

    result.status = NesRomCheckStatus::Compatible;
    result.message = "ROM is compatible with smolnes mapper support.";
    return result;
}

std::vector<NesRomCatalogEntry> scanNesRomCatalog(const std::filesystem::path& romDir)
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
        entry.romId = makeNesRomId(entry.displayName);
        entry.check = inspectNesRom(romPath);
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

std::string makeNesRomId(const std::string& rawName)
{
    return normalizeRomId(rawName);
}

NesConfigValidationResult validateNesRomSelection(
    const std::string& romId, const std::string& romDirectory, const std::string& romPath)
{
    NesConfigValidationResult validation{};

    std::filesystem::path resolvedRomPath;
    if (!romId.empty()) {
        const std::string requestedRomId = makeNesRomId(romId);
        if (requestedRomId.empty()) {
            validation.message = "romId must contain at least one alphanumeric character";
            validation.romCheck.status = NesRomCheckStatus::FileNotFound;
            validation.romCheck.message = validation.message;
            return validation;
        }

        const std::filesystem::path romDir = resolveRomDirectory(romDirectory, romPath);
        const std::vector<NesRomCatalogEntry> entries = scanNesRomCatalog(romDir);
        std::vector<std::filesystem::path> matchingPaths;
        for (const auto& entry : entries) {
            if (entry.romId == requestedRomId) {
                matchingPaths.push_back(entry.romPath);
            }
        }

        if (matchingPaths.empty()) {
            if (!romPath.empty()) {
                const std::filesystem::path fallbackRomPath = romPath;
                const std::string fallbackRomId = makeNesRomId(fallbackRomPath.stem().string());
                if (fallbackRomId == requestedRomId) {
                    resolvedRomPath = fallbackRomPath;
                    validation.resolvedRomId = requestedRomId;
                }
            }

            if (resolvedRomPath.empty()) {
                validation.message =
                    "No ROM found for romId '" + romId + "' in '" + romDir.string() + "'";
                validation.romCheck.status = NesRomCheckStatus::FileNotFound;
                validation.romCheck.message = validation.message;
                return validation;
            }
        }
        else if (matchingPaths.size() > 1) {
            validation.message =
                "romId '" + romId + "' matched multiple ROM files in '" + romDir.string() + "'";
            validation.romCheck.status = NesRomCheckStatus::ReadError;
            validation.romCheck.message = validation.message;
            return validation;
        }
        else {
            resolvedRomPath = matchingPaths.front();
            validation.resolvedRomId = requestedRomId;
        }
    }
    else {
        if (romPath.empty()) {
            validation.message = "romPath must not be empty when romId is not set";
            validation.romCheck.status = NesRomCheckStatus::FileNotFound;
            validation.romCheck.message = validation.message;
            return validation;
        }

        resolvedRomPath = romPath;
        validation.resolvedRomId = makeNesRomId(resolvedRomPath.stem().string());
    }

    validation.romCheck = inspectNesRom(resolvedRomPath);
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

bool isNesMapperSupportedBySmolnes(uint16_t mapper)
{
    for (const uint16_t supportedMapper : kSmolnesSupportedMappers) {
        if (supportedMapper == mapper) {
            return true;
        }
    }
    return false;
}

} // namespace DirtSim
