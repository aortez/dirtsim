#pragma once

#include "core/ScenarioConfig.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace DirtSim {

struct ServerConfig {
    ScenarioConfig startupConfig = Config::Sandbox{};
    std::optional<std::filesystem::path> dataDir;
};

void from_json(const nlohmann::json& j, ServerConfig& cfg);
void to_json(nlohmann::json& j, const ServerConfig& cfg);

} // namespace DirtSim
