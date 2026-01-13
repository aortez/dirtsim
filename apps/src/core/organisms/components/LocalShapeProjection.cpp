#include "LocalShapeProjection.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace DirtSim {

void LocalShapeProjection::addCell(Vector2i localPos, Material::EnumType material, double fillRatio)
{
    localShape.push_back(
        LocalCell{ .localPos = localPos, .material = material, .fillRatio = fillRatio });
}

void LocalShapeProjection::clear(World& world)
{
    auto& data = world.getData();

    for (const auto& oldPos : occupiedCells) {
        if (!data.inBounds(oldPos.x, oldPos.y)) {
            continue;
        }

        auto& cell = data.at(oldPos.x, oldPos.y);
        if (world.getOrganismManager().at(oldPos) == lastOwnerId) {
            world.getOrganismManager().removeCellsFromOrganism(lastOwnerId, { oldPos });
            cell.material_type = Material::EnumType::Air;
            cell.fill_ratio = 0.0;
            cell.velocity = { 0.0, 0.0 };
            cell.com = { 0.0, 0.0 };
        }
    }

    occupiedCells.clear();
}

const std::vector<LocalCell>& LocalShapeProjection::getLocalShape() const
{
    return localShape;
}

const std::vector<Vector2i>& LocalShapeProjection::getOccupiedCells() const
{
    return occupiedCells;
}

void LocalShapeProjection::project(
    World& world, OrganismId id, Vector2d position, Vector2d velocity)
{
    auto& data = world.getData();

    // Clear old projection first.
    clear(world);
    lastOwnerId = id;

    // Project each local cell to grid.
    for (const auto& local : localShape) {
        // World position = organism position + local offset.
        const Vector2d worldPos{ position.x + static_cast<double>(local.localPos.x),
                                 position.y + static_cast<double>(local.localPos.y) };

        // Snap to grid.
        const Vector2i gridPos{ static_cast<int>(std::floor(worldPos.x)),
                                static_cast<int>(std::floor(worldPos.y)) };

        // Bounds check.
        if (!data.inBounds(gridPos.x, gridPos.y)) {
            continue;
        }

        auto& cell = data.at(gridPos.x, gridPos.y);

        // Project cell.
        world.getOrganismManager().addCellToOrganism(id, gridPos);
        cell.material_type = local.material;
        cell.fill_ratio = local.fillRatio;
        cell.velocity = velocity;

        // Compute sub-cell COM from fractional position.
        // Map [0,1) within cell to [-1,1] COM space.
        const double fracX = worldPos.x - std::floor(worldPos.x);
        const double fracY = worldPos.y - std::floor(worldPos.y);
        cell.com.x = fracX * 2.0 - 1.0;
        cell.com.y = fracY * 2.0 - 1.0;

        // Clear pending force (caller should have gathered it already).
        cell.pending_force = { 0.0, 0.0 };

        occupiedCells.push_back(gridPos);
    }
}

void LocalShapeProjection::removeCell(Vector2i localPos)
{
    auto it = std::remove_if(localShape.begin(), localShape.end(), [&](const LocalCell& c) {
        return c.localPos == localPos;
    });
    localShape.erase(it, localShape.end());
}

} // namespace DirtSim
