#pragma once

#include "Body.h"
#include "TreeBrain.h"
#include "TreeCommands.h"
#include "TreeSensoryData.h"
#include <memory>
#include <optional>

namespace DirtSim {

class ITreeCommandProcessor;
class RigidBodyComponent;

/**
 * Tree organism class.
 *
 * Trees are living organisms composed of physics cells (SEED, WOOD, LEAF, ROOT)
 * that participate fully in simulation while being coordinated by a brain.
 *
 * Trees execute commands over time, consume resources, and make growth decisions
 * through pluggable brain implementations.
 *
 * Uses RigidBodyComponent for physics - the entire tree structure moves as one unit.
 */
class Tree : public Organism::Body {
public:
    Tree(
        OrganismId id,
        std::unique_ptr<TreeBrain> brain,
        std::unique_ptr<ITreeCommandProcessor> processor);
    ~Tree();

    // Organism interface.
    Vector2i getAnchorCell() const override;
    void setAnchorCell(Vector2i pos) override;
    void update(World& world, double deltaTime) override;
    bool usesRigidBodyPhysics() const override { return true; }

    // Tree-specific accessors.
    GrowthStage getStage() const { return stage_; }
    void setStage(GrowthStage stage) { stage_ = stage; }
    double getEnergy() const { return total_energy_; }
    void setEnergy(double energy) { total_energy_ = energy; }
    double getWater() const { return total_water_; }
    void setWater(double water) { total_water_ = water; }

    // Command state.
    const std::optional<TreeCommand>& getCurrentCommand() const { return current_command_; }
    void setCurrentCommand(const std::optional<TreeCommand>& cmd) { current_command_ = cmd; }
    double getTimeRemaining() const { return time_remaining_seconds_; }
    void setTimeRemaining(double time) { time_remaining_seconds_ = time; }

    // Sensory data gathering for brain decisions.
    TreeSensoryData gatherSensoryData(const World& world) const;

    // Replace the brain (for testing with custom brain implementations).
    void setBrain(std::unique_ptr<TreeBrain> brain) { brain_ = std::move(brain); }

    // Command processor (public for testing with recording/mock processors).
    std::unique_ptr<ITreeCommandProcessor> processor;

    // Growth: Add a cell to the tree's local shape.
    // Called by TreeCommandProcessor during growth commands.
    void addCellToLocalShape(
        Vector2i localPos, Material::EnumType material, double fillRatio = 1.0);

private:
    GrowthStage stage_ = GrowthStage::SEED;
    double total_energy_ = 0.0;
    double total_water_ = 0.0;
    std::optional<TreeCommand> current_command_;
    double time_remaining_seconds_ = 0.0;
    double total_command_time_seconds_ = 0.0; // Original duration for progress calculation.
    std::unique_ptr<TreeBrain> brain_;
    std::unique_ptr<RigidBodyComponent> rigidBody_;

    void executeCommand(World& world);
    void processBrainDecision(World& world);
    void updateResources(const World& world, double deltaTime);
};

} // namespace DirtSim
