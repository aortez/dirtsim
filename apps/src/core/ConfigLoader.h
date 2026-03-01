#pragma once

#include "Result.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim {

/**
 * @brief Loads configuration files with multi-path search and .local override support.
 *
 * Search order (first match wins):
 * 1. Explicit config directory (if set via setConfigDir)
 * 2. ./config/ (CWD - for development)
 * 3. ~/.config/dirtsim/ (user overrides)
 * 4. /etc/dirtsim/ (system defaults)
 *
 * At each location, checks for .local version first (e.g., foo.json.local),
 * then falls back to base file (e.g., foo.json). The .local file is a complete
 * replacement, not a merge.
 */
class ConfigLoader {
public:
    static void setConfigDir(const std::string& path);
    static void clearConfigDir();

    template <typename T>
    static Result<T, std::string> load(const std::string& filename);

    static std::optional<std::filesystem::path> findConfigFile(const std::string& filename);
    static std::vector<std::filesystem::path> getSearchPaths();

private:
    static std::optional<std::string> explicitConfigDir_;
    static Result<nlohmann::json, std::string> loadJson(const std::string& filename);
    static Result<nlohmann::json, std::string> tryLoadJson(const std::filesystem::path& path);
};

// Template implementation.
template <typename T>
Result<T, std::string> ConfigLoader::load(const std::string& filename)
{
    auto jsonResult = loadJson(filename);
    if (jsonResult.isError()) {
        return Result<T, std::string>::error(jsonResult.errorValue());
    }

    try {
        T config;
        // Use unqualified call to enable ADL (argument-dependent lookup).
        from_json(jsonResult.value(), config);
        return Result<T, std::string>::okay(config);
    }
    catch (const std::exception& e) {
        return Result<T, std::string>::error("Failed to parse " + filename + ": " + e.what());
    }
}

} // namespace DirtSim
