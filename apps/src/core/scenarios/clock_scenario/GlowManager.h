#pragma once

#include "GlowConfig.h"
#include "core/Vector2.h"
#include <vector>

namespace DirtSim {

class World;

/**
 * Manages emissive cell glow for the clock scenario.
 *
 * Stateless utility that applies emissiveness to specified cell positions.
 * Water cells are auto-detected by material type.
 *
 * Call apply() late in the tick cycle, after all cell modifications complete.
 */
class GlowManager {
public:
    static void apply(
        World& world,
        const std::vector<Vector2i>& digitPositions,
        const std::vector<Vector2i>& floorPositions,
        const std::vector<Vector2i>& obstaclePositions,
        const std::vector<Vector2i>& wallPositions,
        const GlowConfig& config);
};

} // namespace DirtSim
