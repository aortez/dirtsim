#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace DirtSim {

class World;

// Floor modification that challenges the duck.
enum class FloorObstacleType { HURDLE, PIT };

struct FloorObstacle {
    int start_x = 0;
    int width = 1;
    FloorObstacleType type = FloorObstacleType::HURDLE;
};

/**
 * Manages floor obstacles (hurdles and pits) for the clock scenario.
 * Hurdles are wall cells one row above the floor. Pits are gaps in the floor.
 */
class ObstacleManager {
public:
    bool spawnObstacle(
        World& world, std::mt19937& rng, std::uniform_real_distribution<double>& uniform_dist);

    void clearAll(World& world);

    bool isHurdleAt(uint32_t x) const;
    bool isPitAt(uint32_t x) const;
    const std::vector<FloorObstacle>& getObstacles() const;

private:
    std::vector<FloorObstacle> obstacles_;

    static constexpr int MARGIN = 5;
    static constexpr size_t MAX_OBSTACLES = 3;
};

} // namespace DirtSim
