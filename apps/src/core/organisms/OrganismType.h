#pragma once

#include "core/StrongType.h"
#include <cstdint>

namespace DirtSim {

/**
 * Types of organisms in the simulation.
 */
enum class OrganismType : uint8_t {
    DUCK = 0,
    GOOSE = 1,
    TREE = 2,
    // Future: BUTTERFLY, FISH, SLIME, etc.
};

using OrganismId = StrongType<struct OrganismIdTag>;
const OrganismId INVALID_ORGANISM_ID{};

} // namespace DirtSim
