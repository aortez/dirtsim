#pragma once

#include "Bone.h"
#include "LocalCell.h"
#include "OrganismType.h"
#include "core/LightManager.h"
#include "core/Vector2.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace DirtSim {

class World;

/**
 * Light attached to an organism.
 */
struct LightAttachment {
    LightHandle handle;
    bool follows_facing = true;
};

/**
 * Result of collision detection for an organism.
 */
struct CollisionInfo {
    bool blocked = false;                // True if any cell is blocked.
    std::vector<Vector2i> blocked_cells; // Grid positions that caused blocking.
    Vector2d contact_normal{ 0.0, 0.0 }; // Average surface normal for bounce direction.
};

namespace Organism {

/**
 * Abstract base class for all organisms.
 *
 * Organisms are living entities that occupy cells in the world.
 * They can be single-cell (duck) or multi-cell (tree).
 */
class Body {
public:
    Body(OrganismId id, OrganismType type);
    virtual ~Body() = default;

    // Move-only.
    Body(Body&&) = default;
    Body& operator=(Body&&) = default;
    Body(const Body&) = delete;
    Body& operator=(const Body&) = delete;

    // Identity.
    OrganismId getId() const { return id_; }
    OrganismType getType() const { return type_; }
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

    // Cell body.
    const std::unordered_set<Vector2i>& getCells() const { return cells_; }
    std::unordered_set<Vector2i>& getCells() { return cells_; }
    const std::vector<Bone>& getBones() const { return bones_; }
    std::vector<Bone>& getBones() { return bones_; }

    // Anchor cell - primary position.
    virtual Vector2i getAnchorCell() const = 0;
    virtual void setAnchorCell(Vector2i pos) = 0;

    // Facing direction (for rendering/AI).
    Vector2<float> getFacing() const { return facing_; }
    void setFacing(Vector2<float> f) { facing_ = f; }

    // Age tracking.
    double getAge() const { return age_seconds_; }

    std::vector<std::pair<std::string, int>> getTopCommandSignatures(size_t maxEntries) const;
    std::vector<std::pair<std::string, int>> getTopCommandOutcomeSignatures(
        size_t maxEntries) const;

    void attachLight(LightHandle handle, bool follows_facing = true);
    void detachLight(LightId id);
    const std::vector<LightAttachment>& getAttachedLights() const { return attached_lights_; }

    // Main update - called each tick for behavior/brain logic.
    virtual void update(World& world, double deltaTime) = 0;

    // Returns true if this organism uses rigid body physics.
    virtual bool usesRigidBodyPhysics() const { return false; }

    // Called when a cell transfers to a new position (physics movement).
    virtual void onCellTransfer(Vector2i from, Vector2i to);

    // Create bones connecting a new cell to existing organism cells.
    void createBonesForCell(Vector2i new_cell, Material::EnumType material, const World& world);

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

    void recordCommandSignature(std::string signature);
    void recordCommandOutcomeSignature(std::string signature);

    void updateAttachedLights(World& world, double deltaTime);

private:
    std::unordered_map<std::string, int> commandSignatureCounts_;
    std::unordered_map<std::string, int> commandOutcomeSignatureCounts_;
};

} // namespace Organism

} // namespace DirtSim
