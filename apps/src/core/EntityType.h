#pragma once

/**
 * \file
 * Type-safe entity identifier enum.
 * Each value corresponds to a renderable sprite entity type.
 */

#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

enum class EntityType : uint8_t {
    Duck = 0,
    Goose,
    Sparkle,
};

std::string toString(EntityType type);

std::optional<EntityType> fromString(const std::string& str);

} // namespace DirtSim
