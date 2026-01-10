#pragma once

#include "ReflectSerializer.h"
#include <cstdint>
#include <nlohmann/json.hpp>

namespace DirtSim {

/**
 * Light system configuration.
 * Controls ambient light, sunlight, and diffusion parameters.
 */
struct LightConfig {
    uint32_t ambient_color = 0x1A1A1EFF; // dayAmbient default.
    bool sun_enabled = true;
    uint32_t sun_color = 0xFFF2D9FF; // warmSunlight default.
    float sun_intensity = 1.0f;
    int diffusion_iterations = 2;
    float diffusion_rate = 0.3f;
};

inline void to_json(nlohmann::json& j, const LightConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, LightConfig& config)
{
    config = ReflectSerializer::from_json<LightConfig>(j);
}

/**
 * @brief Physics simulation parameters.
 *
 * Centralized settings for all physics simulation parameters.
 * Automatically serializable via ReflectSerializer.
 *
 * Use getDefaultPhysicsSettings() to get default values.
 * Default values are in PhysicsSettings.cpp to reduce recompilation.
 */
struct PhysicsSettings {
    double adhesion_strength;
    bool adhesion_enabled;
    double air_resistance;
    double buoyancy_energy_scale;
    double cohesion_resistance_factor;
    double cohesion_strength;
    bool cohesion_enabled;
    double elasticity;
    double fluid_lubrication_factor;
    bool fragmentation_enabled;
    double fragmentation_threshold;      // Minimum energy for fragmentation chance.
    double fragmentation_full_threshold; // Energy for 100% fragmentation.
    double fragmentation_spray_fraction; // Fraction of fill_ratio that sprays out.
    double friction_strength;
    bool friction_enabled;
    double gravity;
    double horizontal_flow_resistance_factor;
    double horizontal_non_fluid_penalty;
    double horizontal_non_fluid_target_resistance;
    double horizontal_non_fluid_energy_multiplier; // Energy cost multiplier for horizontal
                                                   // non-fluid swaps.
    double pressure_dynamic_strength;
    bool pressure_dynamic_enabled;
    double pressure_hydrostatic_strength;
    bool pressure_hydrostatic_enabled;
    double pressure_scale;
    double pressure_diffusion_strength;
    int pressure_diffusion_iterations;
    double pressure_decay_rate; // Decay rate per second (0.0 = no decay, 1.0 = 100%/sec).
    bool swap_enabled;
    double timescale;
    double viscosity_strength;
    bool viscosity_enabled;
    LightConfig light;
};

/**
 * @brief Get default physics settings.
 *
 * Returns a PhysicsSettings struct with sensible defaults.
 * Defined in PhysicsSettings.cpp to reduce recompilation when tweaking values.
 */
PhysicsSettings getDefaultPhysicsSettings();

/**
 * ADL functions for automatic JSON conversion.
 */
inline void to_json(nlohmann::json& j, const PhysicsSettings& settings)
{
    j = ReflectSerializer::to_json(settings);
}

inline void from_json(const nlohmann::json& j, PhysicsSettings& settings)
{
    settings = ReflectSerializer::from_json<PhysicsSettings>(j);
}

} // namespace DirtSim
