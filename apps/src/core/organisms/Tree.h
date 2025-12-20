#pragma once

#include "Organism.h"
#include "TreeBrain.h"
#include "TreeCommands.h"
#include "TreeSensoryData.h"
#include <memory>
#include <optional>

namespace DirtSim {

/**
 * Tree organism class.
 *
 * Trees are living organisms composed of physics cells (SEED, WOOD, LEAF, ROOT)
 * that participate fully in simulation while being coordinated by a brain.
 *
 * Trees execute commands over time, consume resources, and make growth decisions
 * through pluggable brain implementations.
 */
class Tree : public Organism {
public:
    /**
     * Construct a new tree with a given brain implementation.
     *
     * @param id Unique organism identifier.
     * @param brain Brain implementation for decision making.
     */
    Tree(OrganismId id, std::unique_ptr<TreeBrain> brain);

    // Organism interface.
    Vector2i getAnchorCell() const override { return seed_position_; }
    void setAnchorCell(Vector2i pos) override { seed_position_ = pos; }
    void update(World& world, double deltaTime) override;

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

private:
    Vector2i seed_position_;
    GrowthStage stage_ = GrowthStage::SEED;
    double total_energy_ = 0.0;
    double total_water_ = 0.0;
    std::optional<TreeCommand> current_command_;
    double time_remaining_seconds_ = 0.0;
    std::unique_ptr<TreeBrain> brain_;

    void executeCommand(World& world);
    void decideNextAction(const World& world);
    void updateResources(const World& world);
};

} // namespace DirtSim
