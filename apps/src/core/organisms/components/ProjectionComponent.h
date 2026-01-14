#pragma once

#include "core/MaterialType.h"
#include "core/Vector2d.h"
#include "core/Vector2i.h"
#include "core/organisms/Body.h"
#include "core/organisms/OrganismType.h"
#include <vector>

namespace DirtSim {

class World;

/**
 * Interface for how an organism appears on the grid.
 *
 * Projection components manage the organism's local shape and project it
 * onto the world grid based on the organism's continuous position.
 */
class ProjectionComponent {
public:
    virtual ~ProjectionComponent() = default;

    virtual void addCell(Vector2i localPos, Material::EnumType material, double fillRatio) = 0;
    virtual void clear(World& world) = 0;
    virtual const std::vector<LocalCell>& getLocalShape() const = 0;
    virtual const std::vector<Vector2i>& getOccupiedCells() const = 0;
    virtual void project(World& world, OrganismId id, Vector2d position, Vector2d velocity) = 0;
    virtual void removeCell(Vector2i localPos) = 0;
};

} // namespace DirtSim
