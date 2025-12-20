#pragma once

#include <cstdint>

namespace DirtSim {

/**
 * Types of organisms in the simulation.
 */
enum class OrganismType : uint8_t {
    TREE = 0,
    DUCK = 1,
    // Future: BUTTERFLY, FISH, SLIME, etc.
};

using OrganismId = uint32_t;
constexpr OrganismId INVALID_ORGANISM_ID = 0;

} // namespace DirtSim
