#include "ObstacleManager.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

bool ObstacleManager::spawnObstacle(
    World& world, std::mt19937& rng, std::uniform_real_distribution<double>& uniform_dist)
{
    if (obstacles_.size() >= MAX_OBSTACLES) {
        return false;
    }

    const WorldData& data = world.getData();

    // Require minimum world size for obstacles.
    int min_x = MARGIN;
    int max_x = data.width - MARGIN - 1;

    if (max_x <= min_x) {
        spdlog::info(
            "ObstacleManager: World too narrow for floor obstacles (width={})", data.width);
        return false;
    }

    // Choose obstacle width (1-3 contiguous cells).
    std::uniform_int_distribution<int> width_dist(1, 3);
    int obstacle_width = width_dist(rng);

    // Adjust max_x to account for obstacle width.
    int spawn_max_x = max_x - (obstacle_width - 1);
    if (spawn_max_x < min_x) {
        return false;
    }

    // Pick starting X position.
    std::uniform_int_distribution<int> x_dist(min_x, spawn_max_x);
    int start_x = x_dist(rng);

    // Choose type: hurdle or pit.
    FloorObstacleType type =
        (uniform_dist(rng) < 0.5) ? FloorObstacleType::HURDLE : FloorObstacleType::PIT;

    // Check for overlap with existing obstacles.
    for (const auto& existing : obstacles_) {
        int existing_end = existing.start_x + existing.width;
        int new_end = start_x + obstacle_width;
        if (start_x < existing_end && new_end > existing.start_x) {
            spdlog::debug("ObstacleManager: Spawn skipped - overlaps existing");
            return false;
        }
    }

    obstacles_.push_back({
        .start_x = start_x,
        .width = obstacle_width,
        .type = type,
    });

    spdlog::info(
        "ObstacleManager: Spawned {} at x={}, width={}",
        type == FloorObstacleType::HURDLE ? "HURDLE" : "PIT",
        start_x,
        obstacle_width);

    return true;
}

void ObstacleManager::clearAll(World& world)
{
    if (obstacles_.empty()) {
        return;
    }

    WorldData& data = world.getData();
    uint32_t height = data.height;

    spdlog::info("ObstacleManager: Clearing {} floor obstacles", obstacles_.size());

    for (const auto& obs : obstacles_) {
        for (int i = 0; i < obs.width; ++i) {
            int x = obs.start_x + i;
            if (x < 0 || x >= data.width) {
                continue;
            }

            if (obs.type == FloorObstacleType::HURDLE) {
                // Clear hurdle wall at height-2.
                if (height > 2) {
                    Cell& cell = data.at(x, height - 2);
                    if (cell.material_type == Material::EnumType::Wall) {
                        cell = Cell();
                    }
                }
            }
            else {
                // Restore floor at height-1 for pit.
                world.replaceMaterialAtCell(
                    { static_cast<int16_t>(x), static_cast<int16_t>(height - 1) },
                    Material::EnumType::Wall);
            }
        }
    }

    obstacles_.clear();
}

bool ObstacleManager::isHurdleAt(uint32_t x) const
{
    for (const auto& obs : obstacles_) {
        if (obs.type != FloorObstacleType::HURDLE) {
            continue;
        }
        if (static_cast<int>(x) >= obs.start_x && static_cast<int>(x) < obs.start_x + obs.width) {
            return true;
        }
    }
    return false;
}

bool ObstacleManager::isPitAt(uint32_t x) const
{
    for (const auto& obs : obstacles_) {
        if (obs.type != FloorObstacleType::PIT) {
            continue;
        }
        if (static_cast<int>(x) >= obs.start_x && static_cast<int>(x) < obs.start_x + obs.width) {
            return true;
        }
    }
    return false;
}

const std::vector<FloorObstacle>& ObstacleManager::getObstacles() const
{
    return obstacles_;
}

} // namespace DirtSim
