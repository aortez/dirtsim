#pragma once

#include "DuckBrain.h"
#include "GooseBrain.h"
#include "Organism.h"
#include "OrganismType.h"
#include "TreeBrain.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace DirtSim {

// Forward declarations.
class Duck;
class Goose;
class Tree;
class World;

/**
 * Transfer notification from physics system.
 * Records when organism cells move due to physics.
 */
struct OrganismTransfer {
    Vector2i from_pos;
    Vector2i to_pos;
    OrganismId organism_id;
    double amount;
};

/**
 * Manages all organisms in the world.
 *
 * Responsibilities:
 * - Create/destroy organisms (trees, ducks, etc.)
 * - Track cell-to-organism mapping
 * - Update all organisms each tick
 * - Apply bone forces for structural integrity
 * - Handle cell transfers from physics system
 */
class OrganismManager {
public:
    OrganismManager() = default;

    // Main update - calls update() on all organisms for behavior/brain logic.
    // For rigid body organisms, this only handles behavior; physics is in advanceTime().
    void update(World& world, double deltaTime);

    // Physics update for rigid body organisms.
    // Called after world forces are applied to cells, so organisms can gather
    // accumulated forces (gravity, air resistance, etc.) and integrate.
    void advanceTime(World& world, double deltaTime);

    // Clear all organisms.
    void clear();

    // Factory methods for creating organisms.
    OrganismId createTree(
        World& world,
        uint32_t x,
        uint32_t y,
        std::unique_ptr<TreeBrain> brain = nullptr);

    OrganismId createDuck(
        World& world,
        uint32_t x,
        uint32_t y,
        std::unique_ptr<DuckBrain> brain = nullptr);

    OrganismId createGoose(
        World& world,
        uint32_t x,
        uint32_t y,
        std::unique_ptr<GooseBrain> brain = nullptr);

    // Remove an organism and clean up its cells from the world.
    void removeOrganismFromWorld(World& world, OrganismId id);

    // Generic organism access.
    Organism* getOrganism(OrganismId id);
    const Organism* getOrganism(OrganismId id) const;

    // Type-specific access (returns nullptr if wrong type).
    Tree* getTree(OrganismId id);
    const Tree* getTree(OrganismId id) const;
    Duck* getDuck(OrganismId id);
    const Duck* getDuck(OrganismId id) const;
    Goose* getGoose(OrganismId id);
    const Goose* getGoose(OrganismId id) const;

    // Cell-to-organism lookup.
    OrganismId getOrganismAtCell(const Vector2i& pos) const;

    // Iteration over all organisms.
    template <typename Func>
    void forEachOrganism(Func&& func)
    {
        for (auto& [id, organism] : organisms_) {
            func(*organism);
        }
    }

    template <typename Func>
    void forEachOrganism(Func&& func) const
    {
        for (const auto& [id, organism] : organisms_) {
            func(*organism);
        }
    }

    // Cell tracking helpers.
    void addCellToOrganism(World& world, OrganismId id, Vector2i pos);
    void removeCellsFromOrganism(OrganismId id, const std::vector<Vector2i>& positions);

    // Physics integration.
    void notifyTransfers(const std::vector<OrganismTransfer>& transfers);
    void applyBoneForces(World& world, double deltaTime);

    // Statistics.
    size_t getOrganismCount() const { return organisms_.size(); }

private:
    // Internal removal - only clears tracking, not world cells.
    void removeOrganism(OrganismId id);

    std::unordered_map<OrganismId, std::unique_ptr<Organism>> organisms_;
    std::unordered_map<Vector2i, OrganismId> cell_to_organism_;
    OrganismId next_id_ = 1;
};

} // namespace DirtSim
