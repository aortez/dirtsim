#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/brains/Genome.h"
#include <memory>

namespace DirtSim {

class GenomeRepository;
class ScenarioRunner;
class Tree;
class World;
struct EvolutionConfig;

/**
 * Incrementally evaluates a single genome by stepping a World one frame at a time.
 *
 * Unlike blocking evaluation, this allows the caller to:
 * - Process events between steps (cancel, pause).
 * - Access the World for rendering.
 * - Track progress during evaluation.
 */
class TrainingRunner {
public:
    enum class State {
        Running,
        TreeDied,
        TimeExpired,
    };

    struct Status {
        State state = State::Running;
        double simTime = 0.0;
        double maxEnergy = 0.0;
        double lifespan = 0.0;
    };

    TrainingRunner(
        const Genome& genome,
        Scenario::EnumType scenarioId,
        const EvolutionConfig& config,
        GenomeRepository& genomeRepository);
    ~TrainingRunner();

    TrainingRunner(const TrainingRunner&) = delete;
    TrainingRunner& operator=(const TrainingRunner&) = delete;

    TrainingRunner(TrainingRunner&&) noexcept;
    TrainingRunner& operator=(TrainingRunner&&) noexcept;

    Status step(int frames = 1);
    Status getStatus() const;

    const World* getWorld() const { return world_.get(); }
    World* getWorld() { return world_.get(); }

    const Tree* getTree() const;

    double getSimTime() const { return simTime_; }
    double getMaxTime() const { return maxTime_; }
    float getProgress() const { return static_cast<float>(simTime_ / maxTime_); }

    double getCurrentMaxEnergy() const { return maxEnergy_; }
    bool isTreeAlive() const;

private:
    std::unique_ptr<World> world_;
    std::unique_ptr<ScenarioRunner> scenario_;
    OrganismId treeId_ = INVALID_ORGANISM_ID;

    double simTime_ = 0.0;
    double maxTime_ = 600.0;
    double maxEnergy_ = 0.0;

    State state_ = State::Running;

    static constexpr double TIMESTEP = 0.016;
};

} // namespace DirtSim
