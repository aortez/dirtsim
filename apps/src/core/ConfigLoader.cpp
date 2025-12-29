#include "ConfigLoader.h"
#include <cstdlib>
#include <fstream>
#include <spdlog/spdlog.h>

namespace DirtSim {

std::optional<std::string> ConfigLoader::explicitConfigDir_ = std::nullopt;

void ConfigLoader::setConfigDir(const std::string& path)
{
    explicitConfigDir_ = path;
}

void ConfigLoader::clearConfigDir()
{
    explicitConfigDir_ = std::nullopt;
}

std::vector<std::filesystem::path> ConfigLoader::getSearchPaths()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;

    // 1. Explicit config directory (highest priority).
    if (explicitConfigDir_.has_value()) {
        paths.push_back(fs::path(explicitConfigDir_.value()));
    }

    // 2. ./config/ (CWD - for development).
    paths.push_back(fs::current_path() / "config");

    // 3. ~/.config/dirtsim/ (user overrides).
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(fs::path(home) / ".config" / "dirtsim");
    }

    // 4. /etc/dirtsim/ (system defaults).
    paths.push_back(fs::path("/etc/dirtsim"));

    return paths;
}

std::optional<std::filesystem::path> ConfigLoader::findConfigFile(const std::string& filename)
{
    namespace fs = std::filesystem;

    for (const auto& dir : getSearchPaths()) {
        // Check .local version first.
        fs::path localPath = dir / (filename + ".local");
        if (fs::exists(localPath) && fs::is_regular_file(localPath)) {
            return localPath;
        }

        // Check base file.
        fs::path basePath = dir / filename;
        if (fs::exists(basePath) && fs::is_regular_file(basePath)) {
            return basePath;
        }
    }

    return std::nullopt;
}

std::optional<nlohmann::json> ConfigLoader::tryLoadJson(const std::filesystem::path& path)
{
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::warn("ConfigLoader: Cannot open file: {}", path.string());
            return std::nullopt;
        }

        nlohmann::json config = nlohmann::json::parse(file);
        return config;
    }
    catch (const nlohmann::json::parse_error& e) {
        spdlog::error("ConfigLoader: Failed to parse {}: {}", path.string(), e.what());
        return std::nullopt;
    }
    catch (const std::exception& e) {
        spdlog::error("ConfigLoader: Error reading {}: {}", path.string(), e.what());
        return std::nullopt;
    }
}

std::optional<nlohmann::json> ConfigLoader::load(const std::string& filename)
{
    auto path = findConfigFile(filename);
    if (!path.has_value()) {
        spdlog::debug("ConfigLoader: Config file not found: {}", filename);
        return std::nullopt;
    }

    spdlog::info("ConfigLoader: Loading config from {}", path->string());
    return tryLoadJson(path.value());
}

nlohmann::json ConfigLoader::loadWithDefault(
    const std::string& filename, const nlohmann::json& defaultConfig)
{
    auto config = load(filename);
    if (config.has_value()) {
        return config.value();
    }

    spdlog::info("ConfigLoader: Using default config for {}", filename);
    return defaultConfig;
}

} // namespace DirtSim
