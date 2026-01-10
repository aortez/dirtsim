#include "RigidBodyCollisionComponent.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <cmath>

namespace DirtSim {

CollisionResult RigidBodyCollisionComponent::detect(
    const World& world,
    OrganismId organismId,
    const std::vector<Vector2i>& currentCells,
    const std::vector<Vector2i>& predictedCells)
{
    CollisionResult result;
    const WorldData& data = world.getData();

    // Compute organism center from current cells for contact normal calculation.
    Vector2d organismCenter{ 0.0, 0.0 };
    if (!currentCells.empty()) {
        for (const auto& cell : currentCells) {
            organismCenter.x += static_cast<double>(cell.x);
            organismCenter.y += static_cast<double>(cell.y);
        }
        organismCenter.x /= static_cast<double>(currentCells.size());
        organismCenter.y /= static_cast<double>(currentCells.size());
    }

    Vector2d normalSum{ 0.0, 0.0 };

    for (const auto& cellPos : predictedCells) {
        // Check world boundaries.
        if (cellPos.x < 0 || cellPos.y < 0
            || static_cast<uint32_t>(cellPos.x) >= data.width
            || static_cast<uint32_t>(cellPos.y) >= data.height) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);

            // Boundary normal points inward.
            if (cellPos.x < 0) {
                normalSum.x += 1.0;
            }
            if (cellPos.x >= static_cast<int>(data.width)) {
                normalSum.x -= 1.0;
            }
            if (cellPos.y < 0) {
                normalSum.y += 1.0;
            }
            if (cellPos.y >= static_cast<int>(data.height)) {
                normalSum.y -= 1.0;
            }
            continue;
        }

        const Cell& cell = data.at(cellPos.x, cellPos.y);

        // Check for WALL.
        if (cell.material_type == MaterialType::WALL) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);

            // Compute normal from organism center toward blocking cell.
            Vector2d toObstacle{
                static_cast<double>(cellPos.x) - organismCenter.x,
                static_cast<double>(cellPos.y) - organismCenter.y
            };
            double len = std::sqrt(toObstacle.x * toObstacle.x + toObstacle.y * toObstacle.y);
            if (len > 0.0001) {
                // Normal points away from obstacle (opposite direction).
                normalSum.x -= toObstacle.x / len;
                normalSum.y -= toObstacle.y / len;
            }
            continue;
        }

        // Check for other organism.
        OrganismId cellOrg = world.getOrganismManager().at(cellPos);
        if (cellOrg != INVALID_ORGANISM_ID && cellOrg != organismId) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);

            // Compute normal from organism center toward blocking cell.
            Vector2d toObstacle{
                static_cast<double>(cellPos.x) - organismCenter.x,
                static_cast<double>(cellPos.y) - organismCenter.y
            };
            double len = std::sqrt(toObstacle.x * toObstacle.x + toObstacle.y * toObstacle.y);
            if (len > 0.0001) {
                normalSum.x -= toObstacle.x / len;
                normalSum.y -= toObstacle.y / len;
            }
            continue;
        }

        // Check for dense solid material (not owned by this organism).
        bool isSolid = cell.material_type == MaterialType::DIRT
            || cell.material_type == MaterialType::SAND
            || cell.material_type == MaterialType::WOOD
            || cell.material_type == MaterialType::METAL
            || cell.material_type == MaterialType::ROOT;

        if (isSolid && cell.fill_ratio > 0.8 && cellOrg != organismId) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);

            // Compute normal from organism center toward blocking cell.
            Vector2d toObstacle{
                static_cast<double>(cellPos.x) - organismCenter.x,
                static_cast<double>(cellPos.y) - organismCenter.y
            };
            double len = std::sqrt(toObstacle.x * toObstacle.x + toObstacle.y * toObstacle.y);
            if (len > 0.0001) {
                normalSum.x -= toObstacle.x / len;
                normalSum.y -= toObstacle.y / len;
            }
            continue;
        }
    }

    // Normalize the accumulated contact normal.
    if (result.blocked) {
        double len = std::sqrt(normalSum.x * normalSum.x + normalSum.y * normalSum.y);
        if (len > 0.0001) {
            result.contactNormal.x = normalSum.x / len;
            result.contactNormal.y = normalSum.y / len;
        }
    }

    return result;
}

void RigidBodyCollisionComponent::respond(
    const CollisionResult& collision,
    Vector2d& velocity,
    double restitution)
{
    if (!collision.blocked) {
        return;
    }

    const Vector2d& normal = collision.contactNormal;

    // Zero normal means we couldn't determine direction - don't modify velocity.
    if (normal.x == 0.0 && normal.y == 0.0) {
        return;
    }

    // Compute velocity component into the surface.
    double vIntoSurface = velocity.x * normal.x + velocity.y * normal.y;

    // Only respond if moving into the surface.
    if (vIntoSurface >= 0.0) {
        return;
    }

    // Remove velocity into surface, optionally add bounce.
    double impulse = -vIntoSurface * (1.0 + restitution);
    velocity.x += impulse * normal.x;
    velocity.y += impulse * normal.y;
}

} // namespace DirtSim
