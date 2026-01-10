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

Result<nlohmann::json, std::string> ConfigLoader::tryLoadJson(const std::filesystem::path& path)
{
    namespace fs = std::filesystem;

    try {
        // Check for empty file first.
        if (fs::file_size(path) == 0) {
            std::string error = "Empty config file: " + path.string();
            spdlog::warn("ConfigLoader: {}", error);
            return Result<nlohmann::json, std::string>::error(error);
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            std::string error = "Cannot open config file: " + path.string();
            spdlog::warn("ConfigLoader: {}", error);
            return Result<nlohmann::json, std::string>::error(error);
        }

        nlohmann::json config = nlohmann::json::parse(file);
        return Result<nlohmann::json, std::string>::okay(config);
    }
    catch (const nlohmann::json::parse_error& e) {
        std::string error = "Parse error in " + path.string() + ": " + e.what();
        spdlog::error("ConfigLoader: {}", error);
        return Result<nlohmann::json, std::string>::error(error);
    }
    catch (const std::exception& e) {
        std::string error = "Error reading " + path.string() + ": " + e.what();
        spdlog::error("ConfigLoader: {}", error);
        return Result<nlohmann::json, std::string>::error(error);
    }
}

Result<nlohmann::json, std::string> ConfigLoader::loadJson(const std::string& filename)
{
    auto path = findConfigFile(filename);
    if (!path.has_value()) {
        std::string error = "Config file not found: " + filename;
        spdlog::debug("ConfigLoader: {}", error);
        return Result<nlohmann::json, std::string>::error(error);
    }

    spdlog::info("ConfigLoader: Loading config from {}", path->string());
    return tryLoadJson(path.value());
}

} // namespace DirtSim
