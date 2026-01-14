#pragma once

#include "ProjectionComponent.h"

namespace DirtSim {

/**
 * Projection component for multi-cell rigid body organisms.
 *
 * Stores the organism's shape in local coordinates and projects it onto the
 * world grid based on continuous position. Computes sub-cell COM from the
 * fractional position for smooth visual motion.
 */
class LocalShapeProjection : public ProjectionComponent {
public:
    void addCell(Vector2i localPos, Material::EnumType material, double fillRatio) override;
    void clear(World& world) override;
    const std::vector<LocalCell>& getLocalShape() const override;
    const std::vector<Vector2i>& getOccupiedCells() const override;
    void project(World& world, OrganismId id, Vector2d position, Vector2d velocity) override;
    void removeCell(Vector2i localPos) override;

    std::vector<LocalCell> localShape;
    std::vector<Vector2i> occupiedCells;

private:
    OrganismId lastOwnerId{ 0 };
};

} // namespace DirtSim
