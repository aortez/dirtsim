#include "MaterialType.h"
#include "reflect.h"

#include <array>
#include <cassert>

namespace DirtSim::Material {

// Material property database.
// Each material is defined using designated initializers for easy editing and understanding.
static std::array<Properties, 10> MATERIAL_PROPERTIES = {
    { // ========== Air ==========
      // Nearly massless, high elasticity, no cohesion/adhesion, very high pressure diffusion.
      { .density = 0.001,
        .elasticity = 1.0,
        .cohesion = 0.0,
        .adhesion = 0.0,
        .air_resistance = 0.0,
        .hydrostatic_weight = 1.0,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 0.0,
        .pressure_diffusion = 1.0,
        .viscosity = 0.001,
        .static_friction_coefficient = 1.0,
        .kinetic_friction_coefficient = 1.0,
        .stick_velocity = 0.0,
        .friction_transition_width = 0.01,
        .is_fluid = true,
        .light = { .opacity = 0.0f, .scatter = 0.0f } },

      // ========== Dirt ==========
      { .density = 1.5,
        .elasticity = 0.2,
        .cohesion = 0.2,
        .adhesion = 0.3,
        .air_resistance = 0.05,
        .hydrostatic_weight = 0.25,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 1.0,
        .pressure_diffusion = 0.3,
        .viscosity = 0.2,
        .static_friction_coefficient = 1.5,
        .kinetic_friction_coefficient = 0.75,
        .stick_velocity = 0.1,
        .friction_transition_width = 0.10,
        .is_fluid = false,
        .light = { .opacity = 0.7f, .scatter = 0.2f, .tint = 0x8B6914FF } },

      // ========== Leaf ==========
      { .density = 0.3,
        .elasticity = 0.4,
        .cohesion = 0.7,
        .adhesion = 0.3,
        .air_resistance = 0.8,
        .hydrostatic_weight = 1.0,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 0.6,
        .pressure_diffusion = 0.6,
        .viscosity = 0.2,
        .static_friction_coefficient = 0.5,
        .kinetic_friction_coefficient = 0.3,
        .stick_velocity = 0.03,
        .friction_transition_width = 0.06,
        .is_fluid = false,
        .light = { .opacity = 0.1f, .scatter = 0.3f, .tint = 0x228B22FF } },

      // ========== Metal ==========
      { .density = 7.8,
        .elasticity = 0.8,
        .cohesion = 1.0,
        .adhesion = 0.1,
        .air_resistance = 0.1,
        .hydrostatic_weight = 0.0,        // Rigid materials don't respond to pressure.
        .pressure_injection_weight = 1.0, // But they do contribute their weight.
        .dynamic_weight = 0.5,
        .pressure_diffusion = 0.1,
        .viscosity = 1,
        .static_friction_coefficient = 1.5,
        .kinetic_friction_coefficient = 1.0,
        .stick_velocity = 0.01,
        .friction_transition_width = 0.02,
        .is_fluid = false,
        .light = { .opacity = 1.0f, .scatter = 0.8f } },

      // ========== Root ==========
      // Underground tree tissue that grips soil and forms networks.
      { .density = 1.2,
        .elasticity = 0.3,
        .cohesion = 0.8,
        .adhesion = 0.6,
        .air_resistance = 0.3,
        .hydrostatic_weight = 1.0,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 0.7,
        .pressure_diffusion = 0.4,
        .viscosity = 0.7,
        .static_friction_coefficient = 1.2,
        .kinetic_friction_coefficient = 0.8,
        .stick_velocity = 0.03,
        .friction_transition_width = 0.05,
        .is_fluid = false,
        .light = { .opacity = 0.7f, .scatter = 0.15f, .tint = 0x5C4033FF } },

      // ========== Sand ==========
      { .density = 1.8,
        .elasticity = 0.2,
        .cohesion = 0.2,
        .adhesion = 0.1,
        .air_resistance = 0.2,
        .hydrostatic_weight = 1.0,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 1.0,
        .pressure_diffusion = 0.3,
        .viscosity = 0.3,
        .static_friction_coefficient = 0.6,
        .kinetic_friction_coefficient = 0.4,
        .stick_velocity = 0.04,
        .friction_transition_width = 0.08,
        .is_fluid = false,
        .light = { .opacity = 0.4f, .scatter = 0.2f, .tint = 0xE6D5ACFF } },

      // ========== Seed ==========
      { .density = 1.5,
        .elasticity = 0.2,
        .cohesion = 0.9,
        .adhesion = 0.3,
        .air_resistance = 0.2,
        .hydrostatic_weight = 0.0,        // Rigid materials don't respond to pressure.
        .pressure_injection_weight = 1.0, // But they do contribute their weight.
        .dynamic_weight = 0.5,
        .pressure_diffusion = 0.1,
        .viscosity = 0.8,
        .static_friction_coefficient = 1.3,
        .kinetic_friction_coefficient = 0.9,
        .stick_velocity = 0.02,
        .friction_transition_width = 0.03,
        .is_fluid = false,
        .light = { .opacity = 0.3f,
                   .scatter = 0.2f,
                   .tint = 0x90EE90FF,
                   .emission = 0.1f,
                   .emission_color = 0x80FF80FF } },

      // ========== Wall ==========
      { .density = 1000.0,
        .elasticity = 0.9,
        .cohesion = 1.0,
        .adhesion = 0.5,
        .air_resistance = 0.0,
        .hydrostatic_weight = 0.0,        // Rigid materials don't respond to pressure.
        .pressure_injection_weight = 0.0, // Immobile boundary - doesn't inject.
        .dynamic_weight = 0.0,
        .pressure_diffusion = 0.0,
        .viscosity = 1.0,
        .static_friction_coefficient = 1.0,
        .kinetic_friction_coefficient = 1.0,
        .stick_velocity = 0.0,
        .friction_transition_width = 0.01,
        .is_fluid = false,
        .light = { .opacity = 1.0f, .scatter = 0.3f } },

      // ========== Water ==========
      { .density = 1.0,
        .elasticity = 0.1,
        .cohesion = 0.1,
        .adhesion = 0.3,
        .air_resistance = 0.01,
        .hydrostatic_weight = 1,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 0.8,
        .pressure_diffusion = 0.9,
        .viscosity = 0.1,
        .static_friction_coefficient = 0,
        .kinetic_friction_coefficient = 0.01,
        .stick_velocity = 0.0,
        .friction_transition_width = 0.001,
        .is_fluid = true,
        .light = { .opacity = 0.03f, .scatter = 0.5f, .tint = 0xCCE6FFFF } },

      // ========== Wood ==========
      { .density = 0.3,
        .elasticity = 0.6,
        .cohesion = 0.7,
        .adhesion = 0.3,
        .air_resistance = 0.05,
        .hydrostatic_weight = 0.0,
        .pressure_injection_weight = 1.0,
        .dynamic_weight = 0.5,
        .pressure_diffusion = 0.15,
        .viscosity = 1,
        .static_friction_coefficient = 1.3,
        .kinetic_friction_coefficient = 0.9,
        .stick_velocity = 0.02,
        .friction_transition_width = 0.03,
        .is_fluid = false,
        .light = { .opacity = 0.6f, .scatter = 0.2f, .tint = 0xDEB887FF } } }
};

const Properties& getProperties(EnumType type)
{
    const auto index = static_cast<size_t>(type);
    assert(index < MATERIAL_PROPERTIES.size());
    return MATERIAL_PROPERTIES[index];
}

double getDensity(EnumType type)
{
    return getProperties(type).density;
}

bool isFluid(EnumType type)
{
    return getProperties(type).is_fluid;
}

std::string toString(EnumType type)
{
    return std::string(reflect::enum_name(type));
}

std::optional<EnumType> fromString(const std::string& str)
{
    for (const auto& [value, name] : reflect::enumerators<EnumType>) {
        if (name == str) {
            return static_cast<EnumType>(value);
        }
    }
    return std::nullopt;
}

const std::vector<EnumType>& getAllTypes()
{
    // Build list once using reflection - automatically includes all EnumType values.
    static std::vector<EnumType> materials = []() {
        std::vector<EnumType> result;
        for (const auto& [value, name] : reflect::enumerators<EnumType>) {
            result.push_back(static_cast<EnumType>(value));
        }
        return result;
    }();
    return materials;
}

double getFrictionCoefficient(double velocity_magnitude, const Properties& props)
{
    // Below stick velocity, use full static friction.
    if (velocity_magnitude < props.stick_velocity) {
        return props.static_friction_coefficient;
    }

    // Calculate smooth transition parameter.
    double t = (velocity_magnitude - props.stick_velocity) / props.friction_transition_width;

    // Clamp t to [0, 1] range.
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    // Smooth cubic interpolation (3t² - 2t³).
    double smooth_t = t * t * (3.0 - 2.0 * t);

    // Interpolate between static and kinetic friction.
    return props.static_friction_coefficient * (1.0 - smooth_t)
        + props.kinetic_friction_coefficient * smooth_t;
}

} // namespace DirtSim::Material
