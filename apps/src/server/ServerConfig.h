#pragma once

#include "core/ScenarioConfig.h"
#include <nlohmann/json.hpp>

namespace DirtSim {

struct ServerConfig {
    ScenarioConfig startupConfig;
};

inline void from_json(const nlohmann::json& j, ServerConfig& cfg)
{
    DirtSim::from_json(j.at("startupConfig"), cfg.startupConfig);
}

inline void to_json(nlohmann::json& j, const ServerConfig& cfg)
{
    DirtSim::to_json(j["startupConfig"], cfg.startupConfig);
}

} // namespace DirtSim
