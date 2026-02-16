#include "TrainRunner.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h" // For deserialize_payload.
#include "core/network/ClientHello.h"
#include "server/api/EventSubscribe.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStop.h"
#include "server/api/Exit.h"
#include "server/api/TrainingResult.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace DirtSim {
namespace Client {

TrainRunner::TrainRunner()
{}

TrainRunner::~TrainRunner()
{}

TrainResults TrainRunner::run(
    const std::string& serverPath,
    const Api::EvolutionStart::Command& config,
    const std::string& remoteAddress)
{
    TrainResults results;
    results.scenarioId = config.scenarioId;
    if (!config.population.empty()) {
        int total = 0;
        for (const auto& spec : config.population) {
            total += spec.count;
        }
        results.populationSize = total;
    }
    else {
        results.populationSize = config.evolution.populationSize;
    }
    results.totalGenerations = config.evolution.maxGenerations;

    // Determine connection address.
    const std::string connectAddress =
        remoteAddress.empty() ? "ws://localhost:8080" : remoteAddress;
    const bool isRemote = !remoteAddress.empty();

    // Launch local server if no remote address specified.
    if (!isRemote) {
        if (!subprocessManager_.launchServer(
                serverPath, "--log-config benchmark-logging-config.json")) {
            results.errorMessage = "Failed to launch server";
            SLOG_ERROR("{}", results.errorMessage);
            return results;
        }

        if (!subprocessManager_.waitForServerReady(connectAddress, 10)) {
            results.errorMessage = "Server failed to start";
            SLOG_ERROR("{}", results.errorMessage);
            return results;
        }
    }
    else {
        SLOG_INFO("Using remote server at {}", connectAddress);
    }

    // Connect with binary protocol for broadcasts.
    client_.setProtocol(Network::Protocol::BINARY);
    Network::ClientHello hello{
        .protocolVersion = Network::kClientHelloProtocolVersion,
        .wantsRender = false,
        .wantsEvents = true,
    };
    client_.setClientHello(hello);
    client_.registerHandler<Api::TrainingResult::Cwc>([](Api::TrainingResult::Cwc cwc) {
        SLOG_INFO("TrainingResult received (candidates={})", cwc.command.candidates.size());
        cwc.sendResponse(Api::TrainingResult::Response::okay(std::monostate{}));
    });

    auto connectResult = client_.connect(connectAddress);
    if (connectResult.isError()) {
        results.errorMessage = "Failed to connect: " + connectResult.errorValue();
        SLOG_ERROR("{}", results.errorMessage);
        return results;
    }

    // Track progress from broadcasts.
    Api::EvolutionProgress latestProgress;
    std::atomic<bool> progressUpdated{ false };

    // EvolutionProgress broadcasts have id=0, routed to serverCommandCallback.
    client_.onServerCommand([&latestProgress, &progressUpdated](
                                const std::string& type, const std::vector<std::byte>& payload) {
        if (type == "EvolutionProgress") {
            try {
                latestProgress = Network::deserialize_payload<Api::EvolutionProgress>(payload);
                progressUpdated = true;
            }
            catch (const std::exception& e) {
                SLOG_WARN("Error parsing EvolutionProgress: {}", e.what());
            }
        }
    });

    // Subscribe to event stream (EvolutionProgress).
    Api::EventSubscribe::Command eventCmd{
        .enabled = true,
        .connectionId = "",
    };
    auto eventResult = client_.sendCommandAndGetResponse<Api::EventSubscribe::Okay>(eventCmd, 5000);
    if (eventResult.isError()) {
        results.errorMessage = "Failed to subscribe to event stream: " + eventResult.errorValue();
        SLOG_ERROR("{}", results.errorMessage);
        client_.disconnect();
        return results;
    }
    if (eventResult.value().isError()) {
        results.errorMessage =
            "EventSubscribe rejected: " + eventResult.value().errorValue().message;
        SLOG_ERROR("{}", results.errorMessage);
        client_.disconnect();
        return results;
    }

    // Send EvolutionStart command.
    SLOG_INFO("Starting evolution training:");
    SLOG_INFO("  Scenario: {}", toString(config.scenarioId));
    SLOG_INFO("  Generations: {}", config.evolution.maxGenerations);
    SLOG_INFO("  Population: {}", config.evolution.populationSize);
    SLOG_INFO("  Tournament size: {}", config.evolution.tournamentSize);
    SLOG_INFO("  Mutation rate: {}", config.mutation.rate);

    auto startResult = client_.sendCommandAndGetResponse<Api::EvolutionStart::Okay>(config, 10000);
    if (startResult.isError()) {
        results.errorMessage = "Failed to start evolution: " + startResult.errorValue();
        SLOG_ERROR("{}", results.errorMessage);
        client_.disconnect();
        return results;
    }
    if (startResult.value().isError()) {
        results.errorMessage =
            "Server rejected EvolutionStart: " + startResult.value().errorValue().message;
        SLOG_ERROR("{}", results.errorMessage);
        client_.disconnect();
        return results;
    }

    SLOG_INFO("Evolution started, monitoring progress...\n");

    auto startTime = std::chrono::steady_clock::now();

    // Monitor progress until completion or stop requested.
    while (!stopRequested_) {
        // Check server health for local runs.
        if (!isRemote && !subprocessManager_.isServerRunning()) {
            results.errorMessage = "Server process died during training";
            SLOG_ERROR("{}", results.errorMessage);
            break;
        }

        // Process any pending messages.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Update display if progress changed.
        if (progressUpdated.exchange(false)) {
            displayProgress(
                latestProgress.generation,
                latestProgress.maxGenerations,
                latestProgress.currentEval,
                latestProgress.populationSize,
                latestProgress.bestFitnessThisGen,
                latestProgress.bestFitnessAllTime,
                latestProgress.averageFitness);

            // Check for completion.
            if (latestProgress.generation >= latestProgress.maxGenerations
                && latestProgress.maxGenerations > 0) {
                results.completed = true;
                results.bestFitnessAllTime = latestProgress.bestFitnessAllTime;
                results.bestFitnessLastGen = latestProgress.bestFitnessThisGen;
                results.averageFitnessLastGen = latestProgress.averageFitness;
                results.bestGenomeId = latestProgress.bestGenomeId;
                SLOG_INFO("\nEvolution complete!");
                break;
            }
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    results.durationSec = std::chrono::duration<double>(endTime - startTime).count();

    // If stopped early, send EvolutionStop.
    if (stopRequested_ && !results.completed) {
        SLOG_INFO("\nStopping evolution...");
        Api::EvolutionStop::Command stopCmd;
        client_.sendCommandAndGetResponse<std::monostate>(stopCmd, 2000);

        // Capture final state.
        results.bestFitnessAllTime = latestProgress.bestFitnessAllTime;
        results.bestFitnessLastGen = latestProgress.bestFitnessThisGen;
        results.averageFitnessLastGen = latestProgress.averageFitness;
        results.bestGenomeId = latestProgress.bestGenomeId;
        results.totalGenerations = latestProgress.generation;
    }

    // Shutdown local server.
    if (!isRemote) {
        SLOG_INFO("Shutting down server...");
        Api::Exit::Command exitCmd;
        client_.sendCommandAndGetResponse<std::monostate>(exitCmd, 1000);
    }

    client_.disconnect();
    return results;
}

void TrainRunner::requestStop()
{
    stopRequested_ = true;
}

void TrainRunner::displayProgress(
    int generation,
    int maxGenerations,
    int currentEval,
    int populationSize,
    double bestThisGen,
    double bestAllTime,
    double avgFitness)
{
    // Only update on generation or evaluation change.
    if (generation == lastGeneration_ && currentEval == lastEval_) {
        return;
    }
    lastGeneration_ = generation;
    lastEval_ = currentEval;

    // Progress bar for current generation's evaluations.
    const int barWidth = 30;
    const int evalProgress = populationSize > 0 ? (currentEval * barWidth) / populationSize : 0;

    std::cerr << "\r";
    std::cerr << "Gen " << std::setw(3) << generation << "/" << maxGenerations << " ";
    std::cerr << "[";
    for (int i = 0; i < barWidth; ++i) {
        std::cerr << (i < evalProgress ? "=" : " ");
    }
    std::cerr << "] ";
    std::cerr << std::setw(3) << currentEval << "/" << populationSize << " ";
    std::cerr << "pop=" << populationSize << " ";
    std::cerr << "gen=" << std::fixed << std::setprecision(2) << bestThisGen << " ";
    std::cerr << "best=" << std::setprecision(2) << bestAllTime << " ";
    std::cerr << "avg=" << std::setprecision(2) << avgFitness;
    std::cerr << std::flush;

    // Newline when generation completes.
    if (currentEval >= populationSize && populationSize > 0) {
        std::cerr << std::endl;
    }
}

} // namespace Client
} // namespace DirtSim
