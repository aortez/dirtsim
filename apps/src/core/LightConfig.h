#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace DirtSim {

enum class LightMode : uint8_t {
    Propagated = 0,
    Fast = 1,
    FlatBasic = 2,
};

struct LightConfig {
    LightMode mode = LightMode::Propagated;
    bool air_fast_path;
    uint32_t ambient_color;
    float ambient_intensity;
    uint32_t sky_color;
    float sky_intensity;
    int steps_per_frame;
    uint32_t sun_color;
    float sun_intensity;
    float temporal_decay;
    bool temporal_persistence;
};

void applyLightModePreset(LightConfig& config, LightMode mode);
LightConfig getDefaultLightConfig();
void to_json(nlohmann::json& j, const LightConfig& config);
void from_json(const nlohmann::json& j, LightConfig& config);

} // namespace DirtSim
