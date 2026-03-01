#pragma once

#include <cstdint>
#include <filesystem>
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

NesRomCheckResult inspectNesRom(const std::filesystem::path& romPath);
std::vector<NesRomCatalogEntry> scanNesRomCatalog(const std::filesystem::path& romDir);
std::string makeNesRomId(const std::string& rawName);
NesConfigValidationResult validateNesRomSelection(
    const std::string& romId, const std::string& romDirectory, const std::string& romPath);
bool isNesMapperSupportedBySmolnes(uint16_t mapper);

} // namespace DirtSim
