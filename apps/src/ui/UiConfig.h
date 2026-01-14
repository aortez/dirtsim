#pragma once

#include <nlohmann/json.hpp>

namespace DirtSim {

/**
 * @brief UI startup configuration.
 *
 * Loaded from ui.json via ConfigLoader search paths:
 * 1. --config-dir (if specified)
 * 2. ./config/ (development)
 * 3. ~/.config/dirtsim/ (user overrides)
 * 4. /etc/dirtsim/ (system defaults)
 *
 * Use .local override files (e.g., ui.json.local) for per-device customization.
 */
struct UiConfig {
    // Skip StartMenu and go directly to SimRunning on startup.
    bool autoRun = false;
};

inline void from_json(const nlohmann::json& j, UiConfig& cfg)
{
    if (j.contains("autoRun")) {
        j.at("autoRun").get_to(cfg.autoRun);
    }
}

inline void to_json(nlohmann::json& j, const UiConfig& cfg)
{
    j["autoRun"] = cfg.autoRun;
}

} // namespace DirtSim
