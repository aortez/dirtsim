#pragma once

#include "Vector2.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Sparkle particle for rendering.
 *
 * Lightweight struct for sparkle visual data. The physics simulation
 * happens in Duck::DuckSparkle; this is just what gets sent to the renderer.
 */
struct SparkleParticle {
    Vector2<float> position{ 0.0f, 0.0f };  // Absolute world position.
    float opacity = 1.0f;                    // 0.0 = invisible, 1.0 = fully visible.

    using serialize = zpp::bits::members<2>;
};

// JSON serialization for SparkleParticle.
void to_json(nlohmann::json& j, const SparkleParticle& s);
void from_json(const nlohmann::json& j, SparkleParticle& s);

/**
 * @brief Entity types for world overlays.
 *
 * Entities are sprite-based objects that exist in the world but render
 * as images rather than cell materials. They have physics (position,
 * velocity, mass) and can interact with the simulation.
 */
enum class EntityType : uint8_t {
    DUCK = 0,
    SPARKLE = 1,  // Legacy standalone sparkle (may be removed).
    // Future: BUTTERFLY, BIRD, FISH, etc.
};

NLOHMANN_JSON_SERIALIZE_ENUM(
    EntityType,
    {
        { EntityType::DUCK, "duck" },
        { EntityType::SPARKLE, "sparkle" },
    })

/**
 * @brief World entity with physics and rendering state.
 *
 * Entities are sprite-based objects (duck, sparkle, butterfly, etc.) that exist
 * in the world coordinate system. They have:
 * - Position in cell coordinates + COM for sub-cell precision
 * - Velocity for smooth movement
 * - Facing direction for sprite orientation
 * - Mass for physics interactions
 *
 * The UI renders entities as image sprites at their world position,
 * overlaid on top of the cell grid.
 */
struct Entity {
    uint32_t id = 0;
    EntityType type = EntityType::DUCK;
    bool visible = true;

    // Physics state (all vectors for consistency).
    Vector2<float> position{ 0.0f, 0.0f };  // Cell coordinates.
    Vector2<float> com{ 0.0f, 0.0f };       // Sub-cell offset [-1, 1].
    Vector2<float> velocity{ 0.0f, 0.0f };  // Cells per second.
    Vector2<float> facing{ 1.0f, 0.0f };    // Direction (normalized).
    float mass = 1.0f;

    // Attached sparkle particles (used by DUCK entities).
    std::vector<SparkleParticle> sparkles;

    using serialize = zpp::bits::members<9>;
};

// JSON serialization via ReflectSerializer (declared in Entity.cpp).
void to_json(nlohmann::json& j, const Entity& e);
void from_json(const nlohmann::json& j, Entity& e);

} // namespace DirtSim
