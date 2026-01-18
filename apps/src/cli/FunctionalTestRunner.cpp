#include "FunctionalTestRunner.h"
#include "core/ScenarioId.h"
#include "core/network/ClientHello.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStart.h"
#include "server/api/StatusGet.h"
#include "server/api/TrainingResultGet.h"
#include "server/api/TrainingResultList.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/state-machine/api/SimStop.h"
#include "ui/state-machine/api/StateGet.h"
#include "ui/state-machine/api/StatusGet.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace DirtSim {
namespace Client {

namespace {
template <typename OkayT>
Result<OkayT, std::string> unwrapResponse(
    const Result<Result<OkayT, ApiError>, std::string>& response)
{
    if (response.isError()) {
        return Result<OkayT, std::string>::error(response.errorValue());
    }

    const auto& inner = response.value();
    if (inner.isError()) {
        return Result<OkayT, std::string>::error(inner.errorValue().message);
    }

    return Result<OkayT, std::string>::okay(inner.value());
}

Result<UiApi::StateGet::Okay, std::string> requestUiState(
    Network::WebSocketService& client, int timeoutMs)
{
    UiApi::StateGet::Command cmd{};
    return unwrapResponse(client.sendCommandAndGetResponse<UiApi::StateGet::Okay>(cmd, timeoutMs));
}

Result<UiApi::StateGet::Okay, std::string> waitForUiState(
    Network::WebSocketService& client, const std::string& expectedState, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        auto result = requestUiState(client, requestTimeoutMs);
        if (result.isError()) {
            return Result<UiApi::StateGet::Okay, std::string>::error(result.errorValue());
        }
        if (result.value().state == expectedState) {
            return result;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<UiApi::StateGet::Okay, std::string>::error(
                "Timeout waiting for UI state '" + expectedState + "'");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<Api::TrainingResultList::Okay, std::string> waitForTrainingResultList(
    Network::WebSocketService& client, int timeoutMs, size_t minCount)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        Api::TrainingResultList::Command cmd{};
        auto response = unwrapResponse(
            client.sendCommandAndGetResponse<Api::TrainingResultList::Okay>(cmd, requestTimeoutMs));
        if (response.isError()) {
            return Result<Api::TrainingResultList::Okay, std::string>::error(response.errorValue());
        }

        if (response.value().results.size() > minCount) {
            return response;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<Api::TrainingResultList::Okay, std::string>::error(
                "Timeout waiting for TrainingResultList");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}
} // namespace

nlohmann::json FunctionalTrainingSummary::toJson() const
{
    nlohmann::json output;
    output["scenario_id"] = scenario_id;
    output["organism_type"] = organism_type;
    output["population_size"] = population_size;
    output["max_generations"] = max_generations;
    output["completed_generations"] = completed_generations;
    output["best_fitness"] = best_fitness;
    output["average_fitness"] = average_fitness;
    output["total_training_seconds"] = total_training_seconds;
    output["primary_brain_kind"] = primary_brain_kind;
    output["primary_brain_variant"] = primary_brain_variant.has_value()
        ? nlohmann::json(*primary_brain_variant)
        : nlohmann::json(nullptr);
    output["primary_population_count"] = primary_population_count;
    output["training_session_id"] = training_session_id;
    output["candidate_count"] = candidate_count;
    return output;
}

nlohmann::json FunctionalTestSummary::toJson() const
{
    nlohmann::json output;
    output["name"] = name;
    output["duration_ms"] = duration_ms;

    nlohmann::json resultJson;
    if (result.isError()) {
        resultJson["success"] = false;
        resultJson["error"] = result.errorValue();
    }
    else {
        resultJson["success"] = true;
    }
    output["result"] = std::move(resultJson);
    if (training_summary.has_value()) {
        output["training_summary"] = training_summary->toJson();
    }
    return output;
}

FunctionalTestSummary FunctionalTestRunner::runCanExit(
    const std::string& uiAddress, const std::string& serverAddress, int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        std::cerr << "Connecting to UI at " << uiAddress << "..." << std::endl;
        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        std::cerr << "Connecting to server at " << serverAddress << "..." << std::endl;
        auto serverConnect = serverClient.connect(serverAddress, timeoutMs);
        if (serverConnect.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + serverConnect.errorValue());
        }

        Api::StatusGet::Command statusCmd{};
        auto statusResult =
            serverClient.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, timeoutMs);
        const auto statusResponse = unwrapResponse(statusResult);
        if (statusResponse.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server StatusGet failed: " + statusResponse.errorValue());
        }

        const auto& status = statusResponse.value();
        std::cerr << "Server state: " << status.state << " (timestep=" << status.timestep << ")"
                  << std::endl;

        auto stateResult = requestUiState(uiClient, timeoutMs);
        if (stateResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StateGet failed: " + stateResult.errorValue());
        }

        std::cerr << "UI state: " << stateResult.value().state << std::endl;

        const std::string& uiState = stateResult.value().state;
        if (uiState != "StartMenu") {
            if (uiState == "SimRunning" || uiState == "Paused") {
                std::cerr << "Sending SimStop to return to StartMenu..." << std::endl;
                UiApi::SimStop::Command simStopCmd{};
                auto simStopResult =
                    unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(
                        simStopCmd, timeoutMs));
                if (simStopResult.isError()) {
                    uiClient.disconnect();
                    serverClient.disconnect();
                    return Result<std::monostate, std::string>::error(
                        "UI SimStop failed: " + simStopResult.errorValue());
                }

                auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
                if (startMenuResult.isError()) {
                    uiClient.disconnect();
                    serverClient.disconnect();
                    return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
                }
            }
            else {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Unsupported UI state for canExit: " + uiState);
            }
        }

        std::cerr << "Sending Exit command..." << std::endl;
        UiApi::Exit::Command exitCmd{};
        auto exitResult =
            unwrapResponse(uiClient.sendCommandAndGetResponse<std::monostate>(exitCmd, timeoutMs));
        if (exitResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI Exit failed: " + exitResult.errorValue());
        }

        std::cerr << "Exit acknowledged." << std::endl;
        uiClient.disconnect();
        serverClient.disconnect();

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canExit",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanTrain(
    const std::string& uiAddress, const std::string& serverAddress, int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<FunctionalTrainingSummary> trainingSummary;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        Network::WebSocketService uiClient;
        std::cerr << "Connecting to UI at " << uiAddress << "..." << std::endl;
        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        UiApi::StatusGet::Command uiStatusCmd{};
        auto uiStatusResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::StatusGet::Okay>(uiStatusCmd, timeoutMs));
        if (uiStatusResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StatusGet failed: " + uiStatusResult.errorValue());
        }

        const auto& uiStatus = uiStatusResult.value();
        std::cerr << "UI state: " << uiStatus.state
                  << " (connected_to_server=" << (uiStatus.connected_to_server ? "true" : "false")
                  << ")" << std::endl;
        if (!uiStatus.connected_to_server) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error("UI not connected to server");
        }
        uiClient.disconnect();

        Network::WebSocketService serverClient;
        serverClient.setProtocol(Network::Protocol::BINARY);
        Network::ClientHello hello{
            .protocolVersion = Network::kClientHelloProtocolVersion,
            .wantsRender = false,
            .wantsEvents = false,
        };
        serverClient.setClientHello(hello);

        std::cerr << "Connecting to server at " << serverAddress << "..." << std::endl;
        auto connectResult = serverClient.connect(serverAddress, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + connectResult.errorValue());
        }

        Api::StatusGet::Command statusCmd{};
        auto statusResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, timeoutMs));
        if (statusResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server StatusGet failed: " + statusResult.errorValue());
        }

        const auto& status = statusResult.value();
        std::cerr << "Server state: " << status.state << " (timestep=" << status.timestep << ")"
                  << std::endl;

        size_t initialResultCount = 0;
        {
            Api::TrainingResultList::Command listCmd{};
            auto listResult = unwrapResponse(
                serverClient.sendCommandAndGetResponse<Api::TrainingResultList::Okay>(
                    listCmd, timeoutMs));
            if (listResult.isError()) {
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "TrainingResultList failed: " + listResult.errorValue());
            }
            initialResultCount = listResult.value().results.size();
        }

        Api::EvolutionStart::Command startCmd{};
        auto startResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::EvolutionStart::Okay>(startCmd, 10000));
        if (startResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "EvolutionStart failed: " + startResult.errorValue());
        }

        const int trainingTimeoutMs = std::max(timeoutMs, 120000);
        auto listResult =
            waitForTrainingResultList(serverClient, trainingTimeoutMs, initialResultCount);
        if (listResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(listResult.errorValue());
        }

        const auto& results = listResult.value().results;
        const auto& latest = results.back();
        Api::TrainingResultGet::Command getCmd{
            .trainingSessionId = latest.summary.trainingSessionId,
        };
        auto getResult =
            unwrapResponse(serverClient.sendCommandAndGetResponse<Api::TrainingResultGet::Okay>(
                getCmd, timeoutMs));
        if (getResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "TrainingResultGet failed: " + getResult.errorValue());
        }

        const auto& summary = getResult.value().summary;
        trainingSummary = FunctionalTrainingSummary{
            .scenario_id = Scenario::toString(summary.scenarioId),
            .organism_type = static_cast<int>(summary.organismType),
            .population_size = summary.populationSize,
            .max_generations = summary.maxGenerations,
            .completed_generations = summary.completedGenerations,
            .best_fitness = summary.bestFitness,
            .average_fitness = summary.averageFitness,
            .total_training_seconds = summary.totalTrainingSeconds,
            .primary_brain_kind = summary.primaryBrainKind,
            .primary_brain_variant = summary.primaryBrainVariant,
            .primary_population_count = summary.primaryPopulationCount,
            .training_session_id = summary.trainingSessionId.toString(),
            .candidate_count = static_cast<int>(getResult.value().candidates.size()),
        };

        serverClient.disconnect();

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canTrain",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .training_summary = std::move(trainingSummary),
    };
}

} // namespace Client
} // namespace DirtSim
