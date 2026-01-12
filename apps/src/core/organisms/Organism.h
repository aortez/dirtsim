#pragma once

#include "OrganismType.h"
#include "core/LightManager.h"
#include "core/MaterialType.h"
#include "core/Vector2.h"
#include <unordered_set>
#include <vector>

namespace DirtSim {

class World;

/**
 * A cell in the organism's local coordinate system.
 *
 * Local cells define the organism's shape relative to its anchor position.
 * These are projected onto the grid each frame based on the organism's
 * continuous position.
 */
struct LocalCell {
    Vector2i localPos;
    MaterialType material;
    double fillRatio;
};

/**
 * Result of collision detection for an organism.
 *
 * Used to determine if an organism can move to a predicted position
 * and to compute collision response (impulse, bounce).
 */
struct CollisionInfo {
    bool blocked = false;                // True if any cell is blocked.
    std::vector<Vector2i> blocked_cells; // Grid positions that caused blocking.
    Vector2d contact_normal{ 0.0, 0.0 }; // Average surface normal for bounce direction.
};

/**
 * Hinge configuration for bone connections.
 */
enum class HingeEnd {
    NONE,   // Symmetric spring - both ends free to rotate.
    CELL_A, // cell_a is the pivot point.
    CELL_B  // cell_b is the pivot point.
};

/**
 * Structural connection between two cells in an organism.
 */
struct Bone {
    Vector2i cell_a;
    Vector2i cell_b;
    double rest_distance;
    double stiffness;

    // Hinge/motor properties for rotational control.
    HingeEnd hinge_end = HingeEnd::NONE;
    double rotational_damping = 0.0;
};

/**
 * Get bone stiffness based on connected material types.
 */
double getBoneStiffness(MaterialType a, MaterialType b);

struct LightAttachment {
    LightHandle handle;
    bool follows_facing = true;
};

/**
 * Abstract base class for all organisms.
 *
 * Organisms are living entities that occupy cells in the world.
 * They can be single-cell (duck) or multi-cell (tree).
 *
 * All organisms have:
 * - Unique ID and type
 * - Set of owned cells (with organism_id marking)
 * - Optional bone connections between cells
 * - Facing direction for rendering/AI
 * - Age tracking
 */
class Organism {
public:
    Organism(OrganismId id, OrganismType type);
    virtual ~Organism() = default;

    // Move-only (subclasses may have unique_ptr members).
    Organism(Organism&&) = default;
    Organism& operator=(Organism&&) = default;
    Organism(const Organism&) = delete;
    Organism& operator=(const Organism&) = delete;

    // Identity.
    OrganismId getId() const { return id_; }
    OrganismType getType() const { return type_; }
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

    // Cell body - all organisms have this.
    const std::unordered_set<Vector2i>& getCells() const { return cells_; }
    std::unordered_set<Vector2i>& getCells() { return cells_; }
    const std::vector<Bone>& getBones() const { return bones_; }
    std::vector<Bone>& getBones() { return bones_; }

    // Anchor cell - primary position.
    // For trees: seed position.
    // For ducks: body position.
    virtual Vector2i getAnchorCell() const = 0;
    virtual void setAnchorCell(Vector2i pos) = 0;

    // Facing direction (for rendering/AI).
    Vector2<float> getFacing() const { return facing_; }
    void setFacing(Vector2<float> f) { facing_ = f; }

    // Age tracking.
    double getAge() const { return age_seconds_; }

    void attachLight(LightHandle handle, bool follows_facing = true);
    void detachLight(LightId id);
    const std::vector<LightAttachment>& getAttachedLights() const { return attached_lights_; }

    // Main update - called each tick for behavior/brain logic.
    // For cell-based organisms (Duck), this also handles physics.
    // For rigid body organisms (Goose), physics is handled in advanceTime().
    virtual void update(World& world, double deltaTime) = 0;

    // Returns true if this organism uses rigid body physics.
    // When true, the organism manages its own position/velocity and
    // advanceTime() is called after world forces are applied to cells.
    virtual bool usesRigidBodyPhysics() const { return false; }

    // Called when a cell transfers to a new position (physics movement).
    // Default implementation updates anchor if it moved.
    virtual void onCellTransfer(Vector2i from, Vector2i to);

    // Create bones connecting a new cell to existing organism cells.
    void createBonesForCell(Vector2i new_cell, MaterialType material, const World& world);

    // Rigid body state.
    Vector2d position{ 0.0, 0.0 };
    Vector2d velocity{ 0.0, 0.0 };
    double mass = 0.0;
    Vector2d center_of_mass{ 0.0, 0.0 };
    std::vector<LocalCell> local_shape;
    std::vector<Vector2i> occupied_cells;

    void recomputeMass();
    void recomputeCenterOfMass();
    void integratePosition(double dt);
    void applyForce(Vector2d force, double dt);

    CollisionInfo detectCollisions(
        const std::vector<Vector2i>& target_cells, const World& world) const;

protected:
    OrganismId id_;
    OrganismType type_;
    bool active_ = true;

    std::unordered_set<Vector2i> cells_;
    std::vector<Bone> bones_;
    Vector2<float> facing_{ 1.0f, 0.0f };
    double age_seconds_ = 0.0;
    std::vector<LightAttachment> attached_lights_;

    void updateAttachedLights(World& world, double deltaTime);
};

} // namespace DirtSim
