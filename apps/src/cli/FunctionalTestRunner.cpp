#include "FunctionalTestRunner.h"
#include "core/RenderMessageFull.h"
#include "core/ScenarioId.h"
#include "core/input/PlayerControlFrame.h"
#include "core/network/ClientHello.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/scenarios/ClockConfig.h"
#include "os-manager/api/NetworkSnapshotGet.h"
#include "os-manager/api/RestartServer.h"
#include "os-manager/api/RestartUi.h"
#include "os-manager/api/ScannerModeEnter.h"
#include "os-manager/api/ScannerModeExit.h"
#include "os-manager/api/ScannerSnapshotGet.h"
#include "os-manager/api/StopUi.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/WifiConnect.h"
#include "os-manager/api/WifiConnectCancel.h"
#include "os-manager/api/WifiForget.h"
#include "server/api/GenomeDelete.h"
#include "server/api/NesInputSet.h"
#include "server/api/PlanGet.h"
#include "server/api/PlanList.h"
#include "server/api/PlanPlaybackPauseSet.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/SearchProgress.h"
#include "server/api/SimRun.h"
#include "server/api/SimStop.h"
#include "server/api/StateGet.h"
#include "server/api/StatusGet.h"
#include "server/api/TrainingResultGet.h"
#include "server/api/TrainingResultList.h"
#include "server/api/UserSettingsGet.h"
#include "server/api/UserSettingsReset.h"
#include "server/api/UserSettingsSet.h"
#include "ui/controls/IconRail.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/state-machine/api/GenomeBrowserOpen.h"
#include "ui/state-machine/api/GenomeDetailLoad.h"
#include "ui/state-machine/api/GenomeDetailOpen.h"
#include "ui/state-machine/api/IconRailShowIcons.h"
#include "ui/state-machine/api/IconSelect.h"
#include "ui/state-machine/api/NetworkConnectCancelPress.h"
#include "ui/state-machine/api/NetworkConnectPress.h"
#include "ui/state-machine/api/NetworkDiagnosticsGet.h"
#include "ui/state-machine/api/NetworkPasswordSubmit.h"
#include "ui/state-machine/api/NetworkScannerEnterPress.h"
#include "ui/state-machine/api/NetworkScannerExitPress.h"
#include "ui/state-machine/api/PlanBrowserOpen.h"
#include "ui/state-machine/api/PlanDetailOpen.h"
#include "ui/state-machine/api/PlanDetailSelect.h"
#include "ui/state-machine/api/PlanPlaybackPauseSet.h"
#include "ui/state-machine/api/PlanPlaybackStart.h"
#include "ui/state-machine/api/PlanPlaybackStop.h"
#include "ui/state-machine/api/PlantSeed.h"
#include "ui/state-machine/api/ScreenGrab.h"
#include "ui/state-machine/api/SearchPauseSet.h"
#include "ui/state-machine/api/SearchStart.h"
#include "ui/state-machine/api/SearchStop.h"
#include "ui/state-machine/api/SimRun.h"
#include "ui/state-machine/api/SimStop.h"
#include "ui/state-machine/api/StateGet.h"
#include "ui/state-machine/api/StatusGet.h"
#include "ui/state-machine/api/SynthKeyEvent.h"
#include "ui/state-machine/api/TrainingResultSave.h"
#include "ui/state-machine/api/TrainingStart.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <variant>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Client {

namespace {

constexpr uint64_t kFireAndForgetCommandId = std::numeric_limits<uint64_t>::max();

bool isNetworkStateName(const std::string& state)
{
    return state == "NetworkScanner" || state == "NetworkSettings" || state == "NetworkWifi"
        || state == "NetworkWifiConnecting" || state == "NetworkWifiDetails"
        || state == "NetworkWifiPassword";
}

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

int getPollingRequestTimeoutMs(int timeoutMs);
bool isRetryablePollingError(const std::string& error);
Result<OsApi::ScannerSnapshotGet::Okay, std::string> requestScannerSnapshot(
    Network::WebSocketService& client, int timeoutMs);
Result<OsApi::SystemStatus::Okay, std::string> requestSystemStatus(
    Network::WebSocketService& client, int timeoutMs);

std::vector<uint8_t> base64Decode(const std::string& encoded)
{
    static const int base64_index[256] = {
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  62, 63, 62, 62, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0,  0,  0,  0,  0,
        0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
        19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,  63, 0,  26, 27, 28, 29, 30, 31, 32, 33,
        34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
    };

    std::vector<uint8_t> decoded;
    decoded.reserve(encoded.size() * 3 / 4);

    uint32_t val = 0;
    int valBits = -8;

    for (unsigned char c : encoded) {
        if (c == '=') {
            break;
        }
        if (c >= sizeof(base64_index) / sizeof(base64_index[0])) {
            continue;
        }
        val = (val << 6) + base64_index[c];
        valBits += 6;
        if (valBits >= 0) {
            decoded.push_back(static_cast<uint8_t>((val >> valBits) & 0xFF));
            valBits -= 8;
        }
    }

    return decoded;
}

struct UiScreenshotCaptureError {
    std::string message;
};

Result<std::string, UiScreenshotCaptureError> captureUiScreenshotPng(
    Network::WebSocketService& uiClient,
    const std::string& testName,
    const std::string& label,
    int timeoutMs)
{
    const auto now = std::chrono::system_clock::now();
    const auto timestampMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::filesystem::path outputPath = std::filesystem::path("/tmp")
        / ("dirtsim-functional-test-" + testName + "-" + label + "-" + std::to_string(timestampMs)
           + ".png");

    UiApi::ScreenGrab::Command cmd{
        .scale = 1.0,
        .format = UiApi::ScreenGrab::Format::Png,
        .quality = 23,
        .binaryPayload = false,
    };

    auto grabResult =
        unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::ScreenGrab::Okay>(cmd, timeoutMs));
    if (grabResult.isError()) {
        return Result<std::string, UiScreenshotCaptureError>::error(
            UiScreenshotCaptureError{ .message = "ScreenGrab failed: " + grabResult.errorValue() });
    }

    const auto& okay = grabResult.value();
    if (okay.format != UiApi::ScreenGrab::Format::Png) {
        return Result<std::string, UiScreenshotCaptureError>::error(
            UiScreenshotCaptureError{ .message = "ScreenGrab returned non-PNG payload" });
    }

    std::vector<uint8_t> pngData = base64Decode(okay.data);
    if (pngData.empty()) {
        return Result<std::string, UiScreenshotCaptureError>::error(
            UiScreenshotCaptureError{ .message = "Failed to decode screenshot data" });
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        return Result<std::string, UiScreenshotCaptureError>::error(
            UiScreenshotCaptureError{ .message = "Failed to open screenshot output: "
                                          + outputPath.string() });
    }
    output.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
    output.close();

    if (!output) {
        return Result<std::string, UiScreenshotCaptureError>::error(
            UiScreenshotCaptureError{ .message = "Failed to write screenshot output: "
                                          + outputPath.string() });
    }

    return Result<std::string, UiScreenshotCaptureError>::okay(outputPath.string());
}

Result<std::monostate, std::string> captureRequiredUiScreenshotPng(
    Network::WebSocketService& uiClient,
    const std::string& testName,
    const std::string& label,
    int timeoutMs,
    std::optional<std::string>& screenshotPath)
{
    const auto screenshotResult = captureUiScreenshotPng(uiClient, testName, label, timeoutMs);
    if (screenshotResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Screenshot capture failed for " + testName + " (" + label
            + "): " + screenshotResult.errorValue().message);
    }

    screenshotPath = screenshotResult.value();
    std::cerr << "Saved screenshot to " << *screenshotPath << std::endl;
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

struct WifiFunctionalNetworkConfig {
    std::optional<int> minimumSignalDbm;
    std::optional<std::string> password;
    std::string ssid;
};

struct WifiFunctionalTestConfig {
    std::optional<std::string> cancelTargetSsid;
    std::vector<WifiFunctionalNetworkConfig> networks;
};

struct WifiPreflightResult {
    OsApi::NetworkSnapshotGet::Okay snapshot;
    std::optional<std::string> originalConnectedSsid;
};

void from_json(const nlohmann::json& j, WifiFunctionalNetworkConfig& config)
{
    j.at("ssid").get_to(config.ssid);
    if (j.contains("password") && !j.at("password").is_null()) {
        config.password = j.at("password").get<std::string>();
    }
    if (j.contains("minimum_signal_dbm") && !j.at("minimum_signal_dbm").is_null()) {
        config.minimumSignalDbm = j.at("minimum_signal_dbm").get<int>();
    }
}

void from_json(const nlohmann::json& j, WifiFunctionalTestConfig& config)
{
    j.at("networks").get_to(config.networks);
    if (j.contains("cancel_target_ssid") && !j.at("cancel_target_ssid").is_null()) {
        config.cancelTargetSsid = j.at("cancel_target_ssid").get<std::string>();
    }
}

Result<WifiFunctionalTestConfig, std::string> loadWifiFunctionalTestConfig(
    const std::string& configPath)
{
    if (configPath.empty()) {
        return Result<WifiFunctionalTestConfig, std::string>::error(
            "WiFi functional test config path is required");
    }

    std::ifstream in(configPath);
    if (!in) {
        return Result<WifiFunctionalTestConfig, std::string>::error(
            "Failed to open WiFi functional test config: " + configPath);
    }

    try {
        const nlohmann::json configJson = nlohmann::json::parse(in);
        WifiFunctionalTestConfig config = configJson.get<WifiFunctionalTestConfig>();
        if (config.networks.empty()) {
            return Result<WifiFunctionalTestConfig, std::string>::error(
                "WiFi functional test config must define at least one network");
        }

        std::unordered_set<std::string> seenSsids;
        for (const auto& network : config.networks) {
            if (network.ssid.empty()) {
                return Result<WifiFunctionalTestConfig, std::string>::error(
                    "WiFi functional test config contains an empty SSID");
            }
            if (!seenSsids.insert(network.ssid).second) {
                return Result<WifiFunctionalTestConfig, std::string>::error(
                    "WiFi functional test config contains duplicate SSIDs: " + network.ssid);
            }
        }

        if (config.cancelTargetSsid.has_value() && !seenSsids.contains(*config.cancelTargetSsid)) {
            return Result<WifiFunctionalTestConfig, std::string>::error(
                "cancel_target_ssid must match one of the configured networks");
        }

        return Result<WifiFunctionalTestConfig, std::string>::okay(config);
    }
    catch (const std::exception& e) {
        return Result<WifiFunctionalTestConfig, std::string>::error(
            "Failed to parse WiFi functional test config: " + std::string(e.what()));
    }
}

const WifiFunctionalNetworkConfig* findWifiNetworkConfig(
    const WifiFunctionalTestConfig& config, const std::string& ssid)
{
    const auto it = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) { return network.ssid == ssid; });
    return it == config.networks.end() ? nullptr : &(*it);
}

std::string joinStrings(const std::vector<std::string>& items, const std::string& separator)
{
    std::string result;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            result += separator;
        }
        result += items[i];
    }
    return result;
}

Result<OsApi::NetworkSnapshotGet::Okay, std::string> requestNetworkSnapshot(
    Network::WebSocketService& client, bool forceRefresh, int timeoutMs)
{
    OsApi::NetworkSnapshotGet::Command cmd{ .forceRefresh = forceRefresh };
    return unwrapResponse(
        client.sendCommandAndGetResponse<OsApi::NetworkSnapshotGet::Okay>(cmd, timeoutMs));
}

Result<UiApi::NetworkDiagnosticsGet::Okay, std::string> requestUiNetworkDiagnostics(
    Network::WebSocketService& client, int timeoutMs)
{
    UiApi::NetworkDiagnosticsGet::Command cmd{};
    return unwrapResponse(
        client.sendCommandAndGetResponse<UiApi::NetworkDiagnosticsGet::Okay>(cmd, timeoutMs));
}

const UiApi::NetworkDiagnosticsGet::NetworkInfo* findUiNetworkInfo(
    const UiApi::NetworkDiagnosticsGet::Okay& status, const std::string& ssid)
{
    const auto it = std::find_if(
        status.networks.begin(),
        status.networks.end(),
        [&](const UiApi::NetworkDiagnosticsGet::NetworkInfo& network) {
            return network.ssid == ssid;
        });
    return it == status.networks.end() ? nullptr : &(*it);
}

template <typename Predicate>
Result<OsApi::NetworkSnapshotGet::Okay, std::string> waitForNetworkSnapshot(
    Network::WebSocketService& client,
    int timeoutMs,
    const std::string& description,
    bool forceRefresh,
    Predicate&& predicate)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = std::clamp(timeoutMs, 2000, 35000);
    std::string lastError;

    while (true) {
        auto result = requestNetworkSnapshot(client, forceRefresh, requestTimeoutMs);
        if (result.isError()) {
            lastError = result.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
        }
        else if (predicate(result.value(), lastError)) {
            return result;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
            return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                "Timeout waiting for " + description);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

template <typename Predicate>
Result<UiApi::NetworkDiagnosticsGet::Okay, std::string> waitForUiNetworkDiagnostics(
    Network::WebSocketService& client,
    int timeoutMs,
    const std::string& description,
    Predicate&& predicate)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        auto result = requestUiNetworkDiagnostics(client, requestTimeoutMs);
        if (result.isError()) {
            lastError = result.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<UiApi::NetworkDiagnosticsGet::Okay, std::string>::error(lastError);
            }
        }
        else if (predicate(result.value(), lastError)) {
            return result;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<UiApi::NetworkDiagnosticsGet::Okay, std::string>::error(lastError);
            }
            return Result<UiApi::NetworkDiagnosticsGet::Okay, std::string>::error(
                "Timeout waiting for " + description);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<UiApi::StateGet::Okay, std::string> requestUiState(
    Network::WebSocketService& client, int timeoutMs)
{
    UiApi::StateGet::Command cmd{};
    return unwrapResponse(client.sendCommandAndGetResponse<UiApi::StateGet::Okay>(cmd, timeoutMs));
}

Result<UiApi::StatusGet::Okay, std::string> requestUiStatus(
    Network::WebSocketService& client, int timeoutMs)
{
    UiApi::StatusGet::Command cmd{};
    return unwrapResponse(client.sendCommandAndGetResponse<UiApi::StatusGet::Okay>(cmd, timeoutMs));
}

bool isTrainingStateName(const std::string& state)
{
    return state == "TrainingIdle" || state == "TrainingActive" || state == "TrainingUnsavedResult";
}

int getPollingRequestTimeoutMs(int timeoutMs)
{
    return std::clamp(timeoutMs, 1000, 5000);
}

bool isRetryablePollingError(const std::string& error)
{
    return error == "Response timeout";
}

Result<UiApi::StateGet::Okay, std::string> waitForUiStateAny(
    Network::WebSocketService& client,
    const std::vector<std::string>& expectedStates,
    int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string expectedList;
    std::string lastError;
    for (size_t i = 0; i < expectedStates.size(); ++i) {
        if (i > 0) {
            expectedList += ", ";
        }
        expectedList += expectedStates[i];
    }

    while (true) {
        auto result = requestUiState(client, requestTimeoutMs);
        if (result.isError()) {
            lastError = result.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<UiApi::StateGet::Okay, std::string>::error(lastError);
            }
        }
        else {
            for (const auto& expectedState : expectedStates) {
                if (result.value().state == expectedState) {
                    return result;
                }
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<UiApi::StateGet::Okay, std::string>::error(lastError);
            }
            return Result<UiApi::StateGet::Okay, std::string>::error(
                "Timeout waiting for UI state (" + expectedList + ")");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<UiApi::StateGet::Okay, std::string> waitForUiState(
    Network::WebSocketService& client, const std::string& expectedState, int timeoutMs)
{
    auto result = waitForUiStateAny(client, { expectedState }, timeoutMs);
    if (result.isError()) {
        const auto& error = result.errorValue();
        if (error.rfind("Timeout waiting for UI state", 0) == 0) {
            return Result<UiApi::StateGet::Okay, std::string>::error(
                "Timeout waiting for UI state '" + expectedState + "'");
        }
        return Result<UiApi::StateGet::Okay, std::string>::error(error);
    }
    return result;
}

Result<Api::StatusGet::Okay, std::string> requestServerStatus(
    Network::WebSocketService& client, int timeoutMs)
{
    Api::StatusGet::Command cmd{};
    return unwrapResponse(client.sendCommandAndGetResponse<Api::StatusGet::Okay>(cmd, timeoutMs));
}

Result<Api::StatusGet::Okay, std::string> waitForServerState(
    Network::WebSocketService& client, const std::string& expectedState, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        auto result = requestServerStatus(client, requestTimeoutMs);
        if (result.isError()) {
            lastError = result.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<Api::StatusGet::Okay, std::string>::error(lastError);
            }
        }
        else if (result.value().state == expectedState) {
            return result;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<Api::StatusGet::Okay, std::string>::error(lastError);
            }
            return Result<Api::StatusGet::Okay, std::string>::error(
                "Timeout waiting for server state '" + expectedState + "'");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<Api::StatusGet::Okay, std::string> waitForServerTimestepAdvance(
    Network::WebSocketService& client, int32_t previousTimestep, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 100;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        auto result = requestServerStatus(client, requestTimeoutMs);
        if (result.isError()) {
            lastError = result.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<Api::StatusGet::Okay, std::string>::error(lastError);
            }
        }
        else if (result.value().timestep > previousTimestep) {
            return result;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<Api::StatusGet::Okay, std::string>::error(lastError);
            }
            return Result<Api::StatusGet::Okay, std::string>::error(
                "Timeout waiting for server timestep to advance");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<std::monostate, std::string> waitForServerScenario(
    Network::WebSocketService& client, Scenario::EnumType expectedScenario, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        auto statusResult = requestServerStatus(client, requestTimeoutMs);
        if (statusResult.isError()) {
            return Result<std::monostate, std::string>::error(statusResult.errorValue());
        }

        const auto& status = statusResult.value();
        if (status.scenario_id.has_value() && status.scenario_id.value() == expectedScenario) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<std::monostate, std::string>::error(
                "Timeout waiting for server scenario");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<std::monostate, std::string> connectServerBinary(
    Network::WebSocketService& client,
    const std::string& serverAddress,
    int timeoutMs,
    bool wantsRender)
{
    client.setProtocol(Network::Protocol::BINARY);
    Network::ClientHello hello{
        .protocolVersion = Network::kClientHelloProtocolVersion,
        .wantsRender = wantsRender,
        .wantsEvents = false,
    };
    client.setClientHello(hello);

    auto connectResult = client.connect(serverAddress, timeoutMs);
    if (connectResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to connect to server: " + connectResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> ensureUiInStartMenu(
    Network::WebSocketService& uiClient, int timeoutMs)
{
    auto uiStatusResult = requestUiStatus(uiClient, timeoutMs);
    if (uiStatusResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI StatusGet failed: " + uiStatusResult.errorValue());
    }

    if (!uiStatusResult.value().connected_to_server) {
        return Result<std::monostate, std::string>::error("UI not connected to server");
    }

    auto uiStateResult = requestUiState(uiClient, timeoutMs);
    if (uiStateResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI StateGet failed: " + uiStateResult.errorValue());
    }

    const std::string& state = uiStateResult.value().state;
    if (state == "StartMenu") {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (state == "SimRunning" || state == "Paused") {
        UiApi::SimStop::Command simStopCmd{};
        auto simStopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(simStopCmd, timeoutMs));
        if (simStopResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "UI SimStop failed: " + simStopResult.errorValue());
        }

        auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
        if (startMenuResult.isError()) {
            return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    return Result<std::monostate, std::string>::error(
        "Unsupported UI state for settings tests: " + state);
}

Result<std::monostate, std::string> ensureServerIdle(
    Network::WebSocketService& serverClient, int timeoutMs)
{
    auto statusResult = requestServerStatus(serverClient, timeoutMs);
    if (statusResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Server StatusGet failed: " + statusResult.errorValue());
    }

    const std::string& state = statusResult.value().state;
    if (state == "Idle") {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (state == "SimRunning" || state == "SimPaused") {
        Api::SimStop::Command simStopCmd{};
        auto simStopResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::SimStop::OkayType>(simStopCmd, timeoutMs));
        if (simStopResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Server SimStop failed: " + simStopResult.errorValue());
        }

        auto idleResult = waitForServerState(serverClient, "Idle", timeoutMs);
        if (idleResult.isError()) {
            return Result<std::monostate, std::string>::error(idleResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    return Result<std::monostate, std::string>::error(
        "Unsupported server state for settings tests: " + state);
}

Result<WifiPreflightResult, std::string> runWifiPreflight(
    Network::WebSocketService& osManagerClient,
    const WifiFunctionalTestConfig& config,
    int timeoutMs)
{
    auto snapshotResult = waitForNetworkSnapshot(
        osManagerClient,
        std::max(timeoutMs, 30000),
        "WiFi test preflight",
        true,
        [&](const OsApi::NetworkSnapshotGet::Okay& snapshot, std::string& lastError) {
            if (snapshot.connectProgress.has_value()) {
                lastError = "WiFi connect already in progress during preflight (ssid="
                    + snapshot.connectProgress->ssid + ")";
                return false;
            }

            std::vector<std::string> problems;
            for (const auto& target : config.networks) {
                int strongestSignal = -200;
                size_t matchCount = 0;
                bool hasSignal = false;
                for (const auto& accessPoint : snapshot.accessPoints) {
                    if (accessPoint.ssid != target.ssid) {
                        continue;
                    }

                    ++matchCount;
                    if (accessPoint.signalDbm.has_value()) {
                        strongestSignal = std::max(strongestSignal, *accessPoint.signalDbm);
                        hasSignal = true;
                    }
                }

                if (matchCount == 0) {
                    problems.push_back("SSID not visible: " + target.ssid);
                    continue;
                }
                if (target.minimumSignalDbm.has_value()
                    && (!hasSignal || strongestSignal < *target.minimumSignalDbm)) {
                    problems.push_back(
                        "SSID signal too weak: " + target.ssid + " ("
                        + std::to_string(strongestSignal) + " dBm)");
                }
            }

            if (!problems.empty()) {
                lastError = joinStrings(problems, "; ");
                return false;
            }

            lastError.clear();
            return true;
        });
    if (snapshotResult.isError()) {
        return Result<WifiPreflightResult, std::string>::error(
            "WiFi preflight failed: " + snapshotResult.errorValue());
    }

    return Result<WifiPreflightResult, std::string>::okay(
        WifiPreflightResult{
            .snapshot = snapshotResult.value(),
            .originalConnectedSsid = snapshotResult.value().status.connected
                ? std::optional<std::string>(snapshotResult.value().status.ssid)
                : std::nullopt,
        });
}

Result<std::monostate, std::string> ensureUiInNetworkScreen(
    Network::WebSocketService& uiClient, int timeoutMs)
{
    auto uiStateResult = requestUiState(uiClient, timeoutMs);
    if (uiStateResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI StateGet failed: " + uiStateResult.errorValue());
    }

    const std::string& state = uiStateResult.value().state;
    if (!isNetworkStateName(state)) {
        auto startMenuResult = ensureUiInStartMenu(uiClient, timeoutMs);
        if (startMenuResult.isError()) {
            return startMenuResult;
        }
    }

    UiApi::IconRailShowIcons::Command showIconsCmd{};
    auto showIconsResult =
        unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::IconRailShowIcons::Okay>(
            showIconsCmd, timeoutMs));
    if (showIconsResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconRailShowIcons failed: " + showIconsResult.errorValue());
    }

    UiApi::IconSelect::Command networkIconCmd{ .id = Ui::IconId::NETWORK };
    auto iconResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(networkIconCmd, timeoutMs));
    if (iconResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconSelect(NETWORK) failed: " + iconResult.errorValue());
    }

    auto networkStateResult = waitForUiState(uiClient, "NetworkWifi", timeoutMs);
    if (networkStateResult.isError()) {
        return Result<std::monostate, std::string>::error(networkStateResult.errorValue());
    }

    auto diagnosticsResult = waitForUiNetworkDiagnostics(
        uiClient,
        timeoutMs,
        "UI Network WiFi view",
        [](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            if (diagnostics.screen == "Wifi") {
                lastError.clear();
                return true;
            }

            lastError = "UI Network view is not showing WiFi";
            return false;
        });
    if (diagnosticsResult.isError()) {
        return Result<std::monostate, std::string>::error(diagnosticsResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> waitForUiTargetNetworksVisible(
    Network::WebSocketService& uiClient, const WifiFunctionalTestConfig& config, int timeoutMs)
{
    auto result = waitForUiNetworkDiagnostics(
        uiClient,
        std::max(timeoutMs, 20000),
        "target WiFi networks in UI",
        [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            std::vector<std::string> missingSsids;
            for (const auto& network : config.networks) {
                if (!findUiNetworkInfo(diagnostics, network.ssid)) {
                    missingSsids.push_back(network.ssid);
                }
            }
            if (missingSsids.empty()) {
                lastError.clear();
                return true;
            }

            lastError = "UI does not show target SSIDs: " + joinStrings(missingSsids, ", ");
            return false;
        });
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> pressUiNetworkConnect(
    Network::WebSocketService& uiClient, const std::string& ssid, int timeoutMs)
{
    UiApi::NetworkConnectPress::Command cmd{ .ssid = ssid };
    auto result = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::NetworkConnectPress::Okay>(cmd, timeoutMs));
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI NetworkConnectPress failed: " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> forgetWifiViaOsManager(
    Network::WebSocketService& osManagerClient, const std::string& ssid, int timeoutMs)
{
    OsApi::WifiForget::Command cmd{ .ssid = ssid };
    auto result = unwrapResponse(
        osManagerClient.sendCommandAndGetResponse<OsApi::WifiForget::Okay>(cmd, timeoutMs));
    if (result.isError()) {
        if (result.errorValue().find("No saved WiFi connection found for SSID")
            != std::string::npos) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }
        return Result<std::monostate, std::string>::error(
            "OS WifiForget failed for " + ssid + ": " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> pressUiNetworkConnectCancel(
    Network::WebSocketService& uiClient, int timeoutMs)
{
    UiApi::NetworkConnectCancelPress::Command cmd{};
    auto result = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::NetworkConnectCancelPress::Okay>(cmd, timeoutMs));
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI NetworkConnectCancelPress failed: " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> submitUiNetworkPassword(
    Network::WebSocketService& uiClient, const std::string& password, int timeoutMs)
{
    UiApi::NetworkPasswordSubmit::Command cmd{ .password = password };
    auto result = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::NetworkPasswordSubmit::Okay>(cmd, timeoutMs));
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI NetworkPasswordSubmit failed: " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> ensureUiInNetworkScannerScreen(
    Network::WebSocketService& uiClient, int timeoutMs)
{
    auto networkResult = ensureUiInNetworkScreen(uiClient, timeoutMs);
    if (networkResult.isError()) {
        return networkResult;
    }

    UiApi::IconSelect::Command scannerIconCmd{ .id = Ui::IconId::SCANNER };
    auto iconResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(scannerIconCmd, timeoutMs));
    if (iconResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconSelect(SCANNER) failed: " + iconResult.errorValue());
    }

    auto diagnosticsResult = waitForUiNetworkDiagnostics(
        uiClient,
        timeoutMs,
        "UI Network scanner view",
        [](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            if (diagnostics.screen == "Scanner") {
                lastError.clear();
                return true;
            }

            lastError = "UI Network view is not showing Scanner";
            return false;
        });
    if (diagnosticsResult.isError()) {
        return Result<std::monostate, std::string>::error(diagnosticsResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> pressUiScannerEnter(
    Network::WebSocketService& uiClient, int timeoutMs)
{
    UiApi::NetworkScannerEnterPress::Command cmd{};
    auto result = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::NetworkScannerEnterPress::Okay>(cmd, timeoutMs));
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI NetworkScannerEnterPress failed: " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> pressUiScannerExit(
    Network::WebSocketService& uiClient, int timeoutMs)
{
    UiApi::NetworkScannerExitPress::Command cmd{};
    auto result = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::NetworkScannerExitPress::Okay>(cmd, timeoutMs));
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI NetworkScannerExitPress failed: " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<UiApi::NetworkDiagnosticsGet::Okay, std::string> waitForUiScannerMode(
    Network::WebSocketService& uiClient, bool active, int timeoutMs)
{
    const std::string description = active ? "UI active scanner mode" : "UI inactive scanner mode";
    return waitForUiNetworkDiagnostics(
        uiClient,
        std::max(timeoutMs, 20000),
        description,
        [active](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            if (diagnostics.screen == "Scanner") {
                if (active && diagnostics.scanner_mode_active && diagnostics.scanner_exit_enabled
                    && !diagnostics.scanner_enter_enabled) {
                    lastError.clear();
                    return true;
                }

                if (!active && !diagnostics.scanner_mode_active
                    && diagnostics.scanner_mode_available && diagnostics.scanner_enter_enabled
                    && !diagnostics.scanner_exit_enabled) {
                    lastError.clear();
                    return true;
                }
            }

            lastError.clear();
            return false;
        });
}

Result<OsApi::SystemStatus::Okay, std::string> waitForSystemScannerMode(
    Network::WebSocketService& osManagerClient, bool active, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        auto statusResult = requestSystemStatus(osManagerClient, requestTimeoutMs);
        if (statusResult.isError()) {
            lastError = statusResult.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<OsApi::SystemStatus::Okay, std::string>::error(lastError);
            }
        }
        else {
            const auto& status = statusResult.value();
            if (!status.scanner_mode_available) {
                return Result<OsApi::SystemStatus::Okay, std::string>::error(
                    "Scanner mode is unavailable: " + status.scanner_mode_detail);
            }
            if (status.scanner_mode_active == active) {
                return statusResult;
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= std::max(timeoutMs, 30000)) {
            if (!lastError.empty()) {
                return Result<OsApi::SystemStatus::Okay, std::string>::error(lastError);
            }
            return Result<OsApi::SystemStatus::Okay, std::string>::error(
                "Timeout waiting for scanner mode "
                + std::string(active ? "activation" : "deactivation"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<OsApi::ScannerSnapshotGet::Okay, std::string> waitForSystemScannerSnapshot(
    Network::WebSocketService& osManagerClient, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        auto snapshotResult = requestScannerSnapshot(osManagerClient, requestTimeoutMs);
        if (snapshotResult.isError()) {
            lastError = snapshotResult.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<OsApi::ScannerSnapshotGet::Okay, std::string>::error(lastError);
            }
        }
        else if (snapshotResult.value().active) {
            return snapshotResult;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= std::max(timeoutMs, 20000)) {
            if (!lastError.empty()) {
                return Result<OsApi::ScannerSnapshotGet::Okay, std::string>::error(lastError);
            }
            return Result<OsApi::ScannerSnapshotGet::Okay, std::string>::error(
                "Timeout waiting for scanner snapshot");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<UiApi::NetworkDiagnosticsGet::Okay, std::string> waitForUiConnectedSsid(
    Network::WebSocketService& uiClient, const std::string& ssid, int timeoutMs)
{
    return waitForUiNetworkDiagnostics(
        uiClient,
        std::max(timeoutMs, 30000),
        "UI connected SSID '" + ssid + "'",
        [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            if (diagnostics.screen == "Wifi" && diagnostics.connected_ssid.has_value()
                && diagnostics.connected_ssid.value() == ssid
                && !diagnostics.connect_progress.has_value() && !diagnostics.password_prompt_visible
                && !diagnostics.connect_overlay_visible) {
                lastError.clear();
                return true;
            }

            lastError.clear();
            return false;
        });
}

Result<UiApi::NetworkDiagnosticsGet::Okay, std::string> waitForUiUnsavedNetwork(
    Network::WebSocketService& uiClient, const std::string& ssid, int timeoutMs)
{
    return waitForUiNetworkDiagnostics(
        uiClient,
        std::max(timeoutMs, 20000),
        "UI unsaved WiFi network '" + ssid + "'",
        [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            const auto* network = findUiNetworkInfo(diagnostics, ssid);
            if (diagnostics.screen == "Wifi" && network && network->status != "Saved"
                && network->status != "Connected") {
                lastError.clear();
                return true;
            }

            lastError.clear();
            return false;
        });
}

Result<OsApi::NetworkSnapshotGet::Okay, std::string> waitForOsUnsavedNetwork(
    Network::WebSocketService& osManagerClient, const std::string& ssid, int timeoutMs)
{
    return waitForNetworkSnapshot(
        osManagerClient,
        std::max(timeoutMs, 20000),
        "OS unsaved WiFi network '" + ssid + "'",
        true,
        [&](const OsApi::NetworkSnapshotGet::Okay& snapshot, std::string& lastError) {
            const auto it = std::find_if(
                snapshot.networks.begin(),
                snapshot.networks.end(),
                [&](const OsApi::NetworkSnapshotGet::WifiNetworkInfo& network) {
                    return network.ssid == ssid;
                });
            if (it != snapshot.networks.end()
                && it->status != OsApi::NetworkSnapshotGet::WifiNetworkStatus::Saved
                && it->status != OsApi::NetworkSnapshotGet::WifiNetworkStatus::Connected) {
                lastError.clear();
                return true;
            }

            lastError.clear();
            return false;
        });
}

Result<UiApi::NetworkDiagnosticsGet::Okay, std::string> waitForUiCancelableConnect(
    Network::WebSocketService& uiClient, const std::string& ssid, int timeoutMs)
{
    return waitForUiNetworkDiagnostics(
        uiClient,
        std::max(timeoutMs, 20000),
        "UI cancelable WiFi connect for '" + ssid + "'",
        [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
            if (diagnostics.screen == "WifiConnecting" && diagnostics.connect_cancel_visible
                && diagnostics.connect_cancel_enabled && diagnostics.connect_progress.has_value()
                && diagnostics.connect_progress->ssid == ssid
                && diagnostics.connect_progress->can_cancel) {
                lastError.clear();
                return true;
            }

            lastError.clear();
            return false;
        });
}

Result<OsApi::NetworkSnapshotGet::Okay, std::string> waitForOsWifiConnectedSsid(
    Network::WebSocketService& osManagerClient, const std::string& ssid, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = std::clamp(timeoutMs, 2000, 35000);
    std::string lastError;
    bool sawTargetProgress = false;

    while (true) {
        auto snapshotResult = requestNetworkSnapshot(osManagerClient, false, requestTimeoutMs);
        if (snapshotResult.isError()) {
            lastError = snapshotResult.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
        }
        else {
            const auto& snapshot = snapshotResult.value();
            if (snapshot.connectProgress.has_value() && snapshot.connectProgress->ssid == ssid) {
                sawTargetProgress = true;
            }

            const bool networksShowConnectedToTarget = std::any_of(
                snapshot.networks.begin(),
                snapshot.networks.end(),
                [&](const OsApi::NetworkSnapshotGet::WifiNetworkInfo& network) {
                    return network.ssid == ssid
                        && network.status
                        == OsApi::NetworkSnapshotGet::WifiNetworkStatus::Connected;
                });
            if (networksShowConnectedToTarget && !snapshot.connectProgress.has_value()) {
                return snapshotResult;
            }

            if (snapshot.connectOutcome.has_value() && snapshot.connectOutcome->ssid == ssid
                && sawTargetProgress) {
                if (snapshot.connectOutcome->canceled) {
                    return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                        "WiFi connect canceled for " + ssid);
                }
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                    "WiFi connect failed for " + ssid + ": " + snapshot.connectOutcome->message);
            }

            if (!sawTargetProgress && snapshot.connectOutcome.has_value()
                && snapshot.connectOutcome->ssid == ssid) {
                lastError = "WiFi connect outcome present before progress for " + ssid + ": "
                    + snapshot.connectOutcome->message;
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= std::max(timeoutMs, 30000)) {
            if (!lastError.empty()) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
            return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                "Timeout waiting for WiFi connection to " + ssid);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<OsApi::NetworkSnapshotGet::Okay, std::string> waitForOsCancelableConnect(
    Network::WebSocketService& osManagerClient, const std::string& ssid, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 250;
    const int requestTimeoutMs = std::clamp(timeoutMs, 2000, 35000);
    std::string lastError;

    while (true) {
        auto snapshotResult = requestNetworkSnapshot(osManagerClient, false, requestTimeoutMs);
        if (snapshotResult.isError()) {
            lastError = snapshotResult.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
        }
        else {
            const auto& snapshot = snapshotResult.value();
            if (snapshot.connectOutcome.has_value() && snapshot.connectOutcome->ssid == ssid) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                    "WiFi connect ended before reaching a cancelable state for " + ssid);
            }
            if (snapshot.status.connected && snapshot.status.ssid == ssid
                && !snapshot.connectProgress.has_value()) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                    "WiFi connect completed before reaching a cancelable state for " + ssid);
            }
            if (snapshot.connectProgress.has_value() && snapshot.connectProgress->ssid == ssid
                && snapshot.connectProgress->canCancel) {
                return snapshotResult;
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= std::max(timeoutMs, 20000)) {
            if (!lastError.empty()) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
            return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                "Timeout waiting for cancelable WiFi connect to " + ssid);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<OsApi::NetworkSnapshotGet::Okay, std::string> waitForOsCanceledConnect(
    Network::WebSocketService& osManagerClient, const std::string& targetSsid, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = std::clamp(timeoutMs, 2000, 35000);
    std::string lastError;

    while (true) {
        auto snapshotResult = requestNetworkSnapshot(osManagerClient, false, requestTimeoutMs);
        if (snapshotResult.isError()) {
            lastError = snapshotResult.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
        }
        else {
            const auto& snapshot = snapshotResult.value();
            if (snapshot.status.connected && snapshot.status.ssid == targetSsid
                && !snapshot.connectProgress.has_value()) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                    "WiFi connect to " + targetSsid + " completed before cancel");
            }
            if (snapshot.connectOutcome.has_value()
                && snapshot.connectOutcome->ssid == targetSsid) {
                if (!snapshot.connectOutcome->canceled) {
                    return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                        "WiFi connect failed instead of canceling for " + targetSsid + ": "
                        + snapshot.connectOutcome->message);
                }
                if (snapshot.status.connected) {
                    return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                        "WiFi cancel should leave the device disconnected, but it is connected to "
                        + snapshot.status.ssid);
                }
                return snapshotResult;
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= std::max(timeoutMs, 30000)) {
            if (!lastError.empty()) {
                return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(lastError);
            }
            return Result<OsApi::NetworkSnapshotGet::Okay, std::string>::error(
                "Timeout waiting for WiFi cancel on " + targetSsid);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

enum class UiWifiConnectStartState {
    Connected,
    InProgress,
    PasswordPrompt,
    ReadyToPress,
};

struct UiWifiConnectStartContext {
    UiApi::NetworkDiagnosticsGet::Okay diagnostics;
    UiWifiConnectStartState state = UiWifiConnectStartState::ReadyToPress;
};

bool isUiConnectedToTargetSsid(
    const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, const std::string& ssid)
{
    return diagnostics.screen == "Wifi" && diagnostics.connected_ssid.has_value()
        && diagnostics.connected_ssid.value() == ssid && !diagnostics.connect_progress.has_value()
        && !diagnostics.password_prompt_visible && !diagnostics.connect_overlay_visible;
}

bool isUiConnectInProgressForTargetSsid(
    const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, const std::string& ssid)
{
    if (diagnostics.connect_progress.has_value() && diagnostics.connect_progress->ssid == ssid) {
        return true;
    }

    return diagnostics.screen == "WifiConnecting" && diagnostics.connect_target_ssid.has_value()
        && diagnostics.connect_target_ssid.value() == ssid;
}

bool isUiPasswordPromptOpenForTargetSsid(
    const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, const std::string& ssid)
{
    return diagnostics.screen == "WifiPassword" && diagnostics.password_prompt_visible
        && diagnostics.password_prompt_target_ssid.has_value()
        && diagnostics.password_prompt_target_ssid.value() == ssid;
}

bool isUiReadyToPressNetworkConnect(
    const UiApi::StateGet::Okay& uiState, const UiApi::NetworkDiagnosticsGet::Okay& diagnostics)
{
    return uiState.state == "NetworkWifi" && diagnostics.screen == "Wifi"
        && !diagnostics.password_prompt_visible && !diagnostics.connect_overlay_visible
        && !diagnostics.connect_progress.has_value();
}

Result<UiWifiConnectStartContext, std::string> waitForUiWifiConnectStartContext(
    Network::WebSocketService& uiClient, const std::string& ssid, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        auto diagnosticsResult = requestUiNetworkDiagnostics(uiClient, requestTimeoutMs);
        if (diagnosticsResult.isError()) {
            lastError = diagnosticsResult.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<UiWifiConnectStartContext, std::string>::error(lastError);
            }
        }
        else {
            auto uiStateResult = requestUiState(uiClient, requestTimeoutMs);
            if (uiStateResult.isError()) {
                lastError = uiStateResult.errorValue();
                if (!isRetryablePollingError(lastError)) {
                    return Result<UiWifiConnectStartContext, std::string>::error(lastError);
                }
            }
            else {
                UiWifiConnectStartContext context{ .diagnostics = diagnosticsResult.value() };
                if (isUiConnectedToTargetSsid(context.diagnostics, ssid)) {
                    context.state = UiWifiConnectStartState::Connected;
                    return Result<UiWifiConnectStartContext, std::string>::okay(std::move(context));
                }

                if (isUiConnectInProgressForTargetSsid(context.diagnostics, ssid)) {
                    context.state = UiWifiConnectStartState::InProgress;
                    return Result<UiWifiConnectStartContext, std::string>::okay(std::move(context));
                }

                if (isUiPasswordPromptOpenForTargetSsid(context.diagnostics, ssid)) {
                    context.state = UiWifiConnectStartState::PasswordPrompt;
                    return Result<UiWifiConnectStartContext, std::string>::okay(std::move(context));
                }

                const auto* networkInfo = findUiNetworkInfo(context.diagnostics, ssid);
                if (!networkInfo) {
                    lastError = "UI does not show SSID " + ssid;
                }
                else if (isUiReadyToPressNetworkConnect(
                             uiStateResult.value(), context.diagnostics)) {
                    context.state = UiWifiConnectStartState::ReadyToPress;
                    return Result<UiWifiConnectStartContext, std::string>::okay(std::move(context));
                }
                else {
                    lastError.clear();
                }
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= std::max(timeoutMs, 15000)) {
            if (!lastError.empty()) {
                return Result<UiWifiConnectStartContext, std::string>::error(lastError);
            }
            return Result<UiWifiConnectStartContext, std::string>::error(
                "Timeout waiting for UI WiFi connect readiness for " + ssid);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<std::monostate, std::string> beginWifiConnectViaUi(
    Network::WebSocketService& uiClient,
    const std::string& ssid,
    const std::optional<std::string>& password,
    int timeoutMs)
{
    auto uiStateResult = requestUiState(uiClient, timeoutMs);
    if (uiStateResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI StateGet failed: " + uiStateResult.errorValue());
    }

    const bool uiAlreadyInNetworkState = isNetworkStateName(uiStateResult.value().state);
    if (!uiAlreadyInNetworkState) {
        auto uiWifiStateResult =
            waitForUiState(uiClient, "NetworkWifi", std::max(timeoutMs, 20000));
        if (uiWifiStateResult.isError()) {
            return Result<std::monostate, std::string>::error(uiWifiStateResult.errorValue());
        }
    }

    auto connectContextResult = waitForUiWifiConnectStartContext(uiClient, ssid, timeoutMs);
    if (connectContextResult.isError()) {
        return Result<std::monostate, std::string>::error(connectContextResult.errorValue());
    }

    auto connectContext = connectContextResult.value();
    const auto* networkInfo = findUiNetworkInfo(connectContext.diagnostics, ssid);
    if (connectContext.state == UiWifiConnectStartState::Connected
        || connectContext.state == UiWifiConnectStartState::InProgress) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    const bool promptAlreadyOpenForTarget =
        connectContext.state == UiWifiConnectStartState::PasswordPrompt;
    const bool requiresPassword =
        promptAlreadyOpenForTarget || (networkInfo && networkInfo->requires_password);

    if (requiresPassword && !password.has_value()) {
        return Result<std::monostate, std::string>::error("Password is required for SSID " + ssid);
    }

    if (!promptAlreadyOpenForTarget) {
        const auto pressStart = std::chrono::steady_clock::now();
        while (true) {
            auto connectResult =
                pressUiNetworkConnect(uiClient, ssid, getPollingRequestTimeoutMs(timeoutMs));
            if (!connectResult.isError()) {
                break;
            }

            const std::string& error = connectResult.errorValue();
            const bool retryable =
                error.find("Command not supported in state: NetworkWifiConnecting")
                    != std::string::npos
                || error.find("Another network action is in progress") != std::string::npos
                || error.find("Response timeout") != std::string::npos;
            if (!retryable) {
                return connectResult;
            }

            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - pressStart)
                                       .count();
            const int remainingTimeoutMs = std::max(1000, timeoutMs - static_cast<int>(elapsedMs));
            auto retryContextResult =
                waitForUiWifiConnectStartContext(uiClient, ssid, remainingTimeoutMs);
            if (retryContextResult.isError()) {
                return Result<std::monostate, std::string>::error(retryContextResult.errorValue());
            }

            connectContext = retryContextResult.value();
            if (connectContext.state == UiWifiConnectStartState::Connected
                || connectContext.state == UiWifiConnectStartState::InProgress
                || connectContext.state == UiWifiConnectStartState::PasswordPrompt) {
                break;
            }
        }
    }

    auto waitForPasswordPrompt = [&](int promptTimeoutMs) -> Result<bool, std::string> {
        const auto start = std::chrono::steady_clock::now();
        const int pollDelayMs = 200;
        const int requestTimeoutMs = getPollingRequestTimeoutMs(promptTimeoutMs);

        while (true) {
            auto promptCheckResult = requestUiNetworkDiagnostics(uiClient, requestTimeoutMs);
            if (promptCheckResult.isError()) {
                if (!isRetryablePollingError(promptCheckResult.errorValue())) {
                    return Result<bool, std::string>::error(promptCheckResult.errorValue());
                }
            }
            else {
                const auto& diagnostics = promptCheckResult.value();
                if (diagnostics.screen == "WifiPassword" && diagnostics.password_prompt_visible
                    && diagnostics.password_prompt_target_ssid.has_value()
                    && diagnostics.password_prompt_target_ssid.value() == ssid) {
                    if (!diagnostics.connect_overlay_visible
                        && !diagnostics.connect_progress.has_value()) {
                        return Result<bool, std::string>::okay(true);
                    }
                }

                const auto* uiNetwork = findUiNetworkInfo(diagnostics, ssid);
                if (uiNetwork && uiNetwork->status == "Connected"
                    && !diagnostics.connect_progress.has_value()
                    && !diagnostics.password_prompt_visible) {
                    return Result<bool, std::string>::okay(false);
                }
            }

            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();
            if (elapsedMs >= promptTimeoutMs) {
                return Result<bool, std::string>::okay(false);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
        }
    };

    if (password.has_value()) {
        const int promptTimeoutMs = timeoutMs;
        auto promptVisibleResult = waitForPasswordPrompt(promptTimeoutMs);
        if (promptVisibleResult.isError()) {
            return Result<std::monostate, std::string>::error(promptVisibleResult.errorValue());
        }
        if (promptVisibleResult.value()) {
            auto promptRecheckResult =
                requestUiNetworkDiagnostics(uiClient, std::clamp(timeoutMs, 1000, 5000));
            if (!promptRecheckResult.isError()) {
                const auto& diagnostics = promptRecheckResult.value();
                const bool promptStillVisible = diagnostics.screen == "WifiPassword"
                    && diagnostics.password_prompt_visible
                    && diagnostics.password_prompt_target_ssid.has_value()
                    && diagnostics.password_prompt_target_ssid.value() == ssid;
                if (!promptStillVisible) {
                    return Result<std::monostate, std::string>::okay(std::monostate{});
                }
            }

            const auto submitStart = std::chrono::steady_clock::now();
            const int submitPollDelayMs = 200;
            const int submitTimeoutMs = 15000;
            const int submitRequestTimeoutMs = std::clamp(timeoutMs, 1000, 5000);

            while (true) {
                auto submitResult =
                    submitUiNetworkPassword(uiClient, password.value(), submitRequestTimeoutMs);
                if (!submitResult.isError()) {
                    break;
                }

                const std::string& error = submitResult.errorValue();
                if (error.find("Password prompt is not open") != std::string::npos
                    || error.find("Command not supported in state: NetworkWifiConnecting")
                        != std::string::npos) {
                    break;
                }

                auto diagnosticsDuringSubmit =
                    requestUiNetworkDiagnostics(uiClient, submitRequestTimeoutMs);
                if (!diagnosticsDuringSubmit.isError()) {
                    const auto& diagnostics = diagnosticsDuringSubmit.value();
                    if ((diagnostics.connect_progress.has_value()
                         && diagnostics.connect_progress->ssid == ssid)
                        || (diagnostics.screen == "Wifi" && !diagnostics.password_prompt_visible)) {
                        break;
                    }
                }

                const bool retryable =
                    error.find("Another network action is in progress") != std::string::npos
                    || error.find("Response timeout") != std::string::npos;
                if (!retryable) {
                    return submitResult;
                }

                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - submitStart)
                                           .count();
                if (elapsedMs >= submitTimeoutMs) {
                    return submitResult;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(submitPollDelayMs));
            }

            auto postSubmitResult = waitForUiNetworkDiagnostics(
                uiClient,
                std::clamp(timeoutMs, 5000, 15000),
                "UI post-password submit state for '" + ssid + "'",
                [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
                    if (diagnostics.connect_progress.has_value()
                        && diagnostics.connect_progress->ssid == ssid) {
                        lastError.clear();
                        return true;
                    }

                    const auto* uiNetwork = findUiNetworkInfo(diagnostics, ssid);
                    if (uiNetwork && uiNetwork->status == "Connected") {
                        lastError.clear();
                        return true;
                    }

                    lastError.clear();
                    return false;
                });
            if (postSubmitResult.isError()) {
                return Result<std::monostate, std::string>::error(postSubmitResult.errorValue());
            }
        }
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> connectToWifiViaUi(
    Network::WebSocketService& uiClient,
    Network::WebSocketService& osManagerClient,
    const std::string& ssid,
    const std::optional<std::string>& password,
    int timeoutMs)
{
    auto beginResult = beginWifiConnectViaUi(uiClient, ssid, password, timeoutMs);
    if (beginResult.isError()) {
        return beginResult;
    }

    auto osConnectedResult = waitForOsWifiConnectedSsid(osManagerClient, ssid, timeoutMs);
    if (osConnectedResult.isError()) {
        return Result<std::monostate, std::string>::error(osConnectedResult.errorValue());
    }

    auto uiConnectedResult = waitForUiConnectedSsid(uiClient, ssid, timeoutMs);
    if (uiConnectedResult.isError()) {
        return Result<std::monostate, std::string>::error(uiConnectedResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> beginWifiConnectViaBackend(
    Network::WebSocketService& osManagerClient,
    const std::string& ssid,
    const std::optional<std::string>& password,
    int /*timeoutMs*/)
{
    OsApi::WifiConnect::Command cmd{
        .ssid = ssid,
        .password = password,
    };
    const auto envelope = Network::make_command_envelope(kFireAndForgetCommandId, cmd);
    auto connectResult = osManagerClient.sendBinary(Network::serialize_envelope(envelope));
    if (connectResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "OS WifiConnect send failed for " + ssid + ": " + connectResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> cancelWifiConnectViaBackend(
    Network::WebSocketService& osManagerClient, int timeoutMs)
{
    OsApi::WifiConnectCancel::Command cmd{};
    auto cancelResult =
        unwrapResponse(osManagerClient.sendCommandAndGetResponse<OsApi::WifiConnectCancel::Okay>(
            cmd, std::clamp(timeoutMs, 5000, 30000)));
    if (cancelResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "OS WifiConnectCancel failed: " + cancelResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> connectToWifiViaBackend(
    Network::WebSocketService& osManagerClient,
    const std::string& ssid,
    const std::optional<std::string>& password,
    int timeoutMs)
{
    auto beginResult = beginWifiConnectViaBackend(osManagerClient, ssid, password, timeoutMs);
    if (beginResult.isError()) {
        return beginResult;
    }

    auto osConnectedResult = waitForOsWifiConnectedSsid(osManagerClient, ssid, timeoutMs);
    if (osConnectedResult.isError()) {
        return Result<std::monostate, std::string>::error(osConnectedResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> enterScannerModeViaBackend(
    Network::WebSocketService& osManagerClient, int timeoutMs)
{
    OsApi::ScannerModeEnter::Command cmd{};
    auto enterResult =
        unwrapResponse(osManagerClient.sendCommandAndGetResponse<OsApi::ScannerModeEnter::Okay>(
            cmd, std::clamp(timeoutMs, 5000, 60000)));
    if (enterResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "OS ScannerModeEnter failed: " + enterResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> exitScannerModeViaBackend(
    Network::WebSocketService& osManagerClient, int timeoutMs)
{
    OsApi::ScannerModeExit::Command cmd{};
    auto exitResult =
        unwrapResponse(osManagerClient.sendCommandAndGetResponse<OsApi::ScannerModeExit::Okay>(
            cmd, std::clamp(timeoutMs, 5000, 60000)));
    if (exitResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "OS ScannerModeExit failed: " + exitResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> ensureScannerModeInactive(
    Network::WebSocketService& osManagerClient, int timeoutMs)
{
    auto statusResult = requestSystemStatus(osManagerClient, getPollingRequestTimeoutMs(timeoutMs));
    if (statusResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "SystemStatus failed: " + statusResult.errorValue());
    }

    const auto& status = statusResult.value();
    if (!status.scanner_mode_available) {
        return Result<std::monostate, std::string>::error(
            "Scanner mode unavailable: " + status.scanner_mode_detail);
    }
    if (!status.scanner_mode_active) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    auto exitResult = exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
    if (exitResult.isError()) {
        return exitResult;
    }

    auto inactiveResult =
        waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
    if (inactiveResult.isError()) {
        return Result<std::monostate, std::string>::error(inactiveResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> restoreOriginalWifiConnection(
    Network::WebSocketService& osManagerClient,
    const std::optional<std::string>& originalConnectedSsid,
    const WifiFunctionalTestConfig& config,
    int timeoutMs)
{
    if (!originalConnectedSsid.has_value() && config.networks.empty()) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    const int wifiTimeoutMs = std::max(timeoutMs, 60000);
    const WifiFunctionalNetworkConfig* restoreNetwork = nullptr;
    if (originalConnectedSsid.has_value()) {
        restoreNetwork = findWifiNetworkConfig(config, originalConnectedSsid.value());
    }
    if (!restoreNetwork && !config.networks.empty()) {
        restoreNetwork = &config.networks.front();
    }
    if (!restoreNetwork) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    const std::string restoreSsid = restoreNetwork->ssid;

    auto snapshotResult =
        requestNetworkSnapshot(osManagerClient, false, std::clamp(wifiTimeoutMs, 2000, 35000));
    if (snapshotResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to query WiFi status during cleanup: " + snapshotResult.errorValue());
    }
    if (snapshotResult.value().status.connected
        && snapshotResult.value().status.ssid == restoreSsid) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    auto beginResult = beginWifiConnectViaBackend(
        osManagerClient, restoreSsid, restoreNetwork->password, wifiTimeoutMs);
    if (beginResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to start restoring WiFi network '" + restoreSsid
            + "': " + beginResult.errorValue());
    }

    auto restoredResult = waitForOsWifiConnectedSsid(osManagerClient, restoreSsid, wifiTimeoutMs);
    if (restoredResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to restore WiFi network '" + restoreSsid + "': " + restoredResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<UserSettings, std::string> fetchUserSettings(
    Network::WebSocketService& serverClient, int timeoutMs)
{
    Api::UserSettingsGet::Command cmd{};
    auto response = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::UserSettingsGet::Okay>(cmd, timeoutMs));
    if (response.isError()) {
        return Result<UserSettings, std::string>::error(
            "UserSettingsGet failed: " + response.errorValue());
    }

    return Result<UserSettings, std::string>::okay(response.value().settings);
}

Result<UserSettings, std::string> updateUserSettings(
    Network::WebSocketService& serverClient, const UserSettings& settings, int timeoutMs)
{
    Api::UserSettingsSet::Command cmd{ .settings = settings };
    auto response = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::UserSettingsSet::Okay>(cmd, timeoutMs));
    if (response.isError()) {
        return Result<UserSettings, std::string>::error(
            "UserSettingsSet failed: " + response.errorValue());
    }

    return Result<UserSettings, std::string>::okay(response.value().settings);
}

Result<UserSettings, std::string> resetUserSettings(
    Network::WebSocketService& serverClient, int timeoutMs)
{
    Api::UserSettingsReset::Command cmd{};
    auto response = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::UserSettingsReset::Okay>(cmd, timeoutMs));
    if (response.isError()) {
        return Result<UserSettings, std::string>::error(
            "UserSettingsReset failed: " + response.errorValue());
    }

    return Result<UserSettings, std::string>::okay(response.value().settings);
}

Result<std::monostate, std::string> waitForClockRenderTimezone(
    Network::WebSocketService& serverClient, Config::ClockTimezone expectedTimezone, int timeoutMs)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool matched = false;
    std::optional<Config::ClockTimezone> lastTimezone;
    std::string parseError;

    serverClient.onBinary([&](const std::vector<std::byte>& payload) {
        try {
            RenderMessageFull fullMessage;
            zpp::bits::in in(payload);
            in(fullMessage).or_throw();

            if (fullMessage.scenario_id != Scenario::EnumType::Clock) {
                return;
            }

            const auto* clockConfig = std::get_if<Config::Clock>(&fullMessage.scenario_config);
            if (!clockConfig) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                lastTimezone = clockConfig->timezone;
                matched = (lastTimezone.value() == expectedTimezone);
            }

            cv.notify_all();
        }
        catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                parseError = e.what();
            }
            cv.notify_all();
        }
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!matched && parseError.empty()) {
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                break;
            }
        }
    }

    serverClient.onBinary([](const std::vector<std::byte>& /*payload*/) {});

    if (matched) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (!parseError.empty()) {
        return Result<std::monostate, std::string>::error(
            "Failed to parse RenderMessage payload: " + parseError);
    }

    std::string detail = "did not receive Clock render config";
    if (lastTimezone.has_value()) {
        detail += ", last timezone=" + std::string(Config::getDisplayName(*lastTimezone));
    }

    return Result<std::monostate, std::string>::error(
        "Timeout waiting for expected clock timezone ("
        + std::string(Config::getDisplayName(expectedTimezone)) + "): " + detail);
}

struct SeedTarget {
    int x = 0;
    int y = 0;
};

Result<SeedTarget, std::string> resolveSeedTarget(const WorldData& data)
{
    if (data.width <= 0 || data.height <= 0) {
        return Result<SeedTarget, std::string>::error("WorldData has invalid dimensions");
    }

    const int centerX = data.width / 2;
    const int centerY = data.height / 2;
    if (!data.inBounds(centerX, centerY)) {
        return Result<SeedTarget, std::string>::error("Center position is out of bounds");
    }

    return Result<SeedTarget, std::string>::okay(SeedTarget{ centerX, centerY });
}

Result<std::monostate, std::string> waitForTreeVision(
    Network::WebSocketService& client, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        Api::StateGet::Command cmd{};
        auto response = unwrapResponse(
            client.sendCommandAndGetResponse<Api::StateGet::Okay>(cmd, requestTimeoutMs));
        if (response.isError()) {
            return Result<std::monostate, std::string>::error(response.errorValue());
        }

        if (response.value().worldData.tree_vision.has_value()) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<std::monostate, std::string>::error(
                "Timeout waiting for tree_vision in WorldData");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<Api::TrainingResultList::Okay, std::string> waitForTrainingResultList(
    Network::WebSocketService& client, int timeoutMs, size_t minCount)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = getPollingRequestTimeoutMs(timeoutMs);
    std::string lastError;

    while (true) {
        Api::TrainingResultList::Command cmd{};
        auto response = unwrapResponse(
            client.sendCommandAndGetResponse<Api::TrainingResultList::Okay>(cmd, requestTimeoutMs));
        if (response.isError()) {
            lastError = response.errorValue();
            if (!isRetryablePollingError(lastError)) {
                return Result<Api::TrainingResultList::Okay, std::string>::error(lastError);
            }
        }
        else if (response.value().results.size() > minCount) {
            return response;
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<Api::TrainingResultList::Okay, std::string>::error(lastError);
            }
            return Result<Api::TrainingResultList::Okay, std::string>::error(
                "Timeout waiting for TrainingResultList");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<UiApi::TrainingResultSave::Okay, std::string> waitForUiTrainingResultSave(
    Network::WebSocketService& client,
    int timeoutMs,
    std::optional<int> count,
    bool restart = false)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);
    std::string lastError;

    while (true) {
        UiApi::TrainingResultSave::Command cmd{};
        if (count.has_value()) {
            cmd.count = count;
        }
        cmd.restart = restart;

        auto response =
            unwrapResponse(client.sendCommandAndGetResponse<UiApi::TrainingResultSave::Okay>(
                cmd, requestTimeoutMs));
        if (response.isValue()) {
            return Result<UiApi::TrainingResultSave::Okay, std::string>::okay(response.value());
        }

        lastError = response.errorValue();

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            if (!lastError.empty()) {
                return Result<UiApi::TrainingResultSave::Okay, std::string>::error(lastError);
            }
            return Result<UiApi::TrainingResultSave::Okay, std::string>::error(
                "Timeout waiting for TrainingResultSave");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<std::monostate, std::string> deleteGenomes(
    Network::WebSocketService& client, const std::unordered_set<GenomeId>& ids, int timeoutMs)
{
    for (const auto& id : ids) {
        Api::GenomeDelete::Command cmd{ .id = id };
        auto response = unwrapResponse(
            client.sendCommandAndGetResponse<Api::GenomeDelete::Okay>(cmd, timeoutMs));
        if (response.isError()) {
            return Result<std::monostate, std::string>::error(
                "GenomeDelete failed: " + response.errorValue());
        }
        if (!response.value().success) {
            return Result<std::monostate, std::string>::error(
                "GenomeDelete returned success=false for " + id.toShortString());
        }
    }
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> waitForGenomeInWorld(
    Network::WebSocketService& client, const GenomeId& genomeId, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 200;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);

    while (true) {
        Api::StateGet::Command cmd{};
        auto response = unwrapResponse(
            client.sendCommandAndGetResponse<Api::StateGet::Okay>(cmd, requestTimeoutMs));
        if (response.isError()) {
            return Result<std::monostate, std::string>::error(response.errorValue());
        }

        const auto& worldData = response.value().worldData;
        for (const auto& debug : worldData.organism_debug) {
            if (debug.genome_id.has_value() && debug.genome_id.value() == genomeId) {
                return Result<std::monostate, std::string>::okay(std::monostate{});
            }
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= timeoutMs) {
            return Result<std::monostate, std::string>::error(
                "Timeout waiting for genome to load into world");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<FunctionalTrainingSummary, std::string> runTrainingSession(
    const std::string& uiAddress,
    const std::string& serverAddress,
    int timeoutMs,
    int maxGenerations,
    const std::optional<UiApi::TrainingStart::Command>& trainingStartOverride = std::nullopt)
{
    Network::WebSocketService uiClient;
    std::cerr << "Connecting to UI at " << uiAddress << "..." << std::endl;
    auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
    if (uiConnect.isError()) {
        return Result<FunctionalTrainingSummary, std::string>::error(
            "Failed to connect to UI: " + uiConnect.errorValue());
    }

    UiApi::StatusGet::Command uiStatusCmd{};
    auto uiStatusResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::StatusGet::Okay>(uiStatusCmd, timeoutMs));
    if (uiStatusResult.isError()) {
        uiClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "UI StatusGet failed: " + uiStatusResult.errorValue());
    }

    const auto& uiStatus = uiStatusResult.value();
    std::cerr << "UI state: " << uiStatus.state
              << " (connected_to_server=" << (uiStatus.connected_to_server ? "true" : "false")
              << ")" << std::endl;
    if (!uiStatus.connected_to_server) {
        uiClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error("UI not connected to server");
    }
    if (uiStatus.state == "SimRunning" || uiStatus.state == "Paused") {
        std::cerr << "Sending UI SimStop to return to StartMenu..." << std::endl;
        UiApi::SimStop::Command simStopCmd{};
        auto simStopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(simStopCmd, timeoutMs));
        if (simStopResult.isError()) {
            uiClient.disconnect();
            return Result<FunctionalTrainingSummary, std::string>::error(
                "UI SimStop failed: " + simStopResult.errorValue());
        }

        auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
        if (startMenuResult.isError()) {
            uiClient.disconnect();
            return Result<FunctionalTrainingSummary, std::string>::error(
                startMenuResult.errorValue());
        }
    }

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
        uiClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "Failed to connect to server: " + connectResult.errorValue());
    }

    Api::StatusGet::Command statusCmd{};
    auto statusResult = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, timeoutMs));
    if (statusResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "Server StatusGet failed: " + statusResult.errorValue());
    }

    const auto& status = statusResult.value();
    std::cerr << "Server state: " << status.state << " (timestep=" << status.timestep << ")"
              << std::endl;
    if (status.state == "SimRunning" || status.state == "SimPaused") {
        std::cerr << "Sending SimStop to return to Idle..." << std::endl;
        Api::SimStop::Command simStopCmd{};
        auto simStopResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::SimStop::OkayType>(simStopCmd, timeoutMs));
        if (simStopResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<FunctionalTrainingSummary, std::string>::error(
                "Server SimStop failed: " + simStopResult.errorValue());
        }

        auto idleResult = waitForServerState(serverClient, "Idle", timeoutMs);
        if (idleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<FunctionalTrainingSummary, std::string>::error(idleResult.errorValue());
        }
    }

    size_t initialResultCount = 0;
    std::unordered_set<std::string> initialResultIds;
    {
        Api::TrainingResultList::Command listCmd{};
        auto listResult =
            unwrapResponse(serverClient.sendCommandAndGetResponse<Api::TrainingResultList::Okay>(
                listCmd, timeoutMs));
        if (listResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<FunctionalTrainingSummary, std::string>::error(
                "TrainingResultList failed: " + listResult.errorValue());
        }
        const auto& results = listResult.value().results;
        initialResultCount = results.size();
        initialResultIds.reserve(results.size());
        for (const auto& entry : results) {
            initialResultIds.insert(entry.summary.trainingSessionId.toString());
        }
    }

    UiApi::TrainingStart::Command trainCmd;
    if (trainingStartOverride.has_value()) {
        trainCmd = trainingStartOverride.value();
    }
    else {
        trainCmd.evolution.populationSize = 2;
        trainCmd.evolution.maxGenerations = maxGenerations;
        trainCmd.evolution.maxSimulationTime = 0.1;
        trainCmd.training.scenarioId = Scenario::EnumType::TreeGermination;
        trainCmd.training.organismType = OrganismType::TREE;
        PopulationSpec population;
        population.brainKind = TrainingBrainKind::NeuralNet;
        population.count = trainCmd.evolution.populationSize;
        population.randomCount = trainCmd.evolution.populationSize;
        trainCmd.training.population = { population };
    }

    auto trainResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::TrainingStart::Okay>(trainCmd, timeoutMs));
    if (trainResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "UI TrainingStart failed: " + trainResult.errorValue());
    }

    auto trainingStateResult =
        waitForUiStateAny(uiClient, { "TrainingActive", "TrainingUnsavedResult" }, timeoutMs);
    if (trainingStateResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            trainingStateResult.errorValue());
    }

    const int trainingTimeoutMs = std::max(timeoutMs, 120000);
    auto unsavedUiStateResult =
        waitForUiState(uiClient, "TrainingUnsavedResult", trainingTimeoutMs);
    if (unsavedUiStateResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            unsavedUiStateResult.errorValue());
    }

    const int saveTimeoutMs = std::max(timeoutMs, 10000);
    auto saveResult = waitForUiTrainingResultSave(uiClient, saveTimeoutMs, std::nullopt);
    if (saveResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "UI TrainingResultSave failed: " + saveResult.errorValue());
    }

    auto listResult =
        waitForTrainingResultList(serverClient, trainingTimeoutMs, initialResultCount);
    if (listResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(listResult.errorValue());
    }

    const auto& results = listResult.value().results;
    const Api::TrainingResultList::Entry* latest = nullptr;
    for (const auto& entry : results) {
        if (initialResultIds.find(entry.summary.trainingSessionId.toString())
            == initialResultIds.end()) {
            latest = &entry;
            break;
        }
    }
    if (!latest) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "TrainingResultList did not include a new training result");
    }
    Api::TrainingResultGet::Command getCmd{
        .trainingSessionId = latest->summary.trainingSessionId,
    };
    auto getResult = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::TrainingResultGet::Okay>(getCmd, timeoutMs));
    if (getResult.isError()) {
        uiClient.disconnect();
        serverClient.disconnect();
        return Result<FunctionalTrainingSummary, std::string>::error(
            "TrainingResultGet failed: " + getResult.errorValue());
    }

    const auto& summary = getResult.value().summary;
    FunctionalTrainingSummary trainingSummary{
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

    uiClient.disconnect();
    serverClient.disconnect();

    return Result<FunctionalTrainingSummary, std::string>::okay(trainingSummary);
}

struct SearchSessionResult {
    Api::Plan plan;
};

Result<std::monostate, std::string> connectSearchClients(
    Network::WebSocketService& uiClient,
    Network::WebSocketService& serverClient,
    const std::string& uiAddress,
    const std::string& serverAddress,
    int timeoutMs)
{
    auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
    if (uiConnect.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to connect to UI: " + uiConnect.errorValue());
    }

    const auto uiStatusResult = requestUiStatus(uiClient, timeoutMs);
    if (uiStatusResult.isError()) {
        uiClient.disconnect();
        return Result<std::monostate, std::string>::error(
            "UI StatusGet failed: " + uiStatusResult.errorValue());
    }

    if (!uiStatusResult.value().connected_to_server) {
        uiClient.disconnect();
        return Result<std::monostate, std::string>::error("UI not connected to server");
    }

    serverClient.setProtocol(Network::Protocol::BINARY);
    Network::ClientHello hello{
        .protocolVersion = Network::kClientHelloProtocolVersion,
        .wantsRender = false,
        .wantsEvents = false,
    };
    serverClient.setClientHello(hello);

    auto serverConnect = serverClient.connect(serverAddress, timeoutMs);
    if (serverConnect.isError()) {
        uiClient.disconnect();
        return Result<std::monostate, std::string>::error(
            "Failed to connect to server: " + serverConnect.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::vector<Api::PlanList::Entry>, std::string> requestPlanList(
    Network::WebSocketService& serverClient, int timeoutMs)
{
    Api::PlanList::Command command{};
    const auto result = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::PlanList::Okay>(command, timeoutMs));
    if (result.isError()) {
        return Result<std::vector<Api::PlanList::Entry>, std::string>::error(result.errorValue());
    }
    return Result<std::vector<Api::PlanList::Entry>, std::string>::okay(result.value().plans);
}

Result<Api::Plan, std::string> requestPlan(
    Network::WebSocketService& serverClient, UUID planId, int timeoutMs)
{
    Api::PlanGet::Command command{
        .planId = planId,
    };
    const auto result = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::PlanGet::Okay>(command, timeoutMs));
    if (result.isError()) {
        return Result<Api::Plan, std::string>::error(result.errorValue());
    }
    return Result<Api::Plan, std::string>::okay(result.value().plan);
}

Result<Api::SearchProgress, std::string> requestSearchProgress(
    Network::WebSocketService& serverClient, int timeoutMs)
{
    Api::SearchProgressGet::Command command{};
    const auto result = unwrapResponse(
        serverClient.sendCommandAndGetResponse<Api::SearchProgressGet::Okay>(command, timeoutMs));
    if (result.isError()) {
        return Result<Api::SearchProgress, std::string>::error(result.errorValue());
    }
    return Result<Api::SearchProgress, std::string>::okay(result.value().progress);
}

Result<std::monostate, std::string> showUiIcons(Network::WebSocketService& uiClient, int timeoutMs)
{
    const auto result =
        unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::IconRailShowIcons::Okay>(
            UiApi::IconRailShowIcons::Command{}, timeoutMs));
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconRailShowIcons failed: " + result.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> runDefaultSimulationAndReturnToStartMenu(
    Network::WebSocketService& uiClient, Network::WebSocketService& serverClient, int timeoutMs)
{
    const auto startMenuResult = ensureUiInStartMenu(uiClient, timeoutMs);
    if (startMenuResult.isError()) {
        return startMenuResult;
    }

    const auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
    if (serverIdleResult.isError()) {
        return serverIdleResult;
    }

    const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
    if (showIconsResult.isError()) {
        return showIconsResult;
    }

    UiApi::IconSelect::Command startScenarioIcon{
        .id = Ui::IconId::SCENARIO,
    };
    const auto startScenarioResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(startScenarioIcon, timeoutMs));
    if (startScenarioResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconSelect(SCENARIO) failed: " + startScenarioResult.errorValue());
    }

    const auto uiRunningResult = waitForUiState(uiClient, "SimRunning", timeoutMs);
    if (uiRunningResult.isError()) {
        return Result<std::monostate, std::string>::error(uiRunningResult.errorValue());
    }

    const auto serverRunningResult = waitForServerState(serverClient, "SimRunning", timeoutMs);
    if (serverRunningResult.isError()) {
        return Result<std::monostate, std::string>::error(serverRunningResult.errorValue());
    }

    UiApi::SimStop::Command simStopCmd{};
    const auto simStopResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(simStopCmd, timeoutMs));
    if (simStopResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI SimStop failed after default simulation: " + simStopResult.errorValue());
    }

    const auto uiStartMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
    if (uiStartMenuResult.isError()) {
        return Result<std::monostate, std::string>::error(uiStartMenuResult.errorValue());
    }

    const auto serverIdleAfterStopResult = waitForServerState(serverClient, "Idle", timeoutMs);
    if (serverIdleAfterStopResult.isError()) {
        return Result<std::monostate, std::string>::error(serverIdleAfterStopResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> ensureSearchIdle(
    Network::WebSocketService& uiClient, Network::WebSocketService& serverClient, int timeoutMs)
{
    const auto uiStateResult = requestUiState(uiClient, timeoutMs);
    if (uiStateResult.isError()) {
        return Result<std::monostate, std::string>::error(uiStateResult.errorValue());
    }

    const std::string& uiState = uiStateResult.value().state;
    if (uiState == "SearchIdle") {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (uiState == "SearchPlanBrowser") {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (uiState == "SearchActive") {
        const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showIconsResult.isError()) {
            return showIconsResult;
        }

        UiApi::IconSelect::Command stopIcon{
            .id = Ui::IconId::STOP,
        };
        auto stopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(stopIcon, timeoutMs));
        if (stopResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(STOP) failed while recovering SearchActive to SearchIdle: "
                + stopResult.errorValue());
        }
        auto idleResult = waitForUiState(uiClient, "SearchIdle", timeoutMs);
        if (idleResult.isError()) {
            return Result<std::monostate, std::string>::error(idleResult.errorValue());
        }
        auto serverIdleResult = waitForServerState(serverClient, "Idle", timeoutMs);
        if (serverIdleResult.isError()) {
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (uiState == "PlanPlayback") {
        const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showIconsResult.isError()) {
            return showIconsResult;
        }

        UiApi::IconSelect::Command stopIcon{
            .id = Ui::IconId::STOP,
        };
        auto stopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(stopIcon, timeoutMs));
        if (stopResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(STOP) failed while recovering PlanPlayback to SearchIdle: "
                + stopResult.errorValue());
        }
        auto idleResult = waitForUiState(uiClient, "SearchIdle", timeoutMs);
        if (idleResult.isError()) {
            return Result<std::monostate, std::string>::error(idleResult.errorValue());
        }
        auto serverIdleResult = waitForServerState(serverClient, "Idle", timeoutMs);
        if (serverIdleResult.isError()) {
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (uiState != "StartMenu") {
        return Result<std::monostate, std::string>::error(
            "Expected StartMenu or Search state before entering SearchIdle, got " + uiState);
    }

    const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
    if (showIconsResult.isError()) {
        return showIconsResult;
    }

    UiApi::IconSelect::Command iconSelect{
        .id = Ui::IconId::SCANNER,
    };
    const auto iconResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(iconSelect, timeoutMs));
    if (iconResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconSelect(SCANNER) failed: " + iconResult.errorValue());
    }

    const auto idleResult = waitForUiState(uiClient, "SearchIdle", timeoutMs);
    if (idleResult.isError()) {
        return Result<std::monostate, std::string>::error(idleResult.errorValue());
    }

    const auto serverIdleResult = waitForServerState(serverClient, "Idle", timeoutMs);
    if (serverIdleResult.isError()) {
        return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> startSearchAndWaitActive(
    Network::WebSocketService& uiClient, Network::WebSocketService& serverClient, int timeoutMs)
{
    const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
    if (showIconsResult.isError()) {
        return showIconsResult;
    }

    UiApi::IconSelect::Command iconSelect{
        .id = Ui::IconId::SCANNER,
    };
    const auto searchStartResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(iconSelect, timeoutMs));
    if (searchStartResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconSelect(SCANNER) failed: " + searchStartResult.errorValue());
    }

    const int activeTimeoutMs = std::max(timeoutMs, 10000);
    const auto uiActiveResult = waitForUiState(uiClient, "SearchActive", activeTimeoutMs);
    if (uiActiveResult.isError()) {
        return Result<std::monostate, std::string>::error(uiActiveResult.errorValue());
    }

    const auto serverActiveResult =
        waitForServerState(serverClient, "SearchActive", activeTimeoutMs);
    if (serverActiveResult.isError()) {
        return Result<std::monostate, std::string>::error(serverActiveResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> startPlanPlaybackAndWaitActive(
    Network::WebSocketService& uiClient, Network::WebSocketService& serverClient, int timeoutMs)
{
    const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
    if (showIconsResult.isError()) {
        return showIconsResult;
    }

    UiApi::IconSelect::Command iconSelect{
        .id = Ui::IconId::PLAY,
    };
    const auto playbackStartResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(iconSelect, timeoutMs));
    if (playbackStartResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI IconSelect(PLAY) failed: " + playbackStartResult.errorValue());
    }

    const int playbackTimeoutMs = std::max(timeoutMs, 10000);
    const auto uiPlaybackResult = waitForUiState(uiClient, "PlanPlayback", playbackTimeoutMs);
    if (uiPlaybackResult.isError()) {
        return Result<std::monostate, std::string>::error(uiPlaybackResult.errorValue());
    }

    const auto serverPlaybackResult =
        waitForServerState(serverClient, "PlanPlayback", playbackTimeoutMs);
    if (serverPlaybackResult.isError()) {
        return Result<std::monostate, std::string>::error(serverPlaybackResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> browseAndSelectPlan(
    Network::WebSocketService& uiClient, UUID planId, int timeoutMs)
{
    const auto uiStateResult = requestUiState(uiClient, timeoutMs);
    if (uiStateResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI StateGet failed before opening plan browser: " + uiStateResult.errorValue());
    }

    if (uiStateResult.value().state != "SearchPlanBrowser") {
        const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showIconsResult.isError()) {
            return showIconsResult;
        }

        UiApi::IconSelect::Command openBrowser{
            .id = Ui::IconId::PLAN_BROWSER,
        };
        const auto browserOpenResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(openBrowser, timeoutMs));
        if (browserOpenResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(PLAN_BROWSER) failed: " + browserOpenResult.errorValue());
        }
    }

    const auto browserStateResult = waitForUiState(uiClient, "SearchPlanBrowser", timeoutMs);
    if (browserStateResult.isError()) {
        return Result<std::monostate, std::string>::error(browserStateResult.errorValue());
    }

    UiApi::PlanDetailOpen::Command detailOpen{
        .id = planId,
    };
    const auto detailOpenResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::PlanDetailOpen::Okay>(detailOpen, timeoutMs));
    if (detailOpenResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI PlanDetailOpen failed: " + detailOpenResult.errorValue());
    }
    if (detailOpenResult.value().id != planId) {
        return Result<std::monostate, std::string>::error(
            "UI PlanDetailOpen returned unexpected plan id");
    }

    UiApi::PlanDetailSelect::Command detailSelect{
        .id = planId,
    };
    const auto detailSelectResult = unwrapResponse(
        uiClient.sendCommandAndGetResponse<UiApi::PlanDetailSelect::Okay>(detailSelect, timeoutMs));
    if (detailSelectResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "UI PlanDetailSelect failed: " + detailSelectResult.errorValue());
    }
    if (!detailSelectResult.value().selected) {
        return Result<std::monostate, std::string>::error(
            "UI PlanDetailSelect returned selected=false");
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<SearchSessionResult, std::string> runSearchHoldRightSession(
    Network::WebSocketService& uiClient, Network::WebSocketService& serverClient, int timeoutMs)
{
    const auto initialPlansResult = requestPlanList(serverClient, timeoutMs);
    if (initialPlansResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(
            "PlanList failed before search: " + initialPlansResult.errorValue());
    }

    std::unordered_set<std::string> initialPlanIds;
    initialPlanIds.reserve(initialPlansResult.value().size());
    for (const auto& entry : initialPlansResult.value()) {
        initialPlanIds.insert(entry.summary.id.toString());
    }

    const auto searchIdleResult = ensureSearchIdle(uiClient, serverClient, timeoutMs);
    if (searchIdleResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(searchIdleResult.errorValue());
    }

    const auto startResult = startSearchAndWaitActive(uiClient, serverClient, timeoutMs);
    if (startResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(startResult.errorValue());
    }

    const int searchTimeoutMs = std::max(timeoutMs, 120000);
    const auto uiIdleResult = waitForUiState(uiClient, "SearchIdle", searchTimeoutMs);
    if (uiIdleResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(uiIdleResult.errorValue());
    }

    const auto serverIdleResult = waitForServerState(serverClient, "Idle", searchTimeoutMs);
    if (serverIdleResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(serverIdleResult.errorValue());
    }

    const auto finalPlansResult = requestPlanList(serverClient, timeoutMs);
    if (finalPlansResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(
            "PlanList failed after search: " + finalPlansResult.errorValue());
    }

    std::optional<UUID> newPlanId = std::nullopt;
    for (const auto& entry : finalPlansResult.value()) {
        if (!initialPlanIds.contains(entry.summary.id.toString())) {
            newPlanId = entry.summary.id;
            break;
        }
    }
    if (!newPlanId.has_value()) {
        return Result<SearchSessionResult, std::string>::error(
            "Search did not produce a new saved plan");
    }

    const auto planResult = requestPlan(serverClient, newPlanId.value(), timeoutMs);
    if (planResult.isError()) {
        return Result<SearchSessionResult, std::string>::error(
            "PlanGet failed for saved plan: " + planResult.errorValue());
    }

    return Result<SearchSessionResult, std::string>::okay(
        SearchSessionResult{ .plan = planResult.value() });
}

Result<OsApi::SystemStatus::Okay, std::string> requestSystemStatus(
    Network::WebSocketService& client, int timeoutMs)
{
    OsApi::SystemStatus::Command cmd{};
    return unwrapResponse(
        client.sendCommandAndGetResponse<OsApi::SystemStatus::Okay>(cmd, timeoutMs));
}

Result<OsApi::ScannerSnapshotGet::Okay, std::string> requestScannerSnapshot(
    Network::WebSocketService& client, int timeoutMs)
{
    OsApi::ScannerSnapshotGet::Command cmd{};
    return unwrapResponse(
        client.sendCommandAndGetResponse<OsApi::ScannerSnapshotGet::Okay>(cmd, timeoutMs));
}

bool isStatusOk(const std::string& status)
{
    return status == "OK";
}

Result<std::monostate, std::string> waitForSystemStatusOk(
    Network::WebSocketService& client, int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const int pollDelayMs = 500;
    const int requestTimeoutMs = std::min(timeoutMs, 1000);
    const int waitTimeoutMs = std::max(timeoutMs, 15000);
    std::optional<OsApi::SystemStatus::Okay> lastStatus;
    std::string lastError;

    while (true) {
        auto statusResult = requestSystemStatus(client, requestTimeoutMs);
        if (statusResult.isError()) {
            lastError = statusResult.errorValue();
        }
        else {
            lastStatus = statusResult.value();
        }
        if (lastStatus.has_value()) {
            const auto& status = lastStatus.value();
            if (isStatusOk(status.ui_status) && isStatusOk(status.server_status)) {
                return Result<std::monostate, std::string>::okay(std::monostate{});
            }
            lastError = "SystemStatus not OK (ui_status=" + status.ui_status
                + ", server_status=" + status.server_status + ")";
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        if (elapsedMs >= waitTimeoutMs) {
            if (!lastError.empty()) {
                return Result<std::monostate, std::string>::error(lastError);
            }
            return Result<std::monostate, std::string>::error("SystemStatus check failed");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
    }
}

Result<std::monostate, std::string> restartServices(
    const std::string& osManagerAddress, int timeoutMs)
{
    Network::WebSocketService client;
    std::cerr << "Connecting to os-manager at " << osManagerAddress << "..." << std::endl;
    auto connectResult = client.connect(osManagerAddress, timeoutMs);
    if (connectResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to connect to os-manager: " + connectResult.errorValue());
    }

    std::cerr << "Restarting server..." << std::endl;
    OsApi::RestartServer::Command restartServerCmd{};
    auto restartServerResult = unwrapResponse(
        client.sendCommandAndGetResponse<std::monostate>(restartServerCmd, timeoutMs));
    if (restartServerResult.isError()) {
        client.disconnect();
        return Result<std::monostate, std::string>::error(
            "RestartServer failed: " + restartServerResult.errorValue());
    }

    std::cerr << "Restarting UI..." << std::endl;
    OsApi::RestartUi::Command restartUiCmd{};
    auto restartUiResult =
        unwrapResponse(client.sendCommandAndGetResponse<std::monostate>(restartUiCmd, timeoutMs));
    if (restartUiResult.isError()) {
        client.disconnect();
        return Result<std::monostate, std::string>::error(
            "RestartUi failed: " + restartUiResult.errorValue());
    }

    auto statusResult = waitForSystemStatusOk(client, timeoutMs);
    client.disconnect();
    if (statusResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "SystemStatus check failed: " + statusResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> stopUiService(
    const std::string& osManagerAddress, int timeoutMs)
{
    Network::WebSocketService client;
    auto connectResult = client.connect(osManagerAddress, timeoutMs);
    if (connectResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "Failed to connect to os-manager: " + connectResult.errorValue());
    }

    OsApi::StopUi::Command stopUiCmd{};
    auto stopUiResult =
        unwrapResponse(client.sendCommandAndGetResponse<std::monostate>(stopUiCmd, timeoutMs));
    client.disconnect();
    if (stopUiResult.isError()) {
        return Result<std::monostate, std::string>::error(
            "StopUi failed: " + stopUiResult.errorValue());
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
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
    if (failure_screenshot_path.has_value()) {
        output["failure_screenshot_path"] = *failure_screenshot_path;
    }
    if (success_screenshot_path.has_value()) {
        output["success_screenshot_path"] = *success_screenshot_path;
    }
    if (training_summary.has_value()) {
        output["training_summary"] = training_summary->toJson();
    }
    return output;
}

FunctionalTestSummary FunctionalTestRunner::runCanExit(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

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

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canExit",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanTrain(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<FunctionalTrainingSummary> trainingSummary;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        const auto trainingResult = runTrainingSession(uiAddress, serverAddress, timeoutMs, 1);
        if (trainingResult.isError()) {
            return Result<std::monostate, std::string>::error(trainingResult.errorValue());
        }
        trainingSummary = trainingResult.value();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canTrain",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::move(trainingSummary),
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanSearchHoldRight(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<std::string> successScreenshotPath;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        const auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        Network::WebSocketService uiClient;
        Network::WebSocketService serverClient;
        const auto connectResult =
            connectSearchClients(uiClient, serverClient, uiAddress, serverAddress, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, std::string>::error(connectResult.errorValue());
        }

        const auto warmPathResult =
            runDefaultSimulationAndReturnToStartMenu(uiClient, serverClient, timeoutMs);
        if (warmPathResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(warmPathResult.errorValue());
        }

        const auto searchResult = runSearchHoldRightSession(uiClient, serverClient, timeoutMs);
        if (searchResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(searchResult.errorValue());
        }

        const auto& plan = searchResult.value().plan;
        if (plan.summary.elapsedFrames == 0) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Saved plan reported elapsedFrames == 0");
        }
        if (plan.summary.bestFrontier == 0) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Saved plan reported bestFrontier == 0");
        }
        if (plan.frames.empty()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error("Saved plan had no frames");
        }
        for (size_t i = 0; i < plan.frames.size(); ++i) {
            const auto& frame = plan.frames[i];
            if (frame.xAxis != 127 || frame.yAxis != 0 || frame.buttons != 0) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Saved plan frame " + std::to_string(i) + " did not match hold-right");
            }
        }

        const auto screenshotResult = captureRequiredUiScreenshotPng(
            uiClient, "canSearchHoldRight", "search-idle", timeoutMs, successScreenshotPath);
        if (screenshotResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return screenshotResult;
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canSearchHoldRight",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .success_screenshot_path = successScreenshotPath,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanPlaybackPlan(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<std::string> successScreenshotPath;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        const auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        Network::WebSocketService uiClient;
        Network::WebSocketService serverClient;
        const auto connectResult =
            connectSearchClients(uiClient, serverClient, uiAddress, serverAddress, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, std::string>::error(connectResult.errorValue());
        }

        const auto searchResult = runSearchHoldRightSession(uiClient, serverClient, timeoutMs);
        if (searchResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(searchResult.errorValue());
        }

        const UUID planId = searchResult.value().plan.summary.id;

        uiClient.disconnect();
        serverClient.disconnect();

        const auto secondRestartResult = restartServices(osManagerAddress, timeoutMs);
        if (secondRestartResult.isError()) {
            return Result<std::monostate, std::string>::error(secondRestartResult.errorValue());
        }

        const auto reconnectResult =
            connectSearchClients(uiClient, serverClient, uiAddress, serverAddress, timeoutMs);
        if (reconnectResult.isError()) {
            return Result<std::monostate, std::string>::error(reconnectResult.errorValue());
        }

        const auto searchIdleResult = ensureSearchIdle(uiClient, serverClient, timeoutMs);
        if (searchIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(searchIdleResult.errorValue());
        }

        const auto browseResult = browseAndSelectPlan(uiClient, planId, timeoutMs);
        if (browseResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(browseResult.errorValue());
        }

        const auto screenshotResult = captureRequiredUiScreenshotPng(
            uiClient, "canPlaybackPlan", "plan-browser", timeoutMs, successScreenshotPath);
        if (screenshotResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return screenshotResult;
        }

        const auto playbackStartResult =
            startPlanPlaybackAndWaitActive(uiClient, serverClient, timeoutMs);
        if (playbackStartResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(playbackStartResult.errorValue());
        }

        const int playbackTimeoutMs = std::max(timeoutMs, 15000);
        const auto uiIdleResult = waitForUiState(uiClient, "SearchIdle", playbackTimeoutMs);
        if (uiIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiIdleResult.errorValue());
        }

        const auto serverIdleResult = waitForServerState(serverClient, "Idle", playbackTimeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canPlaybackPlan",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .success_screenshot_path = successScreenshotPath,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanStopPlaybackPlan(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<std::string> successScreenshotPath;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        const auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        Network::WebSocketService uiClient;
        Network::WebSocketService serverClient;
        const auto connectResult =
            connectSearchClients(uiClient, serverClient, uiAddress, serverAddress, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, std::string>::error(connectResult.errorValue());
        }

        const auto searchResult = runSearchHoldRightSession(uiClient, serverClient, timeoutMs);
        if (searchResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(searchResult.errorValue());
        }

        const UUID planId = searchResult.value().plan.summary.id;

        uiClient.disconnect();
        serverClient.disconnect();

        const auto secondRestartResult = restartServices(osManagerAddress, timeoutMs);
        if (secondRestartResult.isError()) {
            return Result<std::monostate, std::string>::error(secondRestartResult.errorValue());
        }

        const auto reconnectResult =
            connectSearchClients(uiClient, serverClient, uiAddress, serverAddress, timeoutMs);
        if (reconnectResult.isError()) {
            return Result<std::monostate, std::string>::error(reconnectResult.errorValue());
        }

        const auto searchIdleResult = ensureSearchIdle(uiClient, serverClient, timeoutMs);
        if (searchIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(searchIdleResult.errorValue());
        }

        const auto browseResult = browseAndSelectPlan(uiClient, planId, timeoutMs);
        if (browseResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(browseResult.errorValue());
        }

        const auto screenshotResult = captureRequiredUiScreenshotPng(
            uiClient, "canStopPlaybackPlan", "plan-browser", timeoutMs, successScreenshotPath);
        if (screenshotResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return screenshotResult;
        }

        const auto playbackStartResult =
            startPlanPlaybackAndWaitActive(uiClient, serverClient, timeoutMs);
        if (playbackStartResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(playbackStartResult.errorValue());
        }

        UiApi::IconSelect::Command stopIcon{
            .id = Ui::IconId::STOP,
        };
        const auto showIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showIconsResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(showIconsResult.errorValue());
        }
        const auto playbackStopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(stopIcon, timeoutMs));
        if (playbackStopResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(STOP) failed during playback: " + playbackStopResult.errorValue());
        }

        const int playbackTimeoutMs = std::max(timeoutMs, 10000);
        const auto uiIdleResult = waitForUiState(uiClient, "SearchIdle", playbackTimeoutMs);
        if (uiIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiIdleResult.errorValue());
        }

        const auto serverIdleResult = waitForServerState(serverClient, "Idle", playbackTimeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canStopPlaybackPlan",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .success_screenshot_path = successScreenshotPath,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanPauseSearch(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<std::string> successScreenshotPath;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        const auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        Network::WebSocketService uiClient;
        Network::WebSocketService serverClient;
        const auto connectResult =
            connectSearchClients(uiClient, serverClient, uiAddress, serverAddress, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, std::string>::error(connectResult.errorValue());
        }

        const auto searchIdleResult = ensureSearchIdle(uiClient, serverClient, timeoutMs);
        if (searchIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(searchIdleResult.errorValue());
        }

        const auto startResult = startSearchAndWaitActive(uiClient, serverClient, timeoutMs);
        if (startResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(startResult.errorValue());
        }

        const auto prePauseStart = std::chrono::steady_clock::now();
        bool reachedPrePauseProgress = false;
        while (std::chrono::steady_clock::now() - prePauseStart < std::chrono::seconds(5)) {
            const auto progressResult = requestSearchProgress(serverClient, timeoutMs);
            if (progressResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SearchProgressGet failed before pause: " + progressResult.errorValue());
            }

            if (progressResult.value().elapsedFrames >= 120) {
                reachedPrePauseProgress = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!reachedPrePauseProgress) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Search did not advance far enough before pause");
        }

        UiApi::IconSelect::Command pauseIcon{
            .id = Ui::IconId::PAUSE,
        };
        const auto showPauseIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showPauseIconsResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(showPauseIconsResult.errorValue());
        }
        const auto pauseResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(pauseIcon, timeoutMs));
        if (pauseResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(PAUSE) failed: " + pauseResult.errorValue());
        }

        auto pausedProgressResult = Result<Api::SearchProgress, std::string>::error("");
        const auto pauseWaitStart = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - pauseWaitStart < std::chrono::seconds(2)) {
            pausedProgressResult = requestSearchProgress(serverClient, timeoutMs);
            if (pausedProgressResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SearchProgressGet failed after pause: " + pausedProgressResult.errorValue());
            }

            if (pausedProgressResult.value().paused) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (pausedProgressResult.isError() || !pausedProgressResult.value().paused) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "SearchProgressGet reported paused=false after pause request");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        const auto pausedProgressAgainResult = requestSearchProgress(serverClient, timeoutMs);
        if (pausedProgressAgainResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Second SearchProgressGet failed while paused: "
                + pausedProgressAgainResult.errorValue());
        }
        if (pausedProgressAgainResult.value().elapsedFrames
            != pausedProgressResult.value().elapsedFrames) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "elapsedFrames advanced while paused");
        }

        const auto screenshotResult = captureRequiredUiScreenshotPng(
            uiClient, "canPauseSearch", "search-paused", timeoutMs, successScreenshotPath);
        if (screenshotResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return screenshotResult;
        }

        UiApi::IconSelect::Command playIcon{
            .id = Ui::IconId::PLAY,
        };
        const auto showPlayIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showPlayIconsResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(showPlayIconsResult.errorValue());
        }
        const auto resumeResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(playIcon, timeoutMs));
        if (resumeResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(PLAY) failed while resuming search: " + resumeResult.errorValue());
        }

        const auto resumeStart = std::chrono::steady_clock::now();
        bool progressedAfterResume = false;
        const uint64_t pausedElapsedFrames = pausedProgressAgainResult.value().elapsedFrames;
        while (std::chrono::steady_clock::now() - resumeStart < std::chrono::seconds(5)) {
            const auto progressResult = requestSearchProgress(serverClient, timeoutMs);
            if (progressResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SearchProgressGet failed after resume: " + progressResult.errorValue());
            }
            if (progressResult.value().elapsedFrames > pausedElapsedFrames) {
                progressedAfterResume = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (!progressedAfterResume) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Search did not resume advancing elapsedFrames");
        }

        UiApi::IconSelect::Command stopIcon{
            .id = Ui::IconId::STOP,
        };
        const auto showStopIconsResult = showUiIcons(uiClient, timeoutMs);
        if (showStopIconsResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(showStopIconsResult.errorValue());
        }
        const auto stopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(stopIcon, timeoutMs));
        if (stopResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect(STOP) failed while stopping search: " + stopResult.errorValue());
        }

        const int idleTimeoutMs = std::max(timeoutMs, 10000);
        const auto uiIdleResult = waitForUiState(uiClient, "SearchIdle", idleTimeoutMs);
        if (uiIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiIdleResult.errorValue());
        }

        const auto serverIdleResult = waitForServerState(serverClient, "Idle", idleTimeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canPauseSearch",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .success_screenshot_path = successScreenshotPath,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanTrainNesFlappy(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<FunctionalTrainingSummary> trainingSummary;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        UiApi::TrainingStart::Command trainCmd;
        trainCmd.evolution.populationSize = 2;
        trainCmd.evolution.maxParallelEvaluations = 4;
        trainCmd.evolution.maxGenerations = 1;
        trainCmd.evolution.maxSimulationTime = 0.1;
        trainCmd.training.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
        trainCmd.training.organismType = OrganismType::NES_DUCK;

        PopulationSpec population;
        population.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
        population.count = trainCmd.evolution.populationSize;
        population.randomCount = trainCmd.evolution.populationSize;
        trainCmd.training.population = { population };

        const auto trainingResult =
            runTrainingSession(uiAddress, serverAddress, timeoutMs, 1, trainCmd);
        if (trainingResult.isError()) {
            return Result<std::monostate, std::string>::error(trainingResult.errorValue());
        }

        const auto& summary = trainingResult.value();
        if (summary.scenario_id != Scenario::toString(Scenario::EnumType::NesFlappyParatroopa)) {
            return Result<std::monostate, std::string>::error(
                "Expected NES scenario summary, got " + summary.scenario_id);
        }
        if (summary.primary_brain_kind != TrainingBrainKind::DuckNeuralNetRecurrentV2) {
            return Result<std::monostate, std::string>::error(
                "Expected primary brain kind DuckNeuralNetRecurrentV2, got "
                + summary.primary_brain_kind);
        }
        if (summary.organism_type != static_cast<int>(OrganismType::NES_DUCK)) {
            return Result<std::monostate, std::string>::error(
                "Expected organism type NES_DUCK for NES training summary");
        }

        trainingSummary = summary;
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canTrainNesFlappy",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::move(trainingSummary),
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanSetGenerationsAndTrain(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    std::optional<FunctionalTrainingSummary> trainingSummary;
    const int requestedGenerations = 2;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        const auto trainingResult =
            runTrainingSession(uiAddress, serverAddress, timeoutMs, requestedGenerations);
        if (trainingResult.isError()) {
            return Result<std::monostate, std::string>::error(trainingResult.errorValue());
        }

        const auto& summary = trainingResult.value();
        if (summary.max_generations != requestedGenerations) {
            return Result<std::monostate, std::string>::error(
                "Expected max generations " + std::to_string(requestedGenerations) + ", got "
                + std::to_string(summary.max_generations));
        }
        if (summary.completed_generations != requestedGenerations) {
            return Result<std::monostate, std::string>::error(
                "Expected completed generations " + std::to_string(requestedGenerations) + ", got "
                + std::to_string(summary.completed_generations));
        }

        trainingSummary = summary;
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canSetGenerationsAndTrain",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::move(trainingSummary),
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanPlantTreeSeed(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

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
        if (!uiStatus.connected_to_server) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error("UI not connected to server");
        }

        auto uiStateResult = requestUiState(uiClient, timeoutMs);
        if (uiStateResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StateGet failed: " + uiStateResult.errorValue());
        }

        const std::string& uiState = uiStateResult.value().state;
        if (uiState != "StartMenu") {
            if (uiState == "SimRunning" || uiState == "Paused") {
                UiApi::SimStop::Command simStopCmd{};
                auto simStopResult =
                    unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(
                        simStopCmd, timeoutMs));
                if (simStopResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(
                        "UI SimStop failed: " + simStopResult.errorValue());
                }

                auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
                if (startMenuResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
                }
            }
            else {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Unsupported UI state for canPlantTreeSeed: " + uiState);
            }
        }

        serverClient.setProtocol(Network::Protocol::BINARY);
        Network::ClientHello hello{
            .protocolVersion = Network::kClientHelloProtocolVersion,
            .wantsRender = false,
            .wantsEvents = false,
        };
        serverClient.setClientHello(hello);

        std::cerr << "Connecting to server at " << serverAddress << "..." << std::endl;
        auto serverConnect = serverClient.connect(serverAddress, timeoutMs);
        if (serverConnect.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + serverConnect.errorValue());
        }

        UiApi::SimRun::Command simRunCmd{
            .scenario_id = Scenario::EnumType::TreeGermination,
        };
        auto simRunResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimRun::Okay>(simRunCmd, timeoutMs));
        if (simRunResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI SimRun failed: " + simRunResult.errorValue());
        }

        auto uiRunningResult = waitForUiState(uiClient, "SimRunning", timeoutMs);
        if (uiRunningResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiRunningResult.errorValue());
        }

        auto serverRunningResult = waitForServerState(serverClient, "SimRunning", timeoutMs);
        if (serverRunningResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverRunningResult.errorValue());
        }

        auto scenarioResult =
            waitForServerScenario(serverClient, Scenario::EnumType::TreeGermination, timeoutMs);
        if (scenarioResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(scenarioResult.errorValue());
        }

        Api::StateGet::Command stateCmd{};
        auto serverStateResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::StateGet::Okay>(stateCmd, timeoutMs));
        if (serverStateResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server StateGet failed: " + serverStateResult.errorValue());
        }

        const auto& worldData = serverStateResult.value().worldData;
        if (worldData.tree_vision.has_value()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Expected no tree_vision before planting seed");
        }

        auto targetResult = resolveSeedTarget(worldData);
        if (targetResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(targetResult.errorValue());
        }

        UiApi::PlantSeed::Command plantCmd{
            .x = targetResult.value().x,
            .y = targetResult.value().y,
        };
        auto plantResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::PlantSeed::OkayType>(plantCmd, timeoutMs));
        if (plantResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI PlantSeed failed: " + plantResult.errorValue());
        }

        auto treeVisionResult = waitForTreeVision(serverClient, timeoutMs);
        uiClient.disconnect();
        serverClient.disconnect();
        if (treeVisionResult.isError()) {
            return Result<std::monostate, std::string>::error(treeVisionResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canPlantTreeSeed",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanLoadGenomeFromBrowser(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    std::optional<GenomeId> expectedGenomeId;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

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
        if (!uiStatus.connected_to_server) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error("UI not connected to server");
        }

        auto uiStateResult = requestUiState(uiClient, timeoutMs);
        if (uiStateResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StateGet failed: " + uiStateResult.errorValue());
        }

        const std::string& uiState = uiStateResult.value().state;
        if (uiState != "StartMenu") {
            if (uiState == "SimRunning" || uiState == "Paused") {
                UiApi::SimStop::Command simStopCmd{};
                auto simStopResult =
                    unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(
                        simStopCmd, timeoutMs));
                if (simStopResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(
                        "UI SimStop failed: " + simStopResult.errorValue());
                }

                auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
                if (startMenuResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
                }
            }
            else {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Unsupported UI state for canLoadGenomeFromBrowser: " + uiState);
            }
        }

        UiApi::TrainingStart::Command trainCmd;
        trainCmd.evolution.populationSize = 2;
        trainCmd.evolution.maxGenerations = 1;
        trainCmd.evolution.maxSimulationTime = 0.1;
        trainCmd.training.scenarioId = Scenario::EnumType::TreeGermination;
        trainCmd.training.organismType = OrganismType::TREE;
        PopulationSpec population;
        population.brainKind = TrainingBrainKind::NeuralNet;
        population.count = trainCmd.evolution.populationSize;
        population.randomCount = trainCmd.evolution.populationSize;
        trainCmd.training.population = { population };

        auto trainResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::TrainingStart::Okay>(trainCmd, timeoutMs));
        if (trainResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI TrainingStart failed: " + trainResult.errorValue());
        }

        auto trainingStateResult =
            waitForUiStateAny(uiClient, { "TrainingActive", "TrainingUnsavedResult" }, timeoutMs);
        if (trainingStateResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(trainingStateResult.errorValue());
        }

        const int saveTimeoutMs = std::max(timeoutMs, 10000);
        auto saveResult = waitForUiTrainingResultSave(uiClient, saveTimeoutMs, 1);
        if (saveResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI TrainingResultSave failed: " + saveResult.errorValue());
        }

        if (saveResult.value().savedIds.empty()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI TrainingResultSave returned no saved ids");
        }
        expectedGenomeId = saveResult.value().savedIds.front();
        if (expectedGenomeId->isNil()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI TrainingResultSave returned nil genome_id");
        }

        UiApi::GenomeBrowserOpen::Command openCmd{};
        auto openResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::GenomeBrowserOpen::Okay>(openCmd, timeoutMs));
        if (openResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI GenomeBrowserOpen failed: " + openResult.errorValue());
        }

        UiApi::GenomeDetailOpen::Command detailCmd{ .id = expectedGenomeId };
        auto detailResult =
            unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::GenomeDetailOpen::Okay>(
                detailCmd, timeoutMs));
        if (detailResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI GenomeDetailOpen failed: " + detailResult.errorValue());
        }
        if (detailResult.value().id != expectedGenomeId.value()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI GenomeDetailOpen returned unexpected genome_id");
        }

        UiApi::GenomeDetailLoad::Command loadCmd{ .id = expectedGenomeId.value() };
        auto loadResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::GenomeDetailLoad::Okay>(loadCmd, timeoutMs));
        if (loadResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI GenomeDetailLoad failed: " + loadResult.errorValue());
        }

        const int runningTimeoutMs = std::max(timeoutMs, 10000);
        auto runningResult = waitForUiState(uiClient, "SimRunning", runningTimeoutMs);
        if (runningResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(runningResult.errorValue());
        }

        uiClient.disconnect();

        Network::WebSocketService serverClient;
        auto serverConnect = serverClient.connect(serverAddress, timeoutMs);
        if (serverConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + serverConnect.errorValue());
        }

        const int verifyTimeoutMs = std::max(timeoutMs, 10000);
        auto genomeResult =
            waitForGenomeInWorld(serverClient, expectedGenomeId.value(), verifyTimeoutMs);
        serverClient.disconnect();
        if (genomeResult.isError()) {
            return Result<std::monostate, std::string>::error(genomeResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canLoadGenomeFromBrowser",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanOpenTrainingConfigPanel(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    static_cast<void>(serverAddress);

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

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
        if (!uiStatus.connected_to_server) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error("UI not connected to server");
        }

        auto uiStateResult = requestUiState(uiClient, timeoutMs);
        if (uiStateResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StateGet failed: " + uiStateResult.errorValue());
        }

        const std::string& uiState = uiStateResult.value().state;
        if (uiState != "StartMenu" && !isTrainingStateName(uiState)) {
            if (uiState == "SimRunning" || uiState == "Paused") {
                UiApi::SimStop::Command simStopCmd{};
                auto simStopResult =
                    unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(
                        simStopCmd, timeoutMs));
                if (simStopResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(
                        "UI SimStop failed: " + simStopResult.errorValue());
                }

                auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
                if (startMenuResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
                }
            }
            else {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Unsupported UI state for canOpenTrainingConfigPanel: " + uiState);
            }
        }

        if (uiState != "StartMenu") {
            auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
            if (startMenuResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
            }
        }

        UiApi::IconSelect::Command startTrainCmd{ .id = Ui::IconId::EVOLUTION };
        auto startTrainResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(startTrainCmd, timeoutMs));
        if (startTrainResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect (EVOLUTION) failed: " + startTrainResult.errorValue());
        }
        if (!startTrainResult.value().selected) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect (EVOLUTION) did not select");
        }

        auto trainingStateResult = waitForUiStateAny(uiClient, { "TrainingIdle" }, timeoutMs);
        if (trainingStateResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(trainingStateResult.errorValue());
        }

        UiApi::IconSelect::Command configCmd{ .id = Ui::IconId::EVOLUTION };
        auto configResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(configCmd, timeoutMs));
        if (configResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect (EVOLUTION) failed in Training: " + configResult.errorValue());
        }
        if (!configResult.value().selected) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect (EVOLUTION) not selectable in Training");
        }

        auto waitForTrainingIcon =
            [&](Ui::IconId expectedIcon) -> Result<UiApi::StatusGet::Okay, std::string> {
            const auto start = std::chrono::steady_clock::now();
            const int pollDelayMs = 200;
            const int requestTimeoutMs = std::min(timeoutMs, 1000);

            while (true) {
                auto statusResult = requestUiStatus(uiClient, requestTimeoutMs);
                if (statusResult.isError()) {
                    return Result<UiApi::StatusGet::Okay, std::string>::error(
                        statusResult.errorValue());
                }
                if (statusResult.value().selected_icon == expectedIcon) {
                    return statusResult;
                }

                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - start)
                                           .count();
                if (elapsedMs >= timeoutMs) {
                    return Result<UiApi::StatusGet::Okay, std::string>::error(
                        "Timeout waiting for Training icon selection");
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(pollDelayMs));
            }
        };

        auto panelResult = waitForTrainingIcon(Ui::IconId::EVOLUTION);
        if (panelResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(panelResult.errorValue());
        }

        UiApi::IconSelect::Command browserCmd{ .id = Ui::IconId::GENOME_BROWSER };
        auto browserResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(browserCmd, timeoutMs));
        if (browserResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect (GENOME_BROWSER) failed: " + browserResult.errorValue());
        }
        if (!browserResult.value().selected) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI IconSelect (GENOME_BROWSER) did not select");
        }

        auto verifySwitch = requestUiState(uiClient, timeoutMs);
        if (verifySwitch.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StateGet failed after GENOME_BROWSER select: " + verifySwitch.errorValue());
        }

        uiClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canOpenTrainingConfigPanel",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanUpdateUserSettings(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        auto uiReadyResult = ensureUiInStartMenu(uiClient, timeoutMs);
        if (uiReadyResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(uiReadyResult.errorValue());
        }

        auto serverConnectResult =
            connectServerBinary(serverClient, serverAddress, timeoutMs, false);
        if (serverConnectResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        auto currentSettingsResult = fetchUserSettings(serverClient, timeoutMs);
        if (currentSettingsResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(currentSettingsResult.errorValue());
        }

        UserSettings expected = currentSettingsResult.value();
        expected.clockScenarioConfig.timezone = Config::ClockTimezone::Local;
        expected.volumePercent = 67;
        expected.defaultScenario = Scenario::EnumType::Clock;

        auto setResult = updateUserSettings(serverClient, expected, timeoutMs);
        if (setResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(setResult.errorValue());
        }

        const UserSettings& updated = setResult.value();
        if (updated.clockScenarioConfig.timezone != expected.clockScenarioConfig.timezone
            || updated.volumePercent != expected.volumePercent
            || updated.defaultScenario != expected.defaultScenario) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UserSettingsSet response does not match requested values");
        }

        auto verifyResult = fetchUserSettings(serverClient, timeoutMs);
        if (verifyResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(verifyResult.errorValue());
        }

        const UserSettings& verified = verifyResult.value();
        if (verified.clockScenarioConfig.timezone != expected.clockScenarioConfig.timezone
            || verified.volumePercent != expected.volumePercent
            || verified.defaultScenario != expected.defaultScenario) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UserSettingsGet did not reflect updated values");
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canUpdateUserSettings",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanResetUserSettings(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        auto uiReadyResult = ensureUiInStartMenu(uiClient, timeoutMs);
        if (uiReadyResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(uiReadyResult.errorValue());
        }

        auto serverConnectResult =
            connectServerBinary(serverClient, serverAddress, timeoutMs, false);
        if (serverConnectResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        UserSettings changedSettings{};
        changedSettings.clockScenarioConfig.timezone = Config::ClockTimezone::Local;
        changedSettings.volumePercent = 73;
        changedSettings.defaultScenario = Scenario::EnumType::Clock;

        auto setResult = updateUserSettings(serverClient, changedSettings, timeoutMs);
        if (setResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(setResult.errorValue());
        }

        auto resetResult = resetUserSettings(serverClient, timeoutMs);
        if (resetResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(resetResult.errorValue());
        }

        const UserSettings defaults{};
        const UserSettings& reset = resetResult.value();
        if (reset.clockScenarioConfig.timezone != defaults.clockScenarioConfig.timezone
            || reset.volumePercent != defaults.volumePercent
            || reset.defaultScenario != defaults.defaultScenario) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UserSettingsReset response did not return defaults");
        }

        auto verifyResult = fetchUserSettings(serverClient, timeoutMs);
        if (verifyResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(verifyResult.errorValue());
        }

        const UserSettings& verified = verifyResult.value();
        if (verified.clockScenarioConfig.timezone != defaults.clockScenarioConfig.timezone
            || verified.volumePercent != defaults.volumePercent
            || verified.defaultScenario != defaults.defaultScenario) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UserSettingsGet after reset did not return defaults");
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canResetUserSettings",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanPersistUserSettingsAcrossRestart(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        auto uiReadyResult = ensureUiInStartMenu(uiClient, timeoutMs);
        if (uiReadyResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(uiReadyResult.errorValue());
        }

        auto serverConnectResult =
            connectServerBinary(serverClient, serverAddress, timeoutMs, false);
        if (serverConnectResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        UserSettings expected{};
        expected.clockScenarioConfig.timezone = Config::ClockTimezone::Local;
        expected.volumePercent = 33;
        expected.defaultScenario = Scenario::EnumType::TreeGermination;

        auto setResult = updateUserSettings(serverClient, expected, timeoutMs);
        if (setResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(setResult.errorValue());
        }

        auto verifyBeforeRestart = fetchUserSettings(serverClient, timeoutMs);
        if (verifyBeforeRestart.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(verifyBeforeRestart.errorValue());
        }

        const UserSettings& beforeRestart = verifyBeforeRestart.value();
        if (beforeRestart.clockScenarioConfig.timezone != expected.clockScenarioConfig.timezone
            || beforeRestart.volumePercent != expected.volumePercent
            || beforeRestart.defaultScenario != expected.defaultScenario) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UserSettings before restart do not match expected values");
        }

        uiClient.disconnect();
        serverClient.disconnect();

        auto midRestart = restartServices(osManagerAddress, timeoutMs);
        if (midRestart.isError()) {
            return Result<std::monostate, std::string>::error(midRestart.errorValue());
        }

        auto reconnectServer = connectServerBinary(serverClient, serverAddress, timeoutMs, false);
        if (reconnectServer.isError()) {
            return Result<std::monostate, std::string>::error(reconnectServer.errorValue());
        }

        auto verifyAfterRestart = fetchUserSettings(serverClient, timeoutMs);
        if (verifyAfterRestart.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(verifyAfterRestart.errorValue());
        }

        const UserSettings& afterRestart = verifyAfterRestart.value();
        if (afterRestart.clockScenarioConfig.timezone != expected.clockScenarioConfig.timezone
            || afterRestart.volumePercent != expected.volumePercent
            || afterRestart.defaultScenario != expected.defaultScenario) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "User settings did not persist across restart");
        }

        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canPersistUserSettingsAcrossRestart",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanUseDefaultScenarioWhenSimRunHasNoScenario(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        auto uiReadyResult = ensureUiInStartMenu(uiClient, timeoutMs);
        if (uiReadyResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(uiReadyResult.errorValue());
        }

        auto serverConnectResult =
            connectServerBinary(serverClient, serverAddress, timeoutMs, false);
        if (serverConnectResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        auto currentSettingsResult = fetchUserSettings(serverClient, timeoutMs);
        if (currentSettingsResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(currentSettingsResult.errorValue());
        }

        UserSettings desired = currentSettingsResult.value();
        desired.defaultScenario = Scenario::EnumType::Clock;

        auto setResult = updateUserSettings(serverClient, desired, timeoutMs);
        if (setResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(setResult.errorValue());
        }

        UiApi::SimRun::Command simRunCmd{};
        auto simRunResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimRun::Okay>(simRunCmd, timeoutMs));
        if (simRunResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI SimRun (without scenario) failed: " + simRunResult.errorValue());
        }

        auto uiRunningResult = waitForUiState(uiClient, "SimRunning", timeoutMs);
        if (uiRunningResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiRunningResult.errorValue());
        }

        auto serverRunningResult = waitForServerState(serverClient, "SimRunning", timeoutMs);
        if (serverRunningResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverRunningResult.errorValue());
        }

        auto statusResult = requestServerStatus(serverClient, timeoutMs);
        if (statusResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server StatusGet failed: " + statusResult.errorValue());
        }

        if (!statusResult.value().scenario_id.has_value()
            || statusResult.value().scenario_id.value() != Scenario::EnumType::Clock) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server did not run user default scenario for SimRun without scenario_id");
        }

        UiApi::SimStop::Command simStopCmd{};
        auto stopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(simStopCmd, timeoutMs));
        if (stopResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI SimStop failed: " + stopResult.errorValue());
        }

        auto uiStartMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
        if (uiStartMenuResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiStartMenuResult.errorValue());
        }

        auto serverIdleAfterStop = waitForServerState(serverClient, "Idle", timeoutMs);
        if (serverIdleAfterStop.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleAfterStop.errorValue());
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canUseDefaultScenarioWhenSimRunHasNoScenario",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanControlNesScenario(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        auto uiReadyResult = ensureUiInStartMenu(uiClient, timeoutMs);
        if (uiReadyResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(uiReadyResult.errorValue());
        }

        auto serverConnectResult =
            connectServerBinary(serverClient, serverAddress, timeoutMs, false);
        if (serverConnectResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        UiApi::SimRun::Command simRunCmd{
            .scenario_id = Scenario::EnumType::NesFlappyParatroopa,
        };
        auto simRunResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimRun::Okay>(simRunCmd, timeoutMs));
        if (simRunResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI SimRun failed: " + simRunResult.errorValue());
        }

        auto uiRunningResult = waitForUiState(uiClient, "SimRunning", timeoutMs);
        if (uiRunningResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiRunningResult.errorValue());
        }

        auto serverRunningResult = waitForServerState(serverClient, "SimRunning", timeoutMs);
        if (serverRunningResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverRunningResult.errorValue());
        }

        auto scenarioResult =
            waitForServerScenario(serverClient, Scenario::EnumType::NesFlappyParatroopa, timeoutMs);
        if (scenarioResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(scenarioResult.errorValue());
        }

        auto baselineStatusResult = requestServerStatus(serverClient, timeoutMs);
        if (baselineStatusResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server StatusGet failed: " + baselineStatusResult.errorValue());
        }

        auto advanceBeforeInput = waitForServerTimestepAdvance(
            serverClient, baselineStatusResult.value().timestep, timeoutMs);
        if (advanceBeforeInput.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(advanceBeforeInput.errorValue());
        }

        constexpr uint8_t kNesButtonStart = 1u << 3;
        Api::NesInputSet::Command pressStartCmd{ .controller1_mask = kNesButtonStart };
        auto pressStartResult =
            unwrapResponse(serverClient.sendCommandAndGetResponse<Api::NesInputSet::OkayType>(
                pressStartCmd, timeoutMs));
        if (pressStartResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "NesInputSet press failed: " + pressStartResult.errorValue());
        }

        auto advanceAfterPress = waitForServerTimestepAdvance(
            serverClient, advanceBeforeInput.value().timestep, timeoutMs);
        if (advanceAfterPress.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(advanceAfterPress.errorValue());
        }

        Api::NesInputSet::Command releaseCmd{ .controller1_mask = 0 };
        auto releaseResult =
            unwrapResponse(serverClient.sendCommandAndGetResponse<Api::NesInputSet::OkayType>(
                releaseCmd, timeoutMs));
        if (releaseResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "NesInputSet release failed: " + releaseResult.errorValue());
        }

        auto advanceAfterRelease = waitForServerTimestepAdvance(
            serverClient, advanceAfterPress.value().timestep, timeoutMs);
        if (advanceAfterRelease.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(advanceAfterRelease.errorValue());
        }

        if (!advanceAfterRelease.value().scenario_id.has_value()
            || advanceAfterRelease.value().scenario_id.value()
                != Scenario::EnumType::NesFlappyParatroopa) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server scenario changed unexpectedly while controlling NES");
        }

        UiApi::SimStop::Command simStopCmd{};
        auto stopResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(simStopCmd, timeoutMs));
        if (stopResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI SimStop failed: " + stopResult.errorValue());
        }

        auto uiStartMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
        if (uiStartMenuResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(uiStartMenuResult.errorValue());
        }

        auto serverIdleAfterStop = waitForServerState(serverClient, "Idle", timeoutMs);
        if (serverIdleAfterStop.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleAfterStop.errorValue());
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canControlNesScenario",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanApplyClockTimezoneFromUserSettings(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService serverClient;
    static_cast<void>(uiAddress);

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        constexpr Config::ClockTimezone expectedTimezone = Config::ClockTimezone::UTC;

        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto stopUiResult = stopUiService(osManagerAddress, timeoutMs);
        if (stopUiResult.isError()) {
            return Result<std::monostate, std::string>::error(stopUiResult.errorValue());
        }

        auto serverConnectResult =
            connectServerBinary(serverClient, serverAddress, timeoutMs, true);
        if (serverConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleResult.errorValue());
        }

        Api::RenderFormatSet::Command renderCmd{
            .format = RenderFormat::EnumType::Basic,
            .connectionId = "",
        };
        auto renderResult =
            unwrapResponse(serverClient.sendCommandAndGetResponse<Api::RenderFormatSet::Okay>(
                renderCmd, timeoutMs));
        if (renderResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "RenderFormatSet failed: " + renderResult.errorValue());
        }

        auto currentSettingsResult = fetchUserSettings(serverClient, timeoutMs);
        if (currentSettingsResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(currentSettingsResult.errorValue());
        }

        UserSettings desired = currentSettingsResult.value();
        desired.clockScenarioConfig.timezone = expectedTimezone;

        auto setResult = updateUserSettings(serverClient, desired, timeoutMs);
        if (setResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(setResult.errorValue());
        }

        Api::SimRun::Command simRunCmd{
            .scenario_id = Scenario::EnumType::Clock,
            .container_size = Vector2s(800, 480),
        };
        auto simRunResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::SimRun::Okay>(simRunCmd, timeoutMs));
        if (simRunResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server SimRun (Clock) failed: " + simRunResult.errorValue());
        }

        auto serverRunningResult = waitForServerState(serverClient, "SimRunning", timeoutMs);
        if (serverRunningResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverRunningResult.errorValue());
        }

        auto timezoneResult = waitForClockRenderTimezone(
            serverClient, desired.clockScenarioConfig.timezone, std::max(timeoutMs, 10000));
        if (timezoneResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(timezoneResult.errorValue());
        }

        Api::SimStop::Command simStopCmd{};
        auto stopResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::SimStop::OkayType>(simStopCmd, timeoutMs));
        if (stopResult.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server SimStop failed: " + stopResult.errorValue());
        }

        auto serverIdleAfterStop = waitForServerState(serverClient, "Idle", timeoutMs);
        if (serverIdleAfterStop.isError()) {
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(serverIdleAfterStop.errorValue());
        }

        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canApplyClockTimezoneFromUserSettings",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanCancelWifiConnect(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canCancelWifiConnect",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canCancelWifiConnect",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canCancelWifiConnect requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canCancelWifiConnect",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canCancelWifiConnect requires a baseline network and a distinct cancel target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    Network::WebSocketService serverClient;
    Network::WebSocketService uiClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        auto uiConnectResult = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnectResult.errorValue());
        }

        auto serverConnectResult = serverClient.connect(serverAddress, timeoutMs);
        if (serverConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            return serverIdleResult;
        }

        auto uiNetworkResult = ensureUiInNetworkScreen(uiClient, timeoutMs);
        if (uiNetworkResult.isError()) {
            return uiNetworkResult;
        }

        auto uiTargetsResult = waitForUiTargetNetworksVisible(uiClient, config, timeoutMs);
        if (uiTargetsResult.isError()) {
            return uiTargetsResult;
        }

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaUi(
            uiClient, osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto uiUnsavedCancelTargetResult =
            waitForUiUnsavedNetwork(uiClient, cancelTarget->ssid, timeoutMs);
        if (uiUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                uiUnsavedCancelTargetResult.errorValue());
        }

        auto beginConnectResult =
            beginWifiConnectViaUi(uiClient, cancelTarget->ssid, cancelTarget->password, timeoutMs);
        if (beginConnectResult.isError()) {
            return beginConnectResult;
        }

        auto osCancelableResult =
            waitForOsCancelableConnect(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(osCancelableResult.errorValue());
        }

        auto uiCancelableResult =
            waitForUiCancelableConnect(uiClient, cancelTarget->ssid, timeoutMs);
        if (uiCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(uiCancelableResult.errorValue());
        }

        auto cancelPressResult = pressUiNetworkConnectCancel(uiClient, timeoutMs);
        if (cancelPressResult.isError()) {
            return cancelPressResult;
        }

        auto canceledResult =
            waitForOsCanceledConnect(osManagerClient, cancelTarget->ssid, wifiTimeoutMs);
        if (canceledResult.isError()) {
            return Result<std::monostate, std::string>::error(canceledResult.errorValue());
        }

        auto uiPostCancelResult = waitForUiNetworkDiagnostics(
            uiClient,
            wifiTimeoutMs,
            "UI post-cancel network state",
            [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
                const bool expectsPasswordPrompt = cancelTarget->password.has_value();
                if (expectsPasswordPrompt && diagnostics.screen == "WifiPassword"
                    && diagnostics.password_prompt_visible
                    && diagnostics.password_prompt_target_ssid.has_value()
                    && diagnostics.password_prompt_target_ssid.value() == cancelTarget->ssid
                    && !diagnostics.connect_progress.has_value()
                    && !diagnostics.connect_overlay_visible) {
                    lastError.clear();
                    return true;
                }

                if (!expectsPasswordPrompt && diagnostics.screen == "Wifi"
                    && !diagnostics.connected_ssid.has_value()
                    && !diagnostics.connect_overlay_visible
                    && !diagnostics.connect_progress.has_value()) {
                    lastError.clear();
                    return true;
                }

                lastError.clear();
                return false;
            });
        if (uiPostCancelResult.isError()) {
            return Result<std::monostate, std::string>::error(uiPostCancelResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    uiClient.disconnect();
    serverClient.disconnect();
    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canCancelWifiConnect",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanCancelWifiConnectBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canCancelWifiConnectBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canCancelWifiConnectBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canCancelWifiConnectBackendOnly requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canCancelWifiConnectBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canCancelWifiConnectBackendOnly requires a baseline network and a distinct "
                "cancel target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto osUnsavedCancelTargetResult =
            waitForOsUnsavedNetwork(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedCancelTargetResult.errorValue());
        }

        auto beginConnectResult = beginWifiConnectViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, timeoutMs);
        if (beginConnectResult.isError()) {
            return beginConnectResult;
        }

        auto osCancelableResult =
            waitForOsCancelableConnect(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(osCancelableResult.errorValue());
        }

        auto cancelResult = cancelWifiConnectViaBackend(osManagerClient, timeoutMs);
        if (cancelResult.isError()) {
            return cancelResult;
        }

        auto canceledResult =
            waitForOsCanceledConnect(osManagerClient, cancelTarget->ssid, wifiTimeoutMs);
        if (canceledResult.isError()) {
            return Result<std::monostate, std::string>::error(canceledResult.errorValue());
        }

        auto cancelTargetConnectResult = connectToWifiViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, wifiTimeoutMs);
        if (cancelTargetConnectResult.isError()) {
            return cancelTargetConnectResult;
        }

        auto reconnectBaselineResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canCancelWifiConnectBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanCancelThenScannerBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canCancelThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canCancelThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canCancelThenScannerBackendOnly requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canCancelThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canCancelThenScannerBackendOnly requires a baseline network and a distinct "
                "cancel target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto initialSystemStatus = requestSystemStatus(osManagerClient, timeoutMs);
        if (initialSystemStatus.isError()) {
            return Result<std::monostate, std::string>::error(
                "SystemStatus failed: " + initialSystemStatus.errorValue());
        }
        if (!initialSystemStatus.value().scanner_mode_available) {
            return Result<std::monostate, std::string>::error(
                "Scanner mode unavailable: " + initialSystemStatus.value().scanner_mode_detail);
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto osUnsavedCancelTargetResult =
            waitForOsUnsavedNetwork(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedCancelTargetResult.errorValue());
        }

        auto beginConnectResult = beginWifiConnectViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, timeoutMs);
        if (beginConnectResult.isError()) {
            return beginConnectResult;
        }

        auto osCancelableResult =
            waitForOsCancelableConnect(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(osCancelableResult.errorValue());
        }

        auto cancelResult = cancelWifiConnectViaBackend(osManagerClient, timeoutMs);
        if (cancelResult.isError()) {
            return cancelResult;
        }

        auto canceledResult =
            waitForOsCanceledConnect(osManagerClient, cancelTarget->ssid, wifiTimeoutMs);
        if (canceledResult.isError()) {
            return Result<std::monostate, std::string>::error(canceledResult.errorValue());
        }

        auto reconnectBaselineResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        auto scannerEnterResult =
            enterScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult =
            exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto restoredBaselineResult =
            waitForOsWifiConnectedSsid(osManagerClient, baselineIt->ssid, wifiTimeoutMs);
        if (restoredBaselineResult.isError()) {
            return Result<std::monostate, std::string>::error(restoredBaselineResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canCancelThenScannerBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanPlaySynthKeys(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    static_cast<void>(serverAddress);

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        std::cerr << "Connecting to UI at " << uiAddress << "..." << std::endl;
        auto uiConnect = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnect.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnect.errorValue());
        }

        auto uiStateResult = requestUiState(uiClient, timeoutMs);
        if (uiStateResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI StateGet failed: " + uiStateResult.errorValue());
        }

        const std::string& uiState = uiStateResult.value().state;
        if (uiState != "StartMenu" && uiState != "Synth") {
            if (uiState == "SimRunning" || uiState == "Paused") {
                UiApi::SimStop::Command simStopCmd{};
                auto simStopResult =
                    unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(
                        simStopCmd, timeoutMs));
                if (simStopResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(
                        "UI SimStop failed: " + simStopResult.errorValue());
                }

                auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
                if (startMenuResult.isError()) {
                    uiClient.disconnect();
                    return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
                }
            }
            else {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Unsupported UI state for canPlaySynthKeys: " + uiState);
            }
        }

        if (uiState != "Synth") {
            UiApi::IconSelect::Command synthCmd{ .id = Ui::IconId::MUSIC };
            auto synthResult = unwrapResponse(
                uiClient.sendCommandAndGetResponse<UiApi::IconSelect::Okay>(synthCmd, timeoutMs));
            if (synthResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI IconSelect (MUSIC) failed: " + synthResult.errorValue());
            }
            if (!synthResult.value().selected) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI IconSelect (MUSIC) did not select");
            }

            auto synthStateResult = waitForUiState(uiClient, "Synth", timeoutMs);
            if (synthStateResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(synthStateResult.errorValue());
            }
        }

        struct KeyPress {
            int index = 0;
            bool isBlack = false;
        };
        const std::vector<KeyPress> presses = {
            { 0, false }, { 0, true },  { 2, false }, { 2, true },   { 4, false }, { 4, true },
            { 6, false }, { 7, false }, { 5, true },  { 13, false }, { 9, true },
        };

        for (const auto& press : presses) {
            UiApi::SynthKeyEvent::Command pressCmd{
                .key_index = press.index,
                .is_black = press.isBlack,
                .is_pressed = true,
            };
            auto pressResult =
                unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SynthKeyEvent::Okay>(
                    pressCmd, timeoutMs));
            if (pressResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI SynthKeyEvent failed: " + pressResult.errorValue());
            }

            const auto& response = pressResult.value();
            if (response.key_index != press.index || response.is_black != press.isBlack
                || !response.is_pressed) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SynthKeyEvent response mismatch");
            }

            auto statusResult = requestUiStatus(uiClient, timeoutMs);
            if (statusResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI StatusGet failed: " + statusResult.errorValue());
            }

            const auto& status = statusResult.value();
            const auto* synthDetails =
                std::get_if<UiApi::StatusGet::SynthStateDetails>(&status.state_details);
            if (!synthDetails) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Expected SynthStateDetails in StatusGet");
            }
            if (synthDetails->last_key_index != press.index
                || synthDetails->last_key_is_black != press.isBlack) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SynthStateDetails did not update after key press");
            }

            UiApi::SynthKeyEvent::Command releaseCmd{
                .key_index = press.index,
                .is_black = press.isBlack,
                .is_pressed = false,
            };
            auto releaseResult =
                unwrapResponse(uiClient.sendCommandAndGetResponse<UiApi::SynthKeyEvent::Okay>(
                    releaseCmd, timeoutMs));
            if (releaseResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI SynthKeyEvent release failed: " + releaseResult.errorValue());
            }

            const auto& releaseResponse = releaseResult.value();
            if (releaseResponse.key_index != press.index
                || releaseResponse.is_black != press.isBlack || releaseResponse.is_pressed) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SynthKeyEvent release response mismatch");
            }

            auto statusAfterReleaseResult = requestUiStatus(uiClient, timeoutMs);
            if (statusAfterReleaseResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI StatusGet failed after release: " + statusAfterReleaseResult.errorValue());
            }

            const auto& releasedStatus = statusAfterReleaseResult.value();
            const auto* releasedSynthDetails =
                std::get_if<UiApi::StatusGet::SynthStateDetails>(&releasedStatus.state_details);
            if (!releasedSynthDetails) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Expected SynthStateDetails in StatusGet after release");
            }
            if (releasedSynthDetails->last_key_index != -1
                || releasedSynthDetails->last_key_is_black) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "SynthStateDetails did not clear after key release");
            }
        }

        UiApi::SynthKeyEvent::Command invalidCmd{
            .key_index = 99,
            .is_black = false,
            .is_pressed = true,
        };
        auto invalidResult =
            uiClient.sendCommandAndGetResponse<UiApi::SynthKeyEvent::Okay>(invalidCmd, timeoutMs);
        if (invalidResult.isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI SynthKeyEvent request failed: " + invalidResult.errorValue());
        }
        if (!invalidResult.value().isError()) {
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Expected SynthKeyEvent error for invalid key index");
        }

        uiClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canPlaySynthKeys",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanSwitchWifiNetworks(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canSwitchWifiNetworks",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canSwitchWifiNetworks",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchWifiNetworks requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    Network::WebSocketService serverClient;
    Network::WebSocketService uiClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        auto uiConnectResult = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnectResult.errorValue());
        }

        auto serverConnectResult = serverClient.connect(serverAddress, timeoutMs);
        if (serverConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            return serverIdleResult;
        }

        auto uiNetworkResult = ensureUiInNetworkScreen(uiClient, timeoutMs);
        if (uiNetworkResult.isError()) {
            return uiNetworkResult;
        }

        auto uiTargetsResult = waitForUiTargetNetworksVisible(uiClient, config, timeoutMs);
        if (uiTargetsResult.isError()) {
            return uiTargetsResult;
        }

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        for (const auto& network : config.networks) {
            auto connectResult = connectToWifiViaUi(
                uiClient, osManagerClient, network.ssid, network.password, wifiTimeoutMs);
            if (connectResult.isError()) {
                return connectResult;
            }
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    uiClient.disconnect();
    serverClient.disconnect();
    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canSwitchWifiNetworks",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanSwitchWifiNetworksBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canSwitchWifiNetworksBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canSwitchWifiNetworksBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchWifiNetworksBackendOnly requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* switchTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!switchTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canSwitchWifiNetworksBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchWifiNetworksBackendOnly requires a baseline network and a distinct "
                "switch target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetSwitchTargetResult =
            forgetWifiViaOsManager(osManagerClient, switchTarget->ssid, timeoutMs);
        if (forgetSwitchTargetResult.isError()) {
            return forgetSwitchTargetResult;
        }

        auto osUnsavedSwitchTargetResult =
            waitForOsUnsavedNetwork(osManagerClient, switchTarget->ssid, timeoutMs);
        if (osUnsavedSwitchTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedSwitchTargetResult.errorValue());
        }

        auto connectSwitchTargetResult = connectToWifiViaBackend(
            osManagerClient, switchTarget->ssid, switchTarget->password, wifiTimeoutMs);
        if (connectSwitchTargetResult.isError()) {
            return connectSwitchTargetResult;
        }

        auto reconnectBaselineResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canSwitchWifiNetworksBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanExerciseWifiAndScanner(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canExerciseWifiAndScanner",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canExerciseWifiAndScanner",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canExerciseWifiAndScanner requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canExerciseWifiAndScanner",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canExerciseWifiAndScanner requires a baseline network and a distinct cancel "
                "target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    Network::WebSocketService serverClient;
    Network::WebSocketService uiClient;
    std::optional<std::string> originalConnectedSsid;
    std::optional<std::string> successScreenshotPath;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto initialSystemStatus = requestSystemStatus(osManagerClient, timeoutMs);
        if (initialSystemStatus.isError()) {
            return Result<std::monostate, std::string>::error(
                "SystemStatus failed: " + initialSystemStatus.errorValue());
        }
        if (!initialSystemStatus.value().scanner_mode_available) {
            return Result<std::monostate, std::string>::error(
                "Scanner mode unavailable: " + initialSystemStatus.value().scanner_mode_detail);
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        auto uiConnectResult = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnectResult.errorValue());
        }

        auto serverConnectResult = serverClient.connect(serverAddress, timeoutMs);
        if (serverConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + serverConnectResult.errorValue());
        }

        auto serverIdleResult = ensureServerIdle(serverClient, timeoutMs);
        if (serverIdleResult.isError()) {
            return serverIdleResult;
        }

        auto uiNetworkResult = ensureUiInNetworkScreen(uiClient, timeoutMs);
        if (uiNetworkResult.isError()) {
            return uiNetworkResult;
        }

        auto uiTargetsResult = waitForUiTargetNetworksVisible(uiClient, config, timeoutMs);
        if (uiTargetsResult.isError()) {
            return uiTargetsResult;
        }

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaUi(
            uiClient, osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto uiUnsavedCancelTargetResult =
            waitForUiUnsavedNetwork(uiClient, cancelTarget->ssid, timeoutMs);
        if (uiUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                uiUnsavedCancelTargetResult.errorValue());
        }

        auto beginConnectResult =
            beginWifiConnectViaUi(uiClient, cancelTarget->ssid, cancelTarget->password, timeoutMs);
        if (beginConnectResult.isError()) {
            return beginConnectResult;
        }

        auto osCancelableResult =
            waitForOsCancelableConnect(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(osCancelableResult.errorValue());
        }

        auto uiCancelableResult =
            waitForUiCancelableConnect(uiClient, cancelTarget->ssid, timeoutMs);
        if (uiCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(uiCancelableResult.errorValue());
        }

        auto cancelPressResult = pressUiNetworkConnectCancel(uiClient, timeoutMs);
        if (cancelPressResult.isError()) {
            return cancelPressResult;
        }

        auto canceledResult =
            waitForOsCanceledConnect(osManagerClient, cancelTarget->ssid, wifiTimeoutMs);
        if (canceledResult.isError()) {
            return Result<std::monostate, std::string>::error(canceledResult.errorValue());
        }

        auto uiPostCancelResult = waitForUiNetworkDiagnostics(
            uiClient,
            wifiTimeoutMs,
            "UI post-cancel network state",
            [&](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
                const bool expectsPasswordPrompt = cancelTarget->password.has_value();
                if (expectsPasswordPrompt && diagnostics.screen == "WifiPassword"
                    && diagnostics.password_prompt_visible
                    && diagnostics.password_prompt_target_ssid.has_value()
                    && diagnostics.password_prompt_target_ssid.value() == cancelTarget->ssid
                    && !diagnostics.connect_progress.has_value()
                    && !diagnostics.connect_overlay_visible) {
                    lastError.clear();
                    return true;
                }

                if (!expectsPasswordPrompt && diagnostics.screen == "Wifi"
                    && !diagnostics.connected_ssid.has_value()
                    && !diagnostics.connect_overlay_visible
                    && !diagnostics.connect_progress.has_value()) {
                    lastError.clear();
                    return true;
                }

                lastError.clear();
                return false;
            });
        if (uiPostCancelResult.isError()) {
            return Result<std::monostate, std::string>::error(uiPostCancelResult.errorValue());
        }

        auto cancelTargetConnectResult = connectToWifiViaUi(
            uiClient, osManagerClient, cancelTarget->ssid, cancelTarget->password, wifiTimeoutMs);
        if (cancelTargetConnectResult.isError()) {
            return cancelTargetConnectResult;
        }

        auto reconnectBaselineResult = connectToWifiViaUi(
            uiClient, osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        auto scannerScreenResult = ensureUiInNetworkScannerScreen(uiClient, timeoutMs);
        if (scannerScreenResult.isError()) {
            return scannerScreenResult;
        }

        auto scannerReadyResult = waitForUiNetworkDiagnostics(
            uiClient,
            timeoutMs,
            "UI scanner ready state",
            [](const UiApi::NetworkDiagnosticsGet::Okay& diagnostics, std::string& lastError) {
                if (diagnostics.screen == "Scanner" && diagnostics.scanner_mode_available
                    && diagnostics.scanner_enter_enabled) {
                    lastError.clear();
                    return true;
                }

                lastError.clear();
                return false;
            });
        if (scannerReadyResult.isError()) {
            return Result<std::monostate, std::string>::error(scannerReadyResult.errorValue());
        }

        auto scannerScreenshotResult = captureUiScreenshotPng(
            uiClient, "canExerciseWifiAndScanner", "scanner-ready", timeoutMs);
        if (scannerScreenshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                scannerScreenshotResult.errorValue().message);
        }
        successScreenshotPath = scannerScreenshotResult.value();
        std::cerr << "Saved scanner screenshot to " << *successScreenshotPath << std::endl;

        auto scannerEnterResult = pressUiScannerEnter(uiClient, timeoutMs);
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto uiScannerActiveResult =
            waitForUiScannerMode(uiClient, true, std::max(timeoutMs, 20000));
        if (uiScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(uiScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult = pressUiScannerExit(uiClient, timeoutMs);
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto uiScannerInactiveResult =
            waitForUiScannerMode(uiClient, false, std::max(timeoutMs, 60000));
        if (uiScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(uiScannerInactiveResult.errorValue());
        }

        auto restoredBaselineResult =
            waitForOsWifiConnectedSsid(osManagerClient, baselineIt->ssid, wifiTimeoutMs);
        if (restoredBaselineResult.isError()) {
            return Result<std::monostate, std::string>::error(restoredBaselineResult.errorValue());
        }

        auto wifiScreenResult = ensureUiInNetworkScreen(uiClient, timeoutMs);
        if (wifiScreenResult.isError()) {
            return wifiScreenResult;
        }

        auto uiTargetsAfterScannerResult =
            waitForUiTargetNetworksVisible(uiClient, config, wifiTimeoutMs);
        if (uiTargetsAfterScannerResult.isError()) {
            return uiTargetsAfterScannerResult;
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    uiClient.disconnect();
    serverClient.disconnect();
    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canExerciseWifiAndScanner",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .success_screenshot_path = successScreenshotPath,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanRecoverScannerUiAfterOutOfBandExit(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    static_cast<void>(serverAddress);

    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService osManagerClient;
    Network::WebSocketService uiClient;
    std::optional<std::string> successScreenshotPath;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto scannerInactiveResult = ensureScannerModeInactive(osManagerClient, timeoutMs);
        if (scannerInactiveResult.isError()) {
            return scannerInactiveResult;
        }

        auto uiConnectResult = uiClient.connect(uiAddress, timeoutMs);
        if (uiConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to UI: " + uiConnectResult.errorValue());
        }

        auto scannerScreenResult = ensureUiInNetworkScannerScreen(uiClient, timeoutMs);
        if (scannerScreenResult.isError()) {
            return scannerScreenResult;
        }

        auto scannerReadyResult = waitForUiScannerMode(uiClient, false, std::max(timeoutMs, 20000));
        if (scannerReadyResult.isError()) {
            return Result<std::monostate, std::string>::error(scannerReadyResult.errorValue());
        }

        auto scannerEnterResult = pressUiScannerEnter(uiClient, timeoutMs);
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto uiScannerActiveResult =
            waitForUiScannerMode(uiClient, true, std::max(timeoutMs, 20000));
        if (uiScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(uiScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult =
            exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto uiScannerRecoveredResult =
            waitForUiScannerMode(uiClient, false, std::max(timeoutMs, 60000));
        if (uiScannerRecoveredResult.isError()) {
            return Result<std::monostate, std::string>::error(
                uiScannerRecoveredResult.errorValue());
        }

        auto screenshotResult = captureUiScreenshotPng(
            uiClient, "canRecoverScannerUiAfterOutOfBandExit", "scanner-recovered", timeoutMs);
        if (screenshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                screenshotResult.errorValue().message);
        }
        successScreenshotPath = screenshotResult.value();
        std::cerr << "Saved scanner recovery screenshot to " << *successScreenshotPath << std::endl;

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = ensureScannerModeInactive(osManagerClient, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    uiClient.disconnect();
    osManagerClient.disconnect();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canRecoverScannerUiAfterOutOfBandExit",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .success_screenshot_path = successScreenshotPath,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanExerciseWifiAndScannerBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canExerciseWifiAndScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canExerciseWifiAndScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canExerciseWifiAndScannerBackendOnly requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canExerciseWifiAndScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canExerciseWifiAndScannerBackendOnly requires a baseline network and a distinct "
                "cancel target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto initialSystemStatus = requestSystemStatus(osManagerClient, timeoutMs);
        if (initialSystemStatus.isError()) {
            return Result<std::monostate, std::string>::error(
                "SystemStatus failed: " + initialSystemStatus.errorValue());
        }
        if (!initialSystemStatus.value().scanner_mode_available) {
            return Result<std::monostate, std::string>::error(
                "Scanner mode unavailable: " + initialSystemStatus.value().scanner_mode_detail);
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto osUnsavedCancelTargetResult =
            waitForOsUnsavedNetwork(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedCancelTargetResult.errorValue());
        }

        auto beginConnectResult = beginWifiConnectViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, timeoutMs);
        if (beginConnectResult.isError()) {
            return beginConnectResult;
        }

        auto osCancelableResult =
            waitForOsCancelableConnect(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osCancelableResult.isError()) {
            return Result<std::monostate, std::string>::error(osCancelableResult.errorValue());
        }

        auto cancelResult = cancelWifiConnectViaBackend(osManagerClient, timeoutMs);
        if (cancelResult.isError()) {
            return cancelResult;
        }

        auto canceledResult =
            waitForOsCanceledConnect(osManagerClient, cancelTarget->ssid, wifiTimeoutMs);
        if (canceledResult.isError()) {
            return Result<std::monostate, std::string>::error(canceledResult.errorValue());
        }

        auto cancelTargetConnectResult = connectToWifiViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, wifiTimeoutMs);
        if (cancelTargetConnectResult.isError()) {
            return cancelTargetConnectResult;
        }

        auto reconnectBaselineResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        auto scannerEnterResult =
            enterScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult =
            exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto restoredBaselineResult =
            waitForOsWifiConnectedSsid(osManagerClient, baselineIt->ssid, wifiTimeoutMs);
        if (restoredBaselineResult.isError()) {
            return Result<std::monostate, std::string>::error(restoredBaselineResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canExerciseWifiAndScannerBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanExerciseScannerModeBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canExerciseScannerModeBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.empty()) {
        return FunctionalTestSummary{
            .name = "canExerciseScannerModeBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canExerciseScannerModeBackendOnly requires at least one configured network"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalNetworkConfig& baseline = config.networks.front();
    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto initialSystemStatus = requestSystemStatus(osManagerClient, timeoutMs);
        if (initialSystemStatus.isError()) {
            return Result<std::monostate, std::string>::error(
                "SystemStatus failed: " + initialSystemStatus.errorValue());
        }
        if (!initialSystemStatus.value().scanner_mode_available) {
            return Result<std::monostate, std::string>::error(
                "Scanner mode unavailable: " + initialSystemStatus.value().scanner_mode_detail);
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baseline.ssid, baseline.password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto scannerEnterResult =
            enterScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult =
            exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto restoredBaselineResult =
            waitForOsWifiConnectedSsid(osManagerClient, baseline.ssid, wifiTimeoutMs);
        if (restoredBaselineResult.isError()) {
            return Result<std::monostate, std::string>::error(restoredBaselineResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canExerciseScannerModeBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanSwitchForgetThenScannerBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canSwitchForgetThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canSwitchForgetThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchForgetThenScannerBackendOnly requires at least two configured "
                "networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canSwitchForgetThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchForgetThenScannerBackendOnly requires a baseline network and a "
                "distinct switch target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto initialSystemStatus = requestSystemStatus(osManagerClient, timeoutMs);
        if (initialSystemStatus.isError()) {
            return Result<std::monostate, std::string>::error(
                "SystemStatus failed: " + initialSystemStatus.errorValue());
        }
        if (!initialSystemStatus.value().scanner_mode_available) {
            return Result<std::monostate, std::string>::error(
                "Scanner mode unavailable: " + initialSystemStatus.value().scanner_mode_detail);
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto osUnsavedCancelTargetResult =
            waitForOsUnsavedNetwork(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedCancelTargetResult.errorValue());
        }

        auto connectSwitchTargetResult = connectToWifiViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, wifiTimeoutMs);
        if (connectSwitchTargetResult.isError()) {
            return connectSwitchTargetResult;
        }

        auto reconnectBaselineResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        auto forgetBeforeScannerResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetBeforeScannerResult.isError()) {
            return forgetBeforeScannerResult;
        }

        auto osUnsavedBeforeScannerResult =
            waitForOsUnsavedNetwork(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osUnsavedBeforeScannerResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedBeforeScannerResult.errorValue());
        }

        auto scannerEnterResult =
            enterScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult =
            exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto restoredBaselineResult =
            waitForOsWifiConnectedSsid(osManagerClient, baselineIt->ssid, wifiTimeoutMs);
        if (restoredBaselineResult.isError()) {
            return Result<std::monostate, std::string>::error(restoredBaselineResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canSwitchForgetThenScannerBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runCanSwitchThenScannerBackendOnly(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    const std::string& wifiConfigPath,
    int timeoutMs)
{
    static_cast<void>(serverAddress);
    static_cast<void>(uiAddress);

    const auto startTime = std::chrono::steady_clock::now();
    const auto configResult = loadWifiFunctionalTestConfig(wifiConfigPath);
    if (configResult.isError()) {
        return FunctionalTestSummary{
            .name = "canSwitchThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(configResult.errorValue()),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const WifiFunctionalTestConfig config = configResult.value();
    if (config.networks.size() < 2) {
        return FunctionalTestSummary{
            .name = "canSwitchThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchThenScannerBackendOnly requires at least two configured networks"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    const std::string cancelTargetSsid = config.cancelTargetSsid.value_or(config.networks[1].ssid);
    const auto* cancelTarget = findWifiNetworkConfig(config, cancelTargetSsid);
    const auto baselineIt = std::find_if(
        config.networks.begin(),
        config.networks.end(),
        [&](const WifiFunctionalNetworkConfig& network) {
            return network.ssid != cancelTargetSsid;
        });
    if (!cancelTarget || baselineIt == config.networks.end()) {
        return FunctionalTestSummary{
            .name = "canSwitchThenScannerBackendOnly",
            .duration_ms = 0,
            .result = Result<std::monostate, std::string>::error(
                "canSwitchThenScannerBackendOnly requires a baseline network and a distinct "
                "switch target"),
            .failure_screenshot_path = std::nullopt,
            .training_summary = std::nullopt,
        };
    }

    Network::WebSocketService osManagerClient;
    std::optional<std::string> originalConnectedSsid;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto osConnectResult = osManagerClient.connect(osManagerAddress, timeoutMs);
        if (osConnectResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "Failed to connect to os-manager: " + osConnectResult.errorValue());
        }

        auto systemStatusResult = waitForSystemStatusOk(osManagerClient, timeoutMs);
        if (systemStatusResult.isError()) {
            return Result<std::monostate, std::string>::error(systemStatusResult.errorValue());
        }

        auto initialSystemStatus = requestSystemStatus(osManagerClient, timeoutMs);
        if (initialSystemStatus.isError()) {
            return Result<std::monostate, std::string>::error(
                "SystemStatus failed: " + initialSystemStatus.errorValue());
        }
        if (!initialSystemStatus.value().scanner_mode_available) {
            return Result<std::monostate, std::string>::error(
                "Scanner mode unavailable: " + initialSystemStatus.value().scanner_mode_detail);
        }

        auto preflightResult = runWifiPreflight(osManagerClient, config, timeoutMs);
        if (preflightResult.isError()) {
            return Result<std::monostate, std::string>::error(preflightResult.errorValue());
        }
        originalConnectedSsid = preflightResult.value().originalConnectedSsid;

        const int wifiTimeoutMs = std::max(timeoutMs, 60000);
        auto baselineConnectResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (baselineConnectResult.isError()) {
            return baselineConnectResult;
        }

        auto forgetCancelTargetResult =
            forgetWifiViaOsManager(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (forgetCancelTargetResult.isError()) {
            return forgetCancelTargetResult;
        }

        auto osUnsavedCancelTargetResult =
            waitForOsUnsavedNetwork(osManagerClient, cancelTarget->ssid, timeoutMs);
        if (osUnsavedCancelTargetResult.isError()) {
            return Result<std::monostate, std::string>::error(
                osUnsavedCancelTargetResult.errorValue());
        }

        auto connectSwitchTargetResult = connectToWifiViaBackend(
            osManagerClient, cancelTarget->ssid, cancelTarget->password, wifiTimeoutMs);
        if (connectSwitchTargetResult.isError()) {
            return connectSwitchTargetResult;
        }

        auto reconnectBaselineResult = connectToWifiViaBackend(
            osManagerClient, baselineIt->ssid, baselineIt->password, wifiTimeoutMs);
        if (reconnectBaselineResult.isError()) {
            return reconnectBaselineResult;
        }

        auto scannerEnterResult =
            enterScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerEnterResult.isError()) {
            return scannerEnterResult;
        }

        auto systemScannerActiveResult =
            waitForSystemScannerMode(osManagerClient, true, std::max(timeoutMs, 20000));
        if (systemScannerActiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerActiveResult.errorValue());
        }

        auto scannerSnapshotResult =
            waitForSystemScannerSnapshot(osManagerClient, std::max(timeoutMs, 20000));
        if (scannerSnapshotResult.isError()) {
            return Result<std::monostate, std::string>::error(
                "ScannerSnapshotGet failed after entering scanner mode: "
                + scannerSnapshotResult.errorValue());
        }

        auto scannerExitResult =
            exitScannerModeViaBackend(osManagerClient, std::max(timeoutMs, 60000));
        if (scannerExitResult.isError()) {
            return scannerExitResult;
        }

        auto systemScannerInactiveResult =
            waitForSystemScannerMode(osManagerClient, false, std::max(timeoutMs, 60000));
        if (systemScannerInactiveResult.isError()) {
            return Result<std::monostate, std::string>::error(
                systemScannerInactiveResult.errorValue());
        }

        auto restoredBaselineResult =
            waitForOsWifiConnectedSsid(osManagerClient, baselineIt->ssid, wifiTimeoutMs);
        if (restoredBaselineResult.isError()) {
            return Result<std::monostate, std::string>::error(restoredBaselineResult.errorValue());
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    if (osManagerClient.isConnected()) {
        const auto cleanupResult = restoreOriginalWifiConnection(
            osManagerClient, originalConnectedSsid, config, timeoutMs);
        if (cleanupResult.isError()) {
            if (testResult.isError()) {
                testResult = Result<std::monostate, std::string>::error(
                    testResult.errorValue() + "; cleanup failed: " + cleanupResult.errorValue());
            }
            else {
                testResult = Result<std::monostate, std::string>::error(
                    "Cleanup failed: " + cleanupResult.errorValue());
            }
        }
    }

    osManagerClient.disconnect();

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "canSwitchThenScannerBackendOnly",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

FunctionalTestSummary FunctionalTestRunner::runVerifyTraining(
    const std::string& uiAddress,
    const std::string& serverAddress,
    const std::string& osManagerAddress,
    int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    Network::WebSocketService uiClient;
    Network::WebSocketService serverClient;

    auto testResult = [&]() -> Result<std::monostate, std::string> {
        auto restartResult = restartServices(osManagerAddress, timeoutMs);
        if (restartResult.isError()) {
            return Result<std::monostate, std::string>::error(restartResult.errorValue());
        }

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
        if (uiStatus.state == "SimRunning" || uiStatus.state == "Paused") {
            std::cerr << "Sending UI SimStop to return to StartMenu..." << std::endl;
            UiApi::SimStop::Command simStopCmd{};
            auto simStopResult = unwrapResponse(
                uiClient.sendCommandAndGetResponse<UiApi::SimStop::Okay>(simStopCmd, timeoutMs));
            if (simStopResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI SimStop failed: " + simStopResult.errorValue());
            }

            auto startMenuResult = waitForUiState(uiClient, "StartMenu", timeoutMs);
            if (startMenuResult.isError()) {
                uiClient.disconnect();
                return Result<std::monostate, std::string>::error(startMenuResult.errorValue());
            }
        }

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
            uiClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Failed to connect to server: " + connectResult.errorValue());
        }

        Api::StatusGet::Command statusCmd{};
        auto statusResult = unwrapResponse(
            serverClient.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, timeoutMs));
        if (statusResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "Server StatusGet failed: " + statusResult.errorValue());
        }

        const auto& status = statusResult.value();
        std::cerr << "Server state: " << status.state << " (timestep=" << status.timestep << ")"
                  << std::endl;
        if (status.state == "SimRunning" || status.state == "SimPaused") {
            std::cerr << "Sending SimStop to return to Idle..." << std::endl;
            Api::SimStop::Command simStopCmd{};
            auto simStopResult =
                unwrapResponse(serverClient.sendCommandAndGetResponse<Api::SimStop::OkayType>(
                    simStopCmd, timeoutMs));
            if (simStopResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Server SimStop failed: " + simStopResult.errorValue());
            }

            auto idleResult = waitForServerState(serverClient, "Idle", timeoutMs);
            if (idleResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(idleResult.errorValue());
            }
        }

        size_t initialResultCount = 0;
        std::unordered_set<std::string> trainingResultIds;
        {
            Api::TrainingResultList::Command listCmd{};
            auto listResult = unwrapResponse(
                serverClient.sendCommandAndGetResponse<Api::TrainingResultList::Okay>(
                    listCmd, timeoutMs));
            if (listResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "TrainingResultList failed: " + listResult.errorValue());
            }
            const auto& results = listResult.value().results;
            initialResultCount = results.size();
            trainingResultIds.reserve(results.size());
            for (const auto& entry : results) {
                trainingResultIds.insert(entry.summary.trainingSessionId.toString());
            }
        }

        const int populationSize = 5;
        const int runCount = 5;
        const int trainingTimeoutMs = std::max(timeoutMs, 300000);
        const int saveTimeoutMs = std::max(timeoutMs, 20000);

        UiApi::TrainingStart::Command trainCmd;
        trainCmd.evolution.populationSize = populationSize;
        trainCmd.evolution.maxGenerations = 1;
        trainCmd.evolution.maxSimulationTime = 0.1;
        trainCmd.training.scenarioId = Scenario::EnumType::TreeGermination;
        trainCmd.training.organismType = OrganismType::TREE;
        PopulationSpec population;
        population.brainKind = TrainingBrainKind::NeuralNet;
        population.count = populationSize;
        population.randomCount = populationSize;
        trainCmd.training.population = { population };

        auto trainResult = unwrapResponse(
            uiClient.sendCommandAndGetResponse<UiApi::TrainingStart::Okay>(trainCmd, timeoutMs));
        if (trainResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(
                "UI TrainingStart failed: " + trainResult.errorValue());
        }

        auto trainingStateResult =
            waitForUiStateAny(uiClient, { "TrainingActive", "TrainingUnsavedResult" }, timeoutMs);
        if (trainingStateResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(trainingStateResult.errorValue());
        }

        std::unordered_set<GenomeId> previousGenomes;
        std::unordered_set<GenomeId> savedGenomes;
        size_t expectedResultCount = initialResultCount;

        for (int runIndex = 0; runIndex < runCount; ++runIndex) {
            std::cerr << "verifyTraining: waiting for generation " << (runIndex + 1) << "/"
                      << runCount << std::endl;
            auto waitResult =
                waitForServerState(serverClient, "UnsavedTrainingResult", trainingTimeoutMs);
            if (waitResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(waitResult.errorValue());
            }

            const bool restart = (runIndex < runCount - 1);
            std::cerr << "verifyTraining: saving generation " << (runIndex + 1)
                      << " (restart=" << (restart ? "true" : "false") << ")" << std::endl;
            auto saveResult =
                waitForUiTrainingResultSave(uiClient, saveTimeoutMs, std::nullopt, restart);
            if (saveResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "UI TrainingResultSave failed: " + saveResult.errorValue());
            }

            const auto& saveOkay = saveResult.value();
            if (saveOkay.savedCount != populationSize) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "TrainingResultSave savedCount mismatch");
            }
            if (static_cast<int>(saveOkay.savedIds.size()) != populationSize) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "TrainingResultSave savedIds size mismatch");
            }

            std::unordered_set<GenomeId> currentGenomes;
            currentGenomes.reserve(saveOkay.savedIds.size());
            for (const auto& id : saveOkay.savedIds) {
                currentGenomes.insert(id);
                savedGenomes.insert(id);
            }

            if (!previousGenomes.empty() && currentGenomes == previousGenomes) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "Generation genomes did not change between runs");
            }

            previousGenomes = currentGenomes;

            expectedResultCount += 1;
            auto listResult =
                waitForTrainingResultList(serverClient, trainingTimeoutMs, expectedResultCount - 1);
            if (listResult.isError()) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(listResult.errorValue());
            }

            bool foundNew = false;
            for (const auto& entry : listResult.value().results) {
                const std::string sessionId = entry.summary.trainingSessionId.toString();
                if (trainingResultIds.insert(sessionId).second) {
                    foundNew = true;
                    if (entry.candidateCount != populationSize) {
                        uiClient.disconnect();
                        serverClient.disconnect();
                        return Result<std::monostate, std::string>::error(
                            "TrainingResultList candidate count mismatch");
                    }
                    if (entry.summary.maxGenerations != 1
                        || entry.summary.completedGenerations != 1) {
                        uiClient.disconnect();
                        serverClient.disconnect();
                        return Result<std::monostate, std::string>::error(
                            "TrainingResultList generation mismatch");
                    }
                    break;
                }
            }

            if (!foundNew) {
                uiClient.disconnect();
                serverClient.disconnect();
                return Result<std::monostate, std::string>::error(
                    "TrainingResultList did not include a new entry");
            }
        }

        auto idleResult = waitForServerState(serverClient, "Idle", trainingTimeoutMs);
        if (idleResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(idleResult.errorValue());
        }

        auto deleteResult = deleteGenomes(serverClient, savedGenomes, timeoutMs);
        if (deleteResult.isError()) {
            uiClient.disconnect();
            serverClient.disconnect();
            return Result<std::monostate, std::string>::error(deleteResult.errorValue());
        }

        uiClient.disconnect();
        serverClient.disconnect();
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }();

    auto restartResult = restartServices(osManagerAddress, timeoutMs);
    if (restartResult.isError()) {
        if (testResult.isError()) {
            std::cerr << "Restart failed: " << restartResult.errorValue() << std::endl;
        }
        else {
            testResult = Result<std::monostate, std::string>::error(restartResult.errorValue());
        }
    }

    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTime)
                                .count();

    return FunctionalTestSummary{
        .name = "verifyTraining",
        .duration_ms = durationMs,
        .result = std::move(testResult),
        .failure_screenshot_path = std::nullopt,
        .training_summary = std::nullopt,
    };
}

} // namespace Client
} // namespace DirtSim
