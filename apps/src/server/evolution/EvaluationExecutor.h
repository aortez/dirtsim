#pragma once

#include "core/RenderMessage.h"
#include "core/ScenarioConfig.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/scenarios/nes/NesControllerTelemetry.h"
#include "server/evolution/FitnessEvaluation.h"
#include "server/evolution/FitnessModelBundle.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DirtSim {

class GenomeRepository;
class NesTileTokenizer;

namespace Server::EvolutionSupport {

enum class EvaluationTaskType : uint8_t {
    GenerationEval = 0,
    RobustnessEval = 1,
};

struct EvaluationIndividual {
    std::string brainKind;
    std::optional<std::string> brainVariant;
    Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
    std::optional<Genome> genome;
};

struct EvaluationSnapshot {
    WorldData worldData;
    std::vector<OrganismId> organismIds;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
};

struct EvaluationTimerAggregate {
    double totalMs = 0.0;
    uint32_t calls = 0;
};

struct EvaluationRequest {
    EvaluationTaskType taskType = EvaluationTaskType::GenerationEval;
    int index = -1;
    int robustGeneration = -1;
    int robustSampleOrdinal = 0;
    EvaluationIndividual individual;
};

struct CompletedEvaluation {
    EvaluationTaskType taskType = EvaluationTaskType::GenerationEval;
    int index = -1;
    int robustGeneration = -1;
    int robustSampleOrdinal = 0;
    FitnessEvaluation fitnessEvaluation;
    double simTime = 0.0;
    int commandsAccepted = 0;
    int commandsRejected = 0;
    std::vector<std::pair<std::string, int>> topCommandSignatures;
    std::vector<std::pair<std::string, int>> topCommandOutcomeSignatures;
    std::optional<EvaluationSnapshot> snapshot;
    std::unordered_map<std::string, EvaluationTimerAggregate> timerStats;
};

struct VisibleRenderFrame {
    WorldData worldData;
    std::vector<OrganismId> organismIds;
    Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
    ScenarioConfig scenarioConfig = Config::Empty{};
    std::optional<NesControllerTelemetry> nesControllerTelemetry = std::nullopt;
    std::optional<ScenarioVideoFrame> scenarioVideoFrame;
};

struct VisibleTickResult {
    std::vector<CompletedEvaluation> completed;
    std::optional<VisibleRenderFrame> frame;
    bool progressed = false;
};

class EvaluationExecutor {
public:
    struct Impl;

    struct Config {
        TrainingSpec trainingSpec;
        TrainingBrainRegistry brainRegistry;
        GenomeRepository* genomeRepository = nullptr;
        FitnessModelBundle fitnessModel;
        std::shared_ptr<NesTileTokenizer> nesTileTokenizer = nullptr;
    };

    explicit EvaluationExecutor(Config config);
    ~EvaluationExecutor();

    EvaluationExecutor(const EvaluationExecutor&) = delete;
    EvaluationExecutor& operator=(const EvaluationExecutor&) = delete;

    bool isPaused() const;
    void pauseSet(bool paused);
    void start(int maxParallelEvaluations);
    void stop();

    void generationBatchSubmit(
        std::span<const EvaluationRequest> requests,
        const EvolutionConfig& evolutionConfig,
        const std::optional<ScenarioConfig>& scenarioConfigOverride);

    void robustnessPassSubmit(
        const EvaluationRequest& request,
        int targetEvalCount,
        const EvolutionConfig& evolutionConfig,
        const std::optional<ScenarioConfig>& scenarioConfigOverride);

    void queuedVisibleExecutionConfigSet(
        const EvolutionConfig& evolutionConfig,
        const std::optional<ScenarioConfig>& scenarioConfigOverride);

    std::optional<std::string> scenarioConfigOverrideSet(
        const std::optional<ScenarioConfig>& scenarioConfigOverride, Scenario::EnumType scenarioId);

    std::vector<CompletedEvaluation> completedDrain();
    std::unordered_map<std::string, EvaluationTimerAggregate> visibleTimerStatsCollect() const;
    VisibleTickResult visibleTick(std::chrono::steady_clock::time_point now, int streamIntervalMs);

    int activeEvaluationsGet() const;
    int allowedConcurrencyGet() const;
    void allowedConcurrencySet(int allowedConcurrency);
    int backgroundWorkerCountGet() const;
    bool hasVisibleEvaluation() const;
    size_t pendingVisibleCountGet() const;
    double visibleSimTimeGet() const;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace Server::EvolutionSupport
} // namespace DirtSim
