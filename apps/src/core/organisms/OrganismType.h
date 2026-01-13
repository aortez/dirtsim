#pragma once

#include "core/StrongType.h"

#include <cstdint>

namespace DirtSim {

enum class OrganismType : uint8_t { DUCK = 0, GOOSE = 1, TREE = 2 };

using OrganismId = StrongType<struct OrganismIdTag>;
const OrganismId INVALID_ORGANISM_ID{};

} // namespace DirtSim
