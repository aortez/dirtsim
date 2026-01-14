#pragma once

/**
 * \file
 * Material type definitions for the pure-material World physics system.
 * Each cell contains one material type with a fill ratio [0,1].
 */

#include "LightProperties.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim::Material {

enum class EnumType : uint8_t {
    Air = 0, // Empty space (default).
    Dirt,    // Granular solid material.
    Leaf,    // Light organic matter.
    Metal,   // Dense rigid solid.
    Root,    // Underground tree tissue (grips soil, extracts nutrients).
    Sand,    // Granular solid (faster settling than dirt).
    Seed,    // Tree seed material (dense, grows into tree).
    Wall,    // Immobile boundary material.
    Water,   // Fluid material.
    Wood,    // Rigid solid (light).
};

/**
 * Material properties that define physical behavior.
 */
struct Properties {
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

std::string toString(EnumType type);

std::optional<EnumType> fromString(const std::string& str);

const std::vector<EnumType>& getAllTypes();

const Properties& getProperties(EnumType type);

double getDensity(EnumType type);

bool isFluid(EnumType type);

/**
 * Calculate velocity-dependent friction coefficient with smooth transition.
 * Returns a value between kinetic and static friction coefficients based on velocity.
 */
double getFrictionCoefficient(double velocity_magnitude, const Properties& props);

} // namespace DirtSim::Material
