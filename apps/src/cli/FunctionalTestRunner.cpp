#include "FunctionalTestRunner.h"
#include "core/network/WebSocketService.h"
#include "server/api/StatusGet.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/state-machine/api/SimStop.h"
#include "ui/state-machine/api/StateGet.h"
#include "ui/state-machine/api/StatusGet.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

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

Result<std::monostate, std::string> runCommand(const std::string& command)
{
    const int status = std::system(command.c_str());
    if (status == -1) {
        return Result<std::monostate, std::string>::error("Failed to execute command: " + command);
    }

    if (WIFEXITED(status)) {
        const int exitCode = WEXITSTATUS(status);
        if (exitCode == 0) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }
        return Result<std::monostate, std::string>::error(
            "Command failed with exit code " + std::to_string(exitCode) + ": " + command);
    }

    return Result<std::monostate, std::string>::error("Command terminated abnormally: " + command);
}

Result<Api::StatusGet::Okay, std::string> waitForServerStatus(
    const std::string& serverAddress, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 250;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        Network::WebSocketService client;
        auto connectResult = client.connect(serverAddress, requestTimeoutMs);
        if (connectResult.isValue()) {
            Api::StatusGet::Command cmd{};
            auto response = unwrapResponse(
                client.sendCommandAndGetResponse<Api::StatusGet::Okay>(cmd, requestTimeoutMs));
            client.disconnect();
            if (response.isValue()) {
                return response;
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<Api::StatusGet::Okay, std::string>::error(
                "Timeout waiting for server StatusGet");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<UiApi::StatusGet::Okay, std::string> waitForUiStatus(
    const std::string& uiAddress, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 250;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        Network::WebSocketService client;
        auto connectResult = client.connect(uiAddress, requestTimeoutMs);
        if (connectResult.isValue()) {
            UiApi::StatusGet::Command cmd{};
            auto response = unwrapResponse(
                client.sendCommandAndGetResponse<UiApi::StatusGet::Okay>(cmd, requestTimeoutMs));
            client.disconnect();
            if (response.isValue()) {
                return response;
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<UiApi::StatusGet::Okay, std::string>::error(
                "Timeout waiting for UI StatusGet");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}
} // namespace

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

        auto restartResult = restartServicesAndVerify(uiAddress, serverAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canExit",
        .duration_ms = durationMs,
        .result = std::move(testResult),
    };
}

Result<std::monostate, std::string> FunctionalTestRunner::restartServicesAndVerify(
    const std::string& uiAddress, const std::string& serverAddress, int timeoutMs)
{
    const int restartTimeoutMs = std::max(timeoutMs, 10000);

    std::cerr << "Restarting services..." << std::endl;
    const std::string restartCommand =
        "systemctl restart dirtsim-server.service dirtsim-ui.service";
    if (geteuid() == 0) {
        auto restartResult = runCommand(restartCommand);
        if (restartResult.isError()) {
            return restartResult;
        }
    }
    else {
        auto restartResult = runCommand("sudo -n " + restartCommand);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Restart failed (need sudo?): " + restartResult.errorValue());
        }
    }

    std::cerr << "Waiting for server StatusGet..." << std::endl;
    auto serverStatusResult = waitForServerStatus(serverAddress, restartTimeoutMs);
    if (serverStatusResult.isError()) {
        return Result<std::monostate, std::string>::error(serverStatusResult.errorValue());
    }

    std::cerr << "Waiting for UI StatusGet..." << std::endl;
    auto uiStatusResult = waitForUiStatus(uiAddress, restartTimeoutMs);
    if (uiStatusResult.isError()) {
        return Result<std::monostate, std::string>::error(uiStatusResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

} // namespace Client
} // namespace DirtSim
