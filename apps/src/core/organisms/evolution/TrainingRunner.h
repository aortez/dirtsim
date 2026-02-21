#pragma once

#include "core/ScenarioConfig.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/OrganismTracker.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/organisms/evolution/TreeEvaluator.h"
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace DirtSim {

class GenomeRepository;
namespace Organism {
class Body;
}
class ScenarioRunner;
class World;

/**
 * Incrementally evaluates a single organism by stepping a World one frame at a time.
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
        OrganismDied,
        TimeExpired,
    };

    struct Status {
        State state = State::Running;
        double simTime = 0.0;
        double maxEnergy = 0.0;
        double lifespan = 0.0;
        int commandsAccepted = 0;
        int commandsRejected = 0;
        int idleCancels = 0;
    };

    struct BrainSpec {
        std::string brainKind;
        std::optional<std::string> brainVariant;
    };

    struct Individual {
        BrainSpec brain;
        Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
        std::optional<Genome> genome;
    };

    struct Config {
        TrainingBrainRegistry brainRegistry;
        std::optional<bool> duckClockSpawnLeftFirst = std::nullopt;
        std::optional<uint32_t> duckClockSpawnRngSeed = std::nullopt;
    };

    TrainingRunner(
        const TrainingSpec& trainingSpec,
        const Individual& individual,
        const EvolutionConfig& evolutionConfig,
        GenomeRepository& genomeRepository);

    TrainingRunner(
        const TrainingSpec& trainingSpec,
        const Individual& individual,
        const EvolutionConfig& evolutionConfig,
        GenomeRepository& genomeRepository,
        const Config& runnerConfig);
    ~TrainingRunner();

    TrainingRunner(const TrainingRunner&) = delete;
    TrainingRunner& operator=(const TrainingRunner&) = delete;

    TrainingRunner(TrainingRunner&&) noexcept;
    TrainingRunner& operator=(TrainingRunner&&) noexcept;

    Status step(int frames = 1);
    Status getStatus() const;

    const World* getWorld() const { return world_.get(); }
    World* getWorld() { return world_.get(); }
    ScenarioConfig getScenarioConfig() const;

    const Organism::Body* getOrganism() const;
    const OrganismTrackingHistory& getOrganismTrackingHistory() const;
    const std::optional<TreeResourceTotals>& getTreeResourceTotals() const;
    std::vector<std::pair<std::string, int>> getTopCommandSignatures(size_t maxEntries) const;
    std::vector<std::pair<std::string, int>> getTopCommandOutcomeSignatures(
        size_t maxEntries) const;

    double getSimTime() const { return simTime_; }
    double getMaxTime() const { return maxTime_; }
    float getProgress() const { return static_cast<float>(simTime_ / maxTime_); }

    double getCurrentMaxEnergy() const;
    bool isOrganismAlive() const;

private:
    void spawnEvaluationOrganism();
    static Config makeDefaultConfig();

    TrainingSpec trainingSpec_;
    Individual individual_;
    std::unique_ptr<World> world_;
    std::unique_ptr<ScenarioRunner> scenario_;
    OrganismId organismId_ = INVALID_ORGANISM_ID;

    double simTime_ = 0.0;
    double maxTime_ = 600.0;
    OrganismTracker organismTracker_;
    TreeEvaluator treeEvaluator_;

    State state_ = State::Running;
    TrainingBrainRegistry brainRegistry_;
    std::optional<bool> duckClockSpawnLeftFirst_ = std::nullopt;
    std::mt19937 spawnRng_;
    EvolutionConfig evolutionConfig_;

    static constexpr double TIMESTEP = 0.016;
};

} // namespace DirtSim
