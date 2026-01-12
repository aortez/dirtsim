#pragma once

#include "LightProperties.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim {

/**
 * \file
 * Material type definitions for the pure-material World physics system.
 * Each cell contains one material type with a fill ratio [0,1].
 */

enum class MaterialType : uint8_t {
    AIR = 0, // Empty space (default).
    DIRT,    // Granular solid material.
    LEAF,    // Light organic matter.
    METAL,   // Dense rigid solid.
    ROOT,    // Underground tree tissue (grips soil, extracts nutrients).
    SAND,    // Granular solid (faster settling than dirt).
    SEED,    // Tree seed material (dense, grows into tree).
    WALL,    // Immobile boundary material.
    WATER,   // Fluid material.
    WOOD     // Rigid solid (light).
};

/**
 * Material properties that define physical behavior.
 */
struct MaterialProperties {
    double density;                      // Mass per unit volume (affects gravity response).
    double elasticity;                   // Bounce factor for collisions [0.0-1.0].
    double cohesion;                     // Internal binding strength.
    double adhesion;                     // Binding strength to other materials.
    double air_resistance;               // Air drag coefficient [0.0-1.0].
    double hydrostatic_weight;           // Hydrostatic pressure response [0.0-1.0].
    double pressure_injection_weight;    // Hydrostatic pressure contribution [0.0-1.0].
    double dynamic_weight;               // Dynamic pressure sensitivity [0.0-1.0].
    double pressure_diffusion;           // Pressure propagation rate [0.0-1.0].
    double viscosity;                    // Flow resistance [0.0-1.0].
    double static_friction_coefficient;  // Resistance multiplier when at rest (typically 1.0-1.5).
    double kinetic_friction_coefficient; // Resistance multiplier when moving (typically 0.4-1.0).
    double stick_velocity; // Velocity below which full static friction applies (0.0-0.05).
    double friction_transition_width; // How quickly friction transitions from static to kinetic
                                      // (0.02-0.1).
    bool is_fluid;                    // True for materials that flow freely.
    LightProperties light;
};

const MaterialProperties& getMaterialProperties(MaterialType type);

double getMaterialDensity(MaterialType type);

std::string toString(MaterialType type);

std::optional<MaterialType> fromString(const std::string& str);

const std::vector<MaterialType>& getAllMaterialTypes();

bool isMaterialFluid(MaterialType type);

void setMaterialCohesion(MaterialType type, double cohesion);

/**
 * Calculate velocity-dependent friction coefficient with smooth transition.
 * Returns a value between kinetic and static friction coefficients based on velocity.
 */
double getFrictionCoefficient(double velocity_magnitude, const MaterialProperties& props);

} // namespace DirtSim
