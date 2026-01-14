#pragma once

#include "Body.h"
#include "DuckBrain.h"
#include "GooseBrain.h"
#include "OrganismType.h"
#include "TreeBrain.h"
#include "TreeCommandProcessor.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace DirtSim {

// Forward declarations.
class Duck;
class Goose;
class MultiCellTestOrganism;
class Tree;
class World;
enum class MultiCellShape;

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

    OrganismId at(Vector2i pos) const;
    bool hasOrganism(Vector2i pos) const;
    const std::vector<OrganismId>& getGrid() const;
    void resizeGrid(int16_t width, int16_t height);

    void update(World& world, double deltaTime);

    // Physics update for rigid body organisms.
    // Called after world forces are applied to cells, so organisms can gather
    // accumulated forces (gravity, air resistance, etc.) and integrate.
    void advanceTime(World& world, double deltaTime);

    // Clear all organisms.
    void clear();

    // Factory methods for creating organisms.
    OrganismId createTree(
        World& world, uint32_t x, uint32_t y, std::unique_ptr<TreeBrain> brain = nullptr);

    OrganismId createDuck(
        World& world, uint32_t x, uint32_t y, std::unique_ptr<DuckBrain> brain = nullptr);

    OrganismId createGoose(
        World& world, uint32_t x, uint32_t y, std::unique_ptr<GooseBrain> brain = nullptr);

    OrganismId createMultiCellTestOrganism(
        World& world, uint32_t x, uint32_t y, MultiCellShape shape);

    // Remove an organism and clean up its cells from the world.
    void removeOrganismFromWorld(World& world, OrganismId id);

    // Generic organism access.
    Organism::Body* getOrganism(OrganismId id);
    const Organism::Body* getOrganism(OrganismId id) const;

    // Type-specific access (returns nullptr if wrong type).
    Tree* getTree(OrganismId id);
    const Tree* getTree(OrganismId id) const;
    Duck* getDuck(OrganismId id);
    const Duck* getDuck(OrganismId id) const;
    Goose* getGoose(OrganismId id);
    const Goose* getGoose(OrganismId id) const;
    MultiCellTestOrganism* getMultiCellTestOrganism(OrganismId id);
    const MultiCellTestOrganism* getMultiCellTestOrganism(OrganismId id) const;

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

    void addCellToOrganism(OrganismId id, Vector2i pos);
    void removeCellsFromOrganism(OrganismId id, const std::vector<Vector2i>& positions);
    void swapOrganisms(Vector2i pos1, Vector2i pos2);
    void moveOrganismCell(Vector2i from, Vector2i to, OrganismId organism_id);

    // Physics integration.
    void applyBoneForces(World& world, double deltaTime);

    // Sync organism render data to WorldData.entities.
    // Called automatically by World::advanceTime() - scenarios don't need to manage this.
    void syncEntitiesToWorldData(World& world);

    // Inject organism emissions into the light calculator's emissive overlay.
    // Called before light calculation so glowing organisms illuminate their surroundings.
    void injectEmissions(class WorldLightCalculator& light_calc);

    // Statistics.
    size_t getOrganismCount() const { return organisms_.size(); }

private:
    void removeOrganism(OrganismId id);
    void setOrganismAt(Vector2i pos, OrganismId id);
    void clearOrganismAt(Vector2i pos);

    std::unordered_map<OrganismId, std::unique_ptr<Organism::Body>> organisms_;
    OrganismId next_id_{ 1 };

    std::vector<OrganismId> grid_;
    int16_t width_ = 0;
    int16_t height_ = 0;
};

} // namespace DirtSim
