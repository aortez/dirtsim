/**
 * Multi-cell test organism for validating rigid body components.
 *
 * No brain, no growth - just pure physics with configurable shapes.
 * Used to validate multi-cell behaviors before migrating Tree.
 */
#pragma once

#include "core/organisms/Body.h"
#include <memory>

namespace DirtSim {

class RigidBodyComponent;

enum class MultiCellShape {
    STICK,  // 2 horizontal cells: XX
    LSHAPE, // 3 cells in L: X
            //               XX
    COLUMN, // 3 vertical:   X
            //               X
            //               X
};

class MultiCellTestOrganism : public Organism::Body {
public:
    MultiCellTestOrganism(OrganismId id, MultiCellShape shape);
    ~MultiCellTestOrganism();

    // Organism interface.
    Vector2i getAnchorCell() const override;
    void setAnchorCell(Vector2i pos) override;
    void update(World& world, double deltaTime) override;
    bool usesRigidBodyPhysics() const override { return true; }

    // Test helpers.
    bool isOnGround() const { return on_ground_; }
    void setExternalForce(Vector2d force) { external_force_ = force; }
    MultiCellShape getShape() const { return shape_; }
    std::vector<Vector2i> getGridPositions() const;

private:
    MultiCellShape shape_;
    bool on_ground_ = false;
    Vector2d external_force_{ 0.0, 0.0 };
    std::unique_ptr<RigidBodyComponent> rigidBody_;

    void initializeShape();
};

} // namespace DirtSim
