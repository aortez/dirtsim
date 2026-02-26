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
#include "server/api/FitnessBreakdownReport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
    enum class IndividualOrigin : uint8_t {
        Unknown = 0,
        Seed = 1,
        EliteCarryover = 2,
        OffspringMutated = 3,
        OffspringClone = 4,
    };

    struct Individual {
        std::string brainKind;
        std::optional<std::string> brainVariant;
        Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
        std::optional<Genome> genome;
        bool allowsMutation = false;
        std::optional<double> parentFitness;
    };

    struct EvaluationSnapshot {
        WorldData worldData;
        std::vector<OrganismId> organismIds;
    };

    struct TimerAggregate {
        double totalMs = 0.0;
        uint32_t calls = 0;
    };

    struct MutationOutcomeStats {
        int totalOffspring = 0;
        int mutated = 0;
        int cloneNoGenome = 0;
        int cloneMutationDisabled = 0;
        int cloneNoMutationDelta = 0;

        int cloneCount() const
        {
            return cloneNoGenome + cloneMutationDisabled + cloneNoMutationDelta;
        }
    };

    // Config.
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    TrainingSpec trainingSpec;

    // Population.
    std::vector<Individual> population;
    std::vector<IndividualOrigin> populationOrigins;
    std::vector<double> fitnessScores;
    int generation = 0;
    int currentEval = 0;

    // Tracking.
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    GenomeId bestGenomeId{};
    uint64_t robustEvaluationCount_ = 0;
    IndividualOrigin bestThisGenOrigin_ = IndividualOrigin::Unknown;
    int lastCompletedGeneration_ = -1;
    double lastGenerationFitnessMin_ = 0.0;
    double lastGenerationFitnessMax_ = 0.0;
    std::vector<uint32_t> lastGenerationFitnessHistogram_;
    int saveInterval = 10; // Store best every N generations.
    bool pruneBeforeBreeding_ = false;
    int completedEvaluations_ = 0;

    // RNG.
    std::mt19937 rng;

    struct WorkerResult {
        enum class TaskType : uint8_t {
            GenerationEval = 0,
            RobustnessEval = 1,
        };

        TaskType taskType = TaskType::GenerationEval;
        int index = -1;
        int robustGeneration = -1;
        int robustSampleOrdinal = 0;
        double fitness = 0.0;
        double simTime = 0.0;
        int commandsAccepted = 0;
        int commandsRejected = 0;
        std::vector<std::pair<std::string, int>> topCommandSignatures;
        std::vector<std::pair<std::string, int>> topCommandOutcomeSignatures;
        std::optional<EvaluationSnapshot> snapshot;
        std::unordered_map<std::string, TimerAggregate> timerStats;
        std::optional<Api::FitnessBreakdownReport> fitnessBreakdown;
        std::optional<TreeFitnessBreakdown> treeFitnessBreakdown;
    };

    struct WorkerTask {
        WorkerResult::TaskType taskType = WorkerResult::TaskType::GenerationEval;
        int index = -1;
        int robustGeneration = -1;
        int robustSampleOrdinal = 0;
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
        std::optional<ScenarioConfig> scenarioConfigOverride = std::nullopt;
        TrainingBrainRegistry brainRegistry;
        GenomeRepository* genomeRepository = nullptr;
    };

    std::unique_ptr<TrainingRunner> visibleRunner_;
    int visibleEvalIndex_ = -1;
    bool visibleEvalIsRobustness_ = false;
    std::vector<WorkerResult> visibleDuckPassResults_;
    std::optional<bool> visibleDuckPrimarySpawnLeftFirst_ = std::nullopt;
    int visibleRobustSampleOrdinal_ = 0;
    ScenarioConfig visibleScenarioConfig_ = Config::Empty{};
    Scenario::EnumType visibleScenarioId_ = Scenario::EnumType::TreeGermination;
    std::deque<int> visibleQueue_;
    std::optional<ScenarioConfig> scenarioConfigOverride_ = std::nullopt;

    std::unique_ptr<WorkerState> workerState_;

    // Training timing.
    std::chrono::steady_clock::time_point trainingStartTime_;
    double cumulativeSimTime_ = 0.0; // Total sim time across all completed individuals.
    double sumFitnessThisGen_ = 0.0;
    double finalAverageFitness_ = 0.0;
    double finalTrainingSeconds_ = 0.0;
    bool trainingComplete_ = false;
    int streamIntervalMs_ = 16;
    bool bestPlaybackEnabled_ = false;
    int bestPlaybackIntervalMs_ = 16;
    std::chrono::steady_clock::time_point lastProgressBroadcastTime_{};
    std::chrono::steady_clock::time_point lastStreamBroadcastTime_{};
    std::chrono::steady_clock::time_point lastBestPlaybackBroadcastTime_{};
    UUID trainingSessionId_{};
    std::optional<UnsavedTrainingResult> pendingTrainingResult_;
    std::unordered_map<std::string, TimerAggregate> timerStatsAggregate_;

    TrainingBrainRegistry brainRegistry_;
    std::optional<Individual> bestPlaybackIndividual_;
    std::unique_ptr<TrainingRunner> bestPlaybackRunner_;
    double bestPlaybackFitness_ = 0.0;
    int bestPlaybackGeneration_ = 0;
    bool bestPlaybackDuckSecondPassActive_ = false;
    bool bestPlaybackDuckNextPrimarySpawnLeftFirst_ = true;
    bool bestPlaybackDuckPrimarySpawnLeftFirst_ = true;

    // CPU auto-tuning.
    std::unique_ptr<SystemMetrics> cpuMetrics_;
    std::vector<double> cpuSamples_;
    double lastCpuPercent_ = 0.0;
    std::vector<double> lastCpuPercentPerCore_;
    std::chrono::steady_clock::time_point lastCpuSampleTime_{};

    void onEnter(StateMachine& dsm);
    void onExit(StateMachine& dsm);

    // Called each frame by main loop to advance evolution.
    // Returns a state to transition to, or nullopt to stay in Evolution.
    std::optional<Any> tick(StateMachine& dsm);

    Any onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::ScenarioConfigSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TrainingStreamConfigSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "Evolution"; }

private:
    struct GenerationTelemetry {
        int eliteCarryoverCount = 0;
        int offspringCloneCount = 0;
        int offspringMutatedCount = 0;
        int seedCount = 0;

        int offspringCloneBeatsParentCount = 0;
        int offspringCloneComparedCount = 0;
        double offspringCloneDeltaFitnessSum = 0.0;

        int offspringMutatedBeatsParentCount = 0;
        int offspringMutatedComparedCount = 0;
        double offspringMutatedDeltaFitnessSum = 0.0;

        std::unordered_set<uint64_t> phenotypeAll;
        std::unordered_set<uint64_t> phenotypeEliteCarryover;
        std::unordered_set<uint64_t> phenotypeOffspringMutated;
        std::unordered_set<uint64_t> phenotypeSeed;

        void reset()
        {
            eliteCarryoverCount = 0;
            offspringCloneCount = 0;
            offspringMutatedCount = 0;
            seedCount = 0;

            offspringCloneBeatsParentCount = 0;
            offspringCloneComparedCount = 0;
            offspringCloneDeltaFitnessSum = 0.0;

            offspringMutatedBeatsParentCount = 0;
            offspringMutatedComparedCount = 0;
            offspringMutatedDeltaFitnessSum = 0.0;

            phenotypeAll.clear();
            phenotypeEliteCarryover.clear();
            phenotypeOffspringMutated.clear();
            phenotypeSeed.clear();
        }
    };

    GenerationTelemetry generationTelemetry_;

    int lastGenerationEliteCarryoverCount_ = 0;
    int lastGenerationSeedCount_ = 0;
    int lastGenerationOffspringCloneCount_ = 0;
    int lastGenerationOffspringMutatedCount_ = 0;
    int lastGenerationOffspringCloneBeatsParentCount_ = 0;
    double lastGenerationOffspringCloneAvgDeltaFitness_ = 0.0;
    int lastGenerationOffspringMutatedBeatsParentCount_ = 0;
    double lastGenerationOffspringMutatedAvgDeltaFitness_ = 0.0;
    int lastGenerationPhenotypeUniqueCount_ = 0;
    int lastGenerationPhenotypeUniqueEliteCarryoverCount_ = 0;
    int lastGenerationPhenotypeUniqueOffspringMutatedCount_ = 0;
    int lastGenerationPhenotypeNovelOffspringMutatedCount_ = 0;

    double lastBreedingPerturbationsAvg_ = 0.0;
    double lastBreedingResetsAvg_ = 0.0;
    double lastBreedingWeightChangesAvg_ = 0.0;
    int lastBreedingWeightChangesMin_ = 0;
    int lastBreedingWeightChangesMax_ = 0;

    bool pendingBestRobustness_ = false;
    int pendingBestRobustnessGeneration_ = -1;
    int pendingBestRobustnessIndex_ = -1;
    double pendingBestRobustnessFirstSample_ = 0.0;
    std::optional<EvaluationSnapshot> pendingBestSnapshot_;
    std::optional<Api::FitnessBreakdownReport> pendingBestSnapshotFitnessBreakdown_;
    int pendingBestSnapshotCommandsAccepted_ = 0;
    int pendingBestSnapshotCommandsRejected_ = 0;
    std::vector<std::pair<std::string, int>> pendingBestSnapshotTopCommandSignatures_;
    std::vector<std::pair<std::string, int>> pendingBestSnapshotTopCommandOutcomeSignatures_;
    bool robustnessPassActive_ = false;
    int robustnessPassGeneration_ = -1;
    int robustnessPassIndex_ = -1;
    int robustnessPassTargetEvalCount_ = 0;
    int robustnessPassPendingSamples_ = 0;
    int robustnessPassCompletedSamples_ = 0;
    int robustnessPassVisibleSamplesRemaining_ = 0;
    int robustnessPassNextVisibleSampleOrdinal_ = 1;
    std::vector<double> robustnessPassSamples_;

    void initializePopulation(StateMachine& dsm);
    void startWorkers(StateMachine& dsm);
    void stopWorkers();
    void queueGenerationTasks();
    void drainResults(StateMachine& dsm);
    void startNextVisibleEvaluation(StateMachine& dsm);
    void stepVisibleEvaluation(StateMachine& dsm);
    static WorkerResult runEvaluationTask(const WorkerTask& task, WorkerState& state);
    void captureLastGenerationFitnessDistribution();
    void captureLastGenerationTelemetry();
    void processResult(StateMachine& dsm, WorkerResult result);
    void maybeCompleteGeneration(StateMachine& dsm);
    void startRobustnessPass(StateMachine& dsm);
    void handleRobustnessSampleResult(StateMachine& dsm, const WorkerResult& result);
    void finalizeRobustnessPass(StateMachine& dsm);
    void adjustConcurrency();
    void advanceGeneration(StateMachine& dsm);
    void broadcastProgress(StateMachine& dsm);
    void clearBestPlaybackRunner();
    void setBestPlaybackSource(const Individual& individual, double fitness, int generation);
    void stepBestPlayback(StateMachine& dsm);
    std::optional<Any> broadcastTrainingResult(StateMachine& dsm);
    void storeBestGenome(StateMachine& dsm);
    UnsavedTrainingResult buildUnsavedTrainingResult();
};

} // namespace State
} // namespace Server
} // namespace DirtSim
