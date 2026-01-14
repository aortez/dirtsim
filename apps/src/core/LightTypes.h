#pragma once

#include "Vector2.h"
#include <cstdint>
#include <string>
#include <variant>

namespace DirtSim {

/**
 * Localized light source with position, color, intensity, and falloff.
 *
 * Point lights are omnidirectional, radiating light equally in all
 * directions. Light intensity falls off with distance squared.
 */
struct PointLight {
    Vector2f position;
    uint32_t color = 0xFFFFFFFF;
    float intensity = 1.0f;
    float radius = 20.0f;
    float attenuation = 0.1f;
};

/**
 * Directional light source that illuminates a cone-shaped area.
 *
 * Useful for flashlights, searchlights, and other directional lighting.
 * Direction is measured in radians from the positive x-axis.
 */
struct SpotLight {
    Vector2f position;
    uint32_t color = 0xFFFFFFFF;
    float intensity = 1.0f;
    float radius = 20.0f;
    float attenuation = 0.1f;
    float direction = 0.0f;
    float arc_width = 1.0f;
    float focus = 0.0f;
};

/**
 * Spot light with automatic rotation capability.
 *
 * When rotation_speed is non-zero, the direction automatically increments
 * each frame. Set rotation_speed to zero for manual direction control.
 */
struct RotatingLight {
    Vector2f position;
    uint32_t color = 0xFFFFFFFF;
    float intensity = 1.0f;
    float radius = 20.0f;
    float attenuation = 0.1f;
    float direction = 0.0f;
    float arc_width = 1.0f;
    float focus = 0.0f;
    float rotation_speed = 0.0f;
};

/**
 * Forward-declarable light wrapper class.
 *
 * Wraps the light variant to enable forward declaration,
 * reducing compilation dependencies.
 */
class Light {
public:
    using Variant = std::variant<PointLight, SpotLight, RotatingLight>;

    template <typename T>
    Light(T&& light) : variant_(std::forward<T>(light))
    {}

    Light() = default;

    Variant& getVariant() { return variant_; }
    const Variant& getVariant() const { return variant_; }

private:
    Variant variant_;
};

std::string getLightTypeName(const Light& light);

} // namespace DirtSim
