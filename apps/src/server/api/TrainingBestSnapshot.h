#pragma once

#include "core/WorldData.h"
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Best snapshot broadcast when a new all-time fitness record is reached.
 * Includes a renderable WorldData snapshot and organism grid.
 */
struct TrainingBestSnapshot {
    WorldData worldData;
    std::vector<OrganismId> organismIds;
    double fitness = 0.0;
    int generation = 0;

    static constexpr const char* name() { return "TrainingBestSnapshot"; }

    using serialize = zpp::bits::members<4>;
};

} // namespace Api
} // namespace DirtSim
