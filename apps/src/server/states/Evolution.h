#pragma once

#include "StateForward.h"
#include "UnsavedTrainingResult.h"
#include "core/ScenarioConfig.h"
#include "core/SystemMetrics.h"
#include "core/Vector2.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/TreeResourceTotals.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingRunner.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/organisms/evolution/TreeEvaluator.h"
#include "server/Event.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DirtSim {
class GenomeRepository;
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
        Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
        std::optional<Genome> genome;
        bool allowsMutation = false;
    };

    struct EvaluationSnapshot {
        WorldData worldData;
        std::vector<OrganismId> organismIds;
    };

    struct TimerAggregate {
        double totalMs = 0.0;
        uint32_t calls = 0;
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

    struct WorkerResult {
        int index = -1;
        double fitness = 0.0;
        double simTime = 0.0;
        int commandsAccepted = 0;
        int commandsRejected = 0;
        std::vector<std::pair<std::string, int>> topCommandSignatures;
        std::vector<std::pair<std::string, int>> topCommandOutcomeSignatures;
        std::optional<EvaluationSnapshot> snapshot;
        std::unordered_map<std::string, TimerAggregate> timerStats;
        std::optional<TreeFitnessBreakdown> treeFitnessBreakdown;
    };

    struct WorkerTask {
        int index = -1;
        Individual individual;
    };

    struct WorkerState {
        int backgroundWorkerCount = 0;
        std::vector<std::thread> workers;
        std::deque<WorkerTask> taskQueue;
        std::mutex taskMutex;
        std::condition_variable taskCv;
        std::deque<WorkerResult> resultQueue;
        std::mutex resultMutex;
        std::atomic<bool> stopRequested{ false };
        std::atomic<int> allowedConcurrency{ 0 }; // Max concurrent background evaluations.
        std::atomic<int> activeEvaluations{ 0 };  // Currently running evaluations.
        TrainingSpec trainingSpec;
        EvolutionConfig evolutionConfig;
        TrainingBrainRegistry brainRegistry;
        GenomeRepository* genomeRepository = nullptr;
    };

    std::unique_ptr<TrainingRunner> visibleRunner_;
    int visibleEvalIndex_ = -1;
    ScenarioConfig visibleScenarioConfig_ = Config::Empty{};
    Scenario::EnumType visibleScenarioId_ = Scenario::EnumType::TreeGermination;
    std::deque<int> visibleQueue_;

    std::unique_ptr<WorkerState> workerState_;

    // Training timing.
    std::chrono::steady_clock::time_point trainingStartTime_;
    double cumulativeSimTime_ = 0.0; // Total sim time across all completed individuals.
    double sumFitnessThisGen_ = 0.0;
    double finalAverageFitness_ = 0.0;
    double finalTrainingSeconds_ = 0.0;
    bool trainingComplete_ = false;
    int streamIntervalMs_ = 16;
    std::chrono::steady_clock::time_point lastProgressBroadcastTime_{};
    std::chrono::steady_clock::time_point lastStreamBroadcastTime_{};
    UUID trainingSessionId_{};
    std::optional<UnsavedTrainingResult> pendingTrainingResult_;
    std::unordered_map<std::string, TimerAggregate> timerStatsAggregate_;

    TrainingBrainRegistry brainRegistry_;

    // CPU auto-tuning.
    std::unique_ptr<SystemMetrics> cpuMetrics_;
    std::vector<double> cpuSamples_;
    std::chrono::steady_clock::time_point lastCpuSampleTime_{};

    void onEnter(StateMachine& dsm);
    void onExit(StateMachine& dsm);

    // Called each frame by main loop to advance evolution.
    // Returns a state to transition to, or nullopt to stay in Evolution.
    std::optional<Any> tick(StateMachine& dsm);

    Any onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingStreamConfigSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "Evolution"; }

private:
    void initializePopulation(StateMachine& dsm);
    void startWorkers(StateMachine& dsm);
    void stopWorkers();
    void queueGenerationTasks();
    void drainResults(StateMachine& dsm);
    void startNextVisibleEvaluation(StateMachine& dsm);
    void stepVisibleEvaluation(StateMachine& dsm);
    static WorkerResult runEvaluationTask(const WorkerTask& task, WorkerState& state);
    void processResult(StateMachine& dsm, WorkerResult result);
    static std::optional<EvaluationSnapshot> buildEvaluationSnapshot(const TrainingRunner& runner);
    void maybeCompleteGeneration(StateMachine& dsm);
    void adjustConcurrency();
    void advanceGeneration(StateMachine& dsm);
    void broadcastProgress(StateMachine& dsm);
    std::optional<Any> broadcastTrainingResult(StateMachine& dsm);
    void storeBestGenome(StateMachine& dsm);
    UnsavedTrainingResult buildUnsavedTrainingResult();
};

} // namespace State
} // namespace Server
} // namespace DirtSim
