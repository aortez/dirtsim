#pragma once

#include "StateForward.h"
#include "UnsavedTrainingResult.h"
#include "core/ScenarioConfig.h"
#include "core/Vector2.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/Event.h"

#include <chrono>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace DirtSim {
class World;
class ScenarioRunner;
} // namespace DirtSim

namespace DirtSim {
namespace Server {
namespace State {

/**
 * Evolution state — runs genetic algorithm to evolve organism brains.
 *
 * Each tick() advances one physics step of the current evaluation, allowing
 * the event loop to process commands between steps. This ensures responsive
 * handling of EvolutionStop and other commands during long evaluations.
 */
struct Evolution {
    struct Individual {
        std::string brainKind;
        std::optional<std::string> brainVariant;
        std::optional<Genome> genome;
        bool allowsMutation = false;
    };

    // Config.
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    TrainingSpec trainingSpec;

    // Population.
    std::vector<Individual> population;
    std::vector<double> fitnessScores;
    int generation = 0;
    int currentEval = 0;

    // Tracking.
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    GenomeId bestGenomeId{};
    int saveInterval = 10; // Store best every N generations.

    // RNG.
    std::mt19937 rng;

    // Current evaluation state (for non-blocking tick).
    std::unique_ptr<World> evalWorld_;
    std::unique_ptr<ScenarioRunner> evalScenario_;
    OrganismId evalOrganismId_{};
    Vector2d evalSpawnPosition_{ 0.0, 0.0 };
    Vector2d evalLastPosition_{ 0.0, 0.0 };
    double evalSimTime_ = 0.0;
    double evalMaxEnergy_ = 0.0;
    ScenarioConfig evalScenarioConfig_ = Config::Empty{};

    // Training timing.
    std::chrono::steady_clock::time_point trainingStartTime_;
    double cumulativeSimTime_ = 0.0; // Total sim time across all completed individuals.
    double finalAverageFitness_ = 0.0;
    double finalTrainingSeconds_ = 0.0;
    bool trainingComplete_ = false;
    UUID trainingSessionId_{};
    bool trainingResultAvailableSent_ = false;
    std::optional<UnsavedTrainingResult> pendingTrainingResult_;

    TrainingBrainRegistry brainRegistry_;

    void onEnter(StateMachine& dsm);
    void onExit(StateMachine& dsm);

    // Called each frame by main loop to advance evolution.
    // Returns a state to transition to, or nullopt to stay in Evolution.
    std::optional<Any> tick(StateMachine& dsm);

    Any onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingResultAvailableAck::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "Evolution"; }

private:
    void initializePopulation(StateMachine& dsm);
    void startEvaluation(StateMachine& dsm);
    void finishEvaluation(StateMachine& dsm);
    void advanceGeneration(StateMachine& dsm);
    void broadcastProgress(StateMachine& dsm);
    std::optional<Any> broadcastTrainingResultAvailable(StateMachine& dsm);
    void storeBestGenome(StateMachine& dsm);
    UnsavedTrainingResult buildUnsavedTrainingResult();
};

} // namespace State
} // namespace Server
} // namespace DirtSim
