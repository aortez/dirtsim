#pragma once

#include "StateForward.h"
#include "UnsavedTrainingResult.h"
#include "core/GenomePoolId.h"
#include "core/ScenarioConfig.h"
#include "core/SystemMetrics.h"
#include "core/Vector2.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/TreeResourceTotals.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/AdaptiveMutation.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingPhaseTracker.h"
#include "core/organisms/evolution/TrainingRunner.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/Event.h"
#include "server/api/EvolutionMutationControlsSet.h"
#include "server/api/EvolutionPauseSet.h"
#include "server/evolution/EvaluationExecutor.h"
#include "server/evolution/FitnessEvaluation.h"
#include "server/evolution/FitnessModelBundle.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace DirtSim {
class GenomeRepository;
class NesTileTokenizer;
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
    GenomePoolId genomePoolId_ = GenomePoolId::DirtSim;

    // Population.
    std::vector<Individual> population;
    std::vector<IndividualOrigin> populationOrigins;
    std::vector<double> fitnessScores;
    std::vector<double> wingUpSecondsPerIndividual;
    int generation = 0;
    int currentEval = 0;

    // Tracking.
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    GenomeId bestGenomeId{};
    uint64_t robustEvaluationCount_ = 0;
    IndividualOrigin bestThisGenOrigin_ = IndividualOrigin::Unknown;
    AdaptiveMutationControlMode mutationControlMode_ = AdaptiveMutationControlMode::Auto;
    EffectiveAdaptiveMutation lastEffectiveAdaptiveMutation_{};
    TrainingPhaseTracker trainingPhaseTracker_;
    int lastCompletedGeneration_ = -1;
    double lastGenerationAverageFitness_ = 0.0;
    double lastGenerationFitnessMin_ = 0.0;
    double lastGenerationFitnessMax_ = 0.0;
    std::vector<uint32_t> lastGenerationFitnessHistogram_;
    int saveInterval = 10; // Store best every N generations.
    bool pruneBeforeBreeding_ = false;
    int completedEvaluations_ = 0;

    // RNG.
    std::mt19937 rng;

    std::optional<ScenarioConfig> scenarioConfigOverride_ = std::nullopt;
    std::unique_ptr<EvolutionSupport::EvaluationExecutor> executor_;
    std::shared_ptr<NesTileTokenizer> nesTileTokenizer_ = nullptr;
    std::optional<NesTileBrainCompatibilityMetadata> nesTileBrainCompatibility_ = std::nullopt;

    // Training timing.
    std::chrono::steady_clock::time_point trainingStartTime_;
    std::chrono::steady_clock::time_point trainingPauseStartTime_{};
    double cumulativeSimTime_ = 0.0; // Total sim time across all completed individuals.
    double sumFitnessThisGen_ = 0.0;
    double finalAverageFitness_ = 0.0;
    double finalTrainingSeconds_ = 0.0;
    std::chrono::steady_clock::duration totalPausedDuration_{};
    bool trainingComplete_ = false;
    bool trainingPaused_ = false;
    std::chrono::steady_clock::time_point lastProgressBroadcastTime_{};
    std::chrono::steady_clock::time_point lastBestPlaybackBroadcastTime_{};
    std::chrono::steady_clock::time_point lastBestPlaybackStepTime_{};
    UUID trainingSessionId_{};
    std::optional<UnsavedTrainingResult> pendingTrainingResult_;
    std::unordered_map<std::string, EvolutionSupport::EvaluationTimerAggregate>
        timerStatsAggregate_;

    TrainingBrainRegistry brainRegistry_;
    EvolutionSupport::FitnessModelBundle fitnessModel_;

    struct BestPlaybackState {
        std::optional<Individual> individual;
        std::shared_ptr<NesTileTokenizer> nesTileTokenizer = nullptr;
        std::unique_ptr<TrainingRunner> runner;
        double fitness = 0.0;
        int generation = 0;
        bool duckSecondPassActive = false;
        bool duckNextPrimarySpawnLeftFirst = true;
        bool duckPrimarySpawnLeftFirst = true;

        void reset();
        void clearRunner();
    };

    BestPlaybackState bestPlayback_;

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

    Any onEvent(const Api::EvolutionMutationControlsSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::EvolutionPauseSet::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& dsm);
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

    struct LastGenerationTelemetry {
        int eliteCarryoverCount = 0;
        int seedCount = 0;
        int offspringCloneCount = 0;
        int offspringMutatedCount = 0;
        int offspringCloneBeatsParentCount = 0;
        double offspringCloneAvgDeltaFitness = 0.0;
        int offspringMutatedBeatsParentCount = 0;
        double offspringMutatedAvgDeltaFitness = 0.0;
        int phenotypeUniqueCount = 0;
        int phenotypeUniqueEliteCarryoverCount = 0;
        int phenotypeUniqueOffspringMutatedCount = 0;
        int phenotypeNovelOffspringMutatedCount = 0;
        bool breedingUsesBudget = false;
        AdaptiveMutationMode breedingMutationMode = AdaptiveMutationMode::Baseline;
        int breedingResolvedPerturbationsPerOffspring = 0;
        int breedingResolvedResetsPerOffspring = 0;
        double breedingResolvedSigma = 0.0;
        double breedingPerturbationsAvg = 0.0;
        double breedingResetsAvg = 0.0;
        double breedingWeightChangesAvg = 0.0;
        int breedingWeightChangesMin = 0;
        int breedingWeightChangesMax = 0;
    };

    LastGenerationTelemetry lastGenTelemetry_;

    struct PendingBestState {
        bool robustness = false;
        int robustnessGeneration = -1;
        int robustnessIndex = -1;
        double robustnessFirstSample = 0.0;
        std::optional<EvolutionSupport::EvaluationSnapshot> snapshot;
        std::optional<EvolutionSupport::FitnessEvaluation> snapshotFitnessEvaluation;
        int snapshotCommandsAccepted = 0;
        int snapshotCommandsRejected = 0;
        std::vector<std::pair<std::string, int>> snapshotTopCommandSignatures;
        std::vector<std::pair<std::string, int>> snapshotTopCommandOutcomeSignatures;

        void reset();
        void resetTrigger();
    };

    PendingBestState pendingBest_;

    struct RobustnessPassState {
        bool active = false;
        int generation = -1;
        int index = -1;
        int targetEvalCount = 0;
        int pendingSamples = 0;
        int completedSamples = 0;
        std::vector<double> samples;

        void reset();
    };

    RobustnessPassState robustnessPass_;

    void initializePopulation(StateMachine& dsm);
    void queueGenerationTasks();
    void drainResults(StateMachine& dsm);
    void captureLastGenerationFitnessDistribution();
    void captureLastGenerationTelemetry();
    void updateTrainingPhaseTelemetry();
    void processResult(StateMachine& dsm, EvolutionSupport::CompletedEvaluation result);
    void maybeCompleteGeneration(StateMachine& dsm);
    void finalizePendingBestWithoutRobustnessPass(StateMachine& dsm);
    void startRobustnessPass(StateMachine& dsm);
    void handleRobustnessSampleResult(
        StateMachine& dsm, const EvolutionSupport::CompletedEvaluation& result);
    void finalizeRobustnessPass(StateMachine& dsm);
    double activeTrainingSecondsGet(std::chrono::steady_clock::time_point now) const;
    void adjustConcurrency();
    void advanceGeneration(StateMachine& dsm);
    void pauseSet(bool paused, StateMachine& dsm);
    void broadcastProgress(StateMachine& dsm);
    void setBestPlaybackSource(const Individual& individual, double fitness, int generation);
    void stepBestPlayback(StateMachine& dsm);
    std::optional<Any> broadcastTrainingResult(StateMachine& dsm);
    void storeBestGenome(StateMachine& dsm);
    UnsavedTrainingResult buildUnsavedTrainingResult();
};

} // namespace State
} // namespace Server
} // namespace DirtSim
