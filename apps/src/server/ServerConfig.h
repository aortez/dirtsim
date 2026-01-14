#pragma once

#include "core/ScenarioConfig.h"
#include <nlohmann/json_fwd.hpp>

namespace DirtSim {

struct ServerConfig {
    // Default to Sandbox scenario (not Benchmark which requires 200x200).
    ScenarioConfig startupConfig = Config::Sandbox{};
};

void from_json(const nlohmann::json& j, ServerConfig& cfg);
void to_json(nlohmann::json& j, const ServerConfig& cfg);

} // namespace DirtSim
