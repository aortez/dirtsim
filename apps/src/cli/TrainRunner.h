#pragma once

#include "SubprocessManager.h"
#include "core/ScenarioId.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/EvolutionStart.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace DirtSim {
namespace Client {

/**
 * Results from a completed training run.
 */
struct TrainResults {
    Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
    int totalGenerations = 0;
    int populationSize = 0;
    double durationSec = 0.0;

    double bestFitnessAllTime = 0.0;
    double bestFitnessLastGen = 0.0;
    double averageFitnessLastGen = 0.0;

    GenomeId bestGenomeId{};

    bool completed = false;
    std::string errorMessage;
};

/**
 * Runs evolution training on the server and monitors progress.
 *
 * Sends EvolutionStart command, subscribes to progress broadcasts,
 * displays progress updates, and waits for completion.
 */
class TrainRunner {
public:
    TrainRunner();
    ~TrainRunner();

    /**
     * Run training with typed configuration.
     *
     * @param serverPath Path to server binary (for local runs).
     * @param config Typed evolution config (deserialized at CLI boundary).
     * @param remoteAddress Optional remote server address (empty for local).
     * @return TrainResults with final stats and best genome info.
     */
    TrainResults run(
        const std::string& serverPath,
        const Api::EvolutionStart::Command& config,
        const std::string& remoteAddress = "");

    /**
     * Request stop of current training (from signal handler).
     */
    void requestStop();

private:
    SubprocessManager subprocessManager_;
    Network::WebSocketService client_;
    std::atomic<bool> stopRequested_{ false };

    int lastGeneration_ = -1;
    int lastEval_ = -1;
    uint64_t lastRobustEvaluationCount_ = 0;

    void displayProgress(
        int generation,
        int maxGenerations,
        int currentEval,
        int populationSize,
        uint64_t robustEvaluationCount,
        double latestRobustFitness,
        double bestAllTime,
        double avgFitness);
};

} // namespace Client
} // namespace DirtSim
