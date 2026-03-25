#include "OperatingSystemManager.h"
#include "audio/api/StatusGet.h"
#include "cli/SubprocessManager.h"
#include "core/LoggingChannels.h"
#include "core/StateLifecycle.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/JsonProtocol.h"
#include "os-manager/api/NetworkDiagnosticsModeSet.h"
#include "os-manager/api/NetworkSnapshotChanged.h"
#include "os-manager/api/ScannerSnapshotChanged.h"
#include "os-manager/network/CommandDeserializerJson.h"
#include "os-manager/network/NetworkService.h"
#include "os-manager/network/NexmonChannelController.h"
#include "os-manager/network/PeerAdvertisement.h"
#include "os-manager/network/PeerDiscovery.h"
#include "os-manager/network/ScannerChannelController.h"
#include "os-manager/network/ScannerService.h"
#include "os-manager/ssh/RemoteSshExecutor.h"
#include "server/api/StatusGet.h"
#include "server/api/WebSocketAccessSet.h"
#include "server/api/WebUiAccessSet.h"
#include "ui/state-machine/api/StatusGet.h"
#include "ui/state-machine/api/WebSocketAccessSet.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <pwd.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <sys/reboot.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace DirtSim {
namespace OsManager {

namespace {
constexpr int kDefaultRemoteCommandTimeoutMs = 30000;
constexpr int kScannerHelperEnterTimeoutMs = 15000;
constexpr int kScannerHelperExitTimeoutMs = 15000;
constexpr int kScannerHelperStatusTimeoutMs = 5000;
constexpr const char* kScannerModeHelperPath = "/usr/bin/dirtsim-nexmon-mode";
constexpr const char* kScannerNexutilPath = "/usr/bin/nexutil";
constexpr const char* kScannerStateDir = "/run/dirtsim";
constexpr const char* kScannerStateFile = "/run/dirtsim/scanner_mode_active";
constexpr const char* kNexmonModulePath = "/usr/lib/nexmon/brcmfmac.ko";
constexpr const char* kNexmonFirmwarePath = "/usr/lib/nexmon/cyfmac43455-sdio-standard.bin";
constexpr size_t kScannerOutputLogLimit = 400;
constexpr uint64_t kScannerPushSnapshotMaxAgeMs = 15000;
constexpr size_t kScannerPushSnapshotMaxRadios = 48;
constexpr const char* kWifiInterfaceName = "wlan0";

struct ParsedScannerModeStatus {
    bool stackNexmon = false;
    bool monitorSupported = false;
    std::string loadedVersion;
};

Result<std::monostate, ApiError> makeMissingDependencyError(const std::string& name)
{
    return Result<std::monostate, ApiError>::error(ApiError("Missing dependency for " + name));
}

class UnavailableScannerChannelController : public ScannerChannelController {
public:
    explicit UnavailableScannerChannelController(std::string errorMessage)
        : errorMessage_(std::move(errorMessage))
    {}

    Result<std::monostate, std::string> start() override
    {
        return Result<std::monostate, std::string>::error(errorMessage_);
    }

    void stop() override {}

    Result<std::monostate, std::string> setTuning(const ScannerTuning&) override
    {
        return Result<std::monostate, std::string>::error(errorMessage_);
    }

private:
    std::string errorMessage_;
};

std::string getEnvValue(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return "";
    }
    return value;
}

std::optional<OperatingSystemManager::BackendType> parseBackendType(const std::string& value)
{
    if (value == "systemd") {
        return OperatingSystemManager::BackendType::Systemd;
    }
    if (value == "local") {
        return OperatingSystemManager::BackendType::LocalProcess;
    }
    return std::nullopt;
}

std::string formatWifiAccessPointSummary(const Network::WifiAccessPointInfo& info)
{
    std::ostringstream stream;
    stream << info.bssid << "@";
    if (info.frequencyMhz.has_value()) {
        stream << *info.frequencyMhz;
    }
    else {
        stream << "?";
    }
    stream << "MHz/";
    if (info.channel.has_value()) {
        stream << "ch" << *info.channel;
    }
    else {
        stream << "ch?";
    }
    stream << "/";
    if (info.signalDbm.has_value()) {
        stream << *info.signalDbm << "dBm";
    }
    else {
        stream << "?dBm";
    }
    if (info.active) {
        stream << "/active";
    }
    return stream.str();
}

std::string formatWifiAccessPointsForSsid(
    const std::vector<Network::WifiAccessPointInfo>& accessPoints,
    const std::optional<std::string>& ssid)
{
    constexpr size_t maxEntries = 4;

    std::ostringstream stream;
    size_t matchingCount = 0;
    for (const auto& accessPoint : accessPoints) {
        if (ssid.has_value() && accessPoint.ssid != *ssid) {
            continue;
        }

        if (matchingCount < maxEntries) {
            if (matchingCount > 0) {
                stream << "; ";
            }
            stream << formatWifiAccessPointSummary(accessPoint);
        }
        ++matchingCount;
    }

    if (matchingCount == 0) {
        return "none";
    }
    if (matchingCount > maxEntries) {
        stream << "; +" << (matchingCount - maxEntries) << " more";
    }
    return stream.str();
}

std::string formatWifiConnectPhase(const Network::WifiConnectPhase phase)
{
    switch (phase) {
        case Network::WifiConnectPhase::Starting:
            return "starting";
        case Network::WifiConnectPhase::Associating:
            return "associating";
        case Network::WifiConnectPhase::Authenticating:
            return "authenticating";
        case Network::WifiConnectPhase::GettingAddress:
            return "getting_address";
        case Network::WifiConnectPhase::Canceling:
            return "canceling";
    }

    return "unknown";
}

std::string formatNetworkSnapshotSummary(
    const NetworkService::Snapshot& snapshot, const std::optional<std::string>& focusSsid)
{
    std::ostringstream stream;
    stream << "status.connected=" << (snapshot.status.connected ? "true" : "false")
           << ", status.ssid='" << snapshot.status.ssid << "'"
           << ", active_bssid="
           << (snapshot.activeBssid.has_value() ? *snapshot.activeBssid : std::string("none"))
           << ", scan_in_progress=" << (snapshot.scanInProgress ? "true" : "false");

    if (snapshot.lastScanAgeMs.has_value()) {
        stream << ", last_scan_age_ms=" << *snapshot.lastScanAgeMs;
    }
    else {
        stream << ", last_scan_age_ms=none";
    }

    if (snapshot.connectProgress.has_value()) {
        stream << ", connect_progress={ssid='" << snapshot.connectProgress->ssid
               << "', phase=" << formatWifiConnectPhase(snapshot.connectProgress->phase)
               << ", can_cancel=" << (snapshot.connectProgress->canCancel ? "true" : "false")
               << "}";
    }
    else {
        stream << ", connect_progress=none";
    }

    if (snapshot.connectOutcome.has_value()) {
        stream << ", connect_outcome={ssid='" << snapshot.connectOutcome->ssid
               << "', canceled=" << (snapshot.connectOutcome->canceled ? "true" : "false")
               << ", message='" << snapshot.connectOutcome->message << "'}";
    }
    else {
        stream << ", connect_outcome=none";
    }

    stream << ", access_points(";
    if (focusSsid.has_value()) {
        stream << "'" << *focusSsid << "'";
    }
    else {
        stream << "*";
    }
    stream << ")=" << formatWifiAccessPointsForSsid(snapshot.accessPoints, focusSsid);
    return stream.str();
}

void logNetworkSnapshot(
    NetworkService* networkService,
    const std::string& label,
    const std::optional<std::string>& focusSsid)
{
    if (!networkService) {
        LOG_INFO(State, "{}: network service unavailable.", label);
        return;
    }

    const auto snapshotResult = networkService->getSnapshot(false);
    if (snapshotResult.isError()) {
        LOG_INFO(
            State, "{}: failed to read network snapshot: {}", label, snapshotResult.errorValue());
        return;
    }

    LOG_INFO(
        State, "{}: {}", label, formatNetworkSnapshotSummary(snapshotResult.value(), focusSsid));
}

OsApi::NetworkSnapshotGet::WifiNetworkStatus toApiWifiNetworkStatus(
    const Network::WifiNetworkStatus status)
{
    switch (status) {
        case Network::WifiNetworkStatus::Connected:
            return OsApi::NetworkSnapshotGet::WifiNetworkStatus::Connected;
        case Network::WifiNetworkStatus::Saved:
            return OsApi::NetworkSnapshotGet::WifiNetworkStatus::Saved;
        case Network::WifiNetworkStatus::Open:
            return OsApi::NetworkSnapshotGet::WifiNetworkStatus::Open;
        case Network::WifiNetworkStatus::Available:
            return OsApi::NetworkSnapshotGet::WifiNetworkStatus::Available;
    }

    return OsApi::NetworkSnapshotGet::WifiNetworkStatus::Saved;
}

OsApi::NetworkSnapshotGet::WifiNetworkInfo toApiWifiNetworkInfo(
    const Network::WifiNetworkInfo& info)
{
    return OsApi::NetworkSnapshotGet::WifiNetworkInfo{
        .ssid = info.ssid,
        .status = toApiWifiNetworkStatus(info.status),
        .signalDbm = info.signalDbm,
        .security = info.security,
        .autoConnect = info.autoConnect,
        .hasCredentials = info.hasCredentials,
        .lastUsedDate = info.lastUsedDate,
        .lastUsedRelative = info.lastUsedRelative,
        .connectionId = info.connectionId,
    };
}

OsApi::NetworkSnapshotGet::WifiAccessPointInfo toApiWifiAccessPointInfo(
    const Network::WifiAccessPointInfo& info)
{
    return OsApi::NetworkSnapshotGet::WifiAccessPointInfo{
        .ssid = info.ssid,
        .bssid = info.bssid,
        .signalDbm = info.signalDbm,
        .frequencyMhz = info.frequencyMhz,
        .channel = info.channel,
        .security = info.security,
        .active = info.active,
    };
}

OsApi::NetworkSnapshotGet::WifiStatusInfo toApiWifiStatusInfo(const Network::WifiStatus& status)
{
    return OsApi::NetworkSnapshotGet::WifiStatusInfo{
        .connected = status.connected,
        .ssid = status.ssid,
    };
}

OsApi::NetworkSnapshotGet::WifiConnectPhase toApiWifiConnectPhase(
    const Network::WifiConnectPhase phase)
{
    switch (phase) {
        case Network::WifiConnectPhase::Starting:
            return OsApi::NetworkSnapshotGet::WifiConnectPhase::Starting;
        case Network::WifiConnectPhase::Associating:
            return OsApi::NetworkSnapshotGet::WifiConnectPhase::Associating;
        case Network::WifiConnectPhase::Authenticating:
            return OsApi::NetworkSnapshotGet::WifiConnectPhase::Authenticating;
        case Network::WifiConnectPhase::GettingAddress:
            return OsApi::NetworkSnapshotGet::WifiConnectPhase::GettingAddress;
        case Network::WifiConnectPhase::Canceling:
            return OsApi::NetworkSnapshotGet::WifiConnectPhase::Canceling;
    }

    return OsApi::NetworkSnapshotGet::WifiConnectPhase::Starting;
}

OsApi::NetworkSnapshotGet::WifiConnectProgressInfo toApiWifiConnectProgressInfo(
    const Network::WifiConnectProgress& progress)
{
    return OsApi::NetworkSnapshotGet::WifiConnectProgressInfo{
        .ssid = progress.ssid,
        .phase = toApiWifiConnectPhase(progress.phase),
        .canCancel = progress.canCancel,
    };
}

OsApi::NetworkSnapshotGet::WifiConnectOutcomeInfo toApiWifiConnectOutcomeInfo(
    const Network::WifiConnectOutcome& outcome)
{
    return OsApi::NetworkSnapshotGet::WifiConnectOutcomeInfo{
        .ssid = outcome.ssid,
        .message = outcome.message,
        .canceled = outcome.canceled,
    };
}

OsApi::NetworkSnapshotGet::LocalAddressInfo toApiLocalAddressInfo(
    const NetworkService::LocalAddressInfo& info)
{
    return OsApi::NetworkSnapshotGet::LocalAddressInfo{
        .name = info.name,
        .address = info.address,
    };
}

OsApi::NetworkSnapshotGet::Okay toApiNetworkSnapshotOkay(const NetworkService::Snapshot& snapshot)
{
    OsApi::NetworkSnapshotGet::Okay okay;
    okay.status = toApiWifiStatusInfo(snapshot.status);
    okay.scanInProgress = snapshot.scanInProgress;
    okay.activeBssid = snapshot.activeBssid;
    okay.lastScanAgeMs = snapshot.lastScanAgeMs;
    if (snapshot.connectOutcome.has_value()) {
        okay.connectOutcome = toApiWifiConnectOutcomeInfo(snapshot.connectOutcome.value());
    }
    if (snapshot.connectProgress.has_value()) {
        okay.connectProgress = toApiWifiConnectProgressInfo(snapshot.connectProgress.value());
    }
    okay.localAddresses.reserve(snapshot.localAddresses.size());
    for (const auto& localAddress : snapshot.localAddresses) {
        okay.localAddresses.push_back(toApiLocalAddressInfo(localAddress));
    }
    okay.networks.reserve(snapshot.networks.size());
    for (const auto& network : snapshot.networks) {
        okay.networks.push_back(toApiWifiNetworkInfo(network));
    }
    okay.accessPoints.reserve(snapshot.accessPoints.size());
    for (const auto& accessPoint : snapshot.accessPoints) {
        okay.accessPoints.push_back(toApiWifiAccessPointInfo(accessPoint));
    }
    return okay;
}

OsApi::ScannerSnapshotGet::Okay toApiScannerSnapshotOkay(
    const ScannerService::Snapshot& snapshot,
    const std::string& lastError,
    bool active,
    const std::optional<std::string>& detailOverride = std::nullopt)
{
    OsApi::ScannerSnapshotGet::Okay okay;
    okay.active = active;
    okay.config = snapshot.config;
    okay.currentTuning = snapshot.currentTuning;

    const auto now = std::chrono::steady_clock::now();
    okay.radios.reserve(snapshot.radios.size());
    for (const auto& radio : snapshot.radios) {
        OsApi::ScannerSnapshotGet::ObservedRadioInfo info;
        info.bssid = radio.bssid;
        info.ssid = radio.ssid;
        info.signalDbm = radio.signalDbm;
        info.channel = radio.channel;
        info.observationKind = radio.observationKind;
        if (radio.lastSeenAt.time_since_epoch().count() > 0) {
            info.lastSeenAgeMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - radio.lastSeenAt)
                    .count());
        }
        okay.radios.push_back(std::move(info));
    }

    if (detailOverride.has_value()) {
        okay.detail = *detailOverride;
    }
    else if (!lastError.empty()) {
        okay.detail = lastError;
    }
    else if (active && okay.config.mode == ScannerConfigMode::Manual) {
        const auto& manualConfig = okay.config.manualConfig;
        okay.detail = "Listening " + scannerBandLabel(manualConfig.band) + " "
            + scannerManualTargetShortLabel(
                          manualConfig.band, manualConfig.widthMhz, manualConfig.targetChannel)
            + " @ " + std::to_string(manualConfig.widthMhz) + " MHz.";
    }
    else if (active && okay.currentTuning.has_value()) {
        const auto& tuning = okay.currentTuning.value();
        okay.detail = "Scanning " + scannerBandLabel(tuning.band) + " ch "
            + std::to_string(tuning.primaryChannel) + " @ " + std::to_string(tuning.widthMhz)
            + " MHz.";
    }
    else if (snapshot.running) {
        okay.detail = "Scanning.";
    }
    else if (active) {
        okay.detail = "Scanner capture stopped.";
    }
    else {
        okay.detail = "Scanner mode is not active.";
    }

    return okay;
}

OsApi::WifiConnect::Okay toApiWifiConnectOkay(const Network::WifiConnectResult& result)
{
    return OsApi::WifiConnect::Okay{ .success = result.success, .ssid = result.ssid };
}

OsApi::WifiDisconnect::Okay toApiWifiDisconnectOkay(const Network::WifiDisconnectResult& result)
{
    return OsApi::WifiDisconnect::Okay{ .success = result.success, .ssid = result.ssid };
}

OsApi::WifiForget::Okay toApiWifiForgetOkay(const Network::WifiForgetResult& result)
{
    return OsApi::WifiForget::Okay{
        .success = result.success,
        .ssid = result.ssid,
        .removed = result.removed,
    };
}

std::string resolveBinaryPath(const std::string& overridePath, const std::string& binaryName)
{
    if (!overridePath.empty()) {
        return overridePath;
    }

    std::error_code error;
    const auto exePath = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        const auto sibling = exePath.parent_path() / binaryName;
        if (std::filesystem::exists(sibling, error) && !error) {
            return sibling.string();
        }
    }

    const std::string usrPath = "/usr/bin/" + binaryName;
    if (std::filesystem::exists(usrPath, error) && !error) {
        return usrPath;
    }

    return "";
}

std::string resolveUiDisplay(const std::string& overrideDisplay)
{
    if (!overrideDisplay.empty()) {
        return overrideDisplay;
    }

    const auto display = getEnvValue("DISPLAY");
    if (!display.empty()) {
        return display;
    }

    return ":99";
}

std::string resolveServerPort(const std::string& serverArgs)
{
    if (serverArgs.empty()) {
        return "8080";
    }

    std::istringstream iss(serverArgs);
    std::string token;
    while (iss >> token) {
        if (token == "-p" || token == "--port") {
            std::string port;
            if (iss >> port) {
                return port;
            }
        }
        if (token.rfind("-p", 0) == 0 && token.size() > 2) {
            return token.substr(2);
        }
        if (token.rfind("--port=", 0) == 0 && token.size() > 7) {
            return token.substr(7);
        }
    }

    return "8080";
}

std::string resolveUiArgs(
    const std::string& overrideArgs, const std::string& backend, const std::string& serverPort)
{
    if (!overrideArgs.empty()) {
        return overrideArgs;
    }

    return "-b " + backend + " --connect localhost:" + serverPort;
}

std::string resolveWorkDir(const std::string& overrideDir)
{
    if (!overrideDir.empty()) {
        return overrideDir;
    }

    std::error_code error;
    const std::filesystem::path dataRoot("/data");
    if (std::filesystem::exists(dataRoot, error) && !error) {
        const auto dataDir = dataRoot / "dirtsim";
        std::filesystem::create_directories(dataDir, error);
        if (!error) {
            return dataDir.string();
        }
    }

    const auto homeDir = getEnvValue("HOME");
    if (!homeDir.empty()) {
        return (std::filesystem::path(homeDir) / ".dirtsim").string();
    }

    return "/tmp/dirtsim";
}

std::string generateWebSocketToken()
{
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 15);
    std::string token;
    token.reserve(32);
    for (int i = 0; i < 32; ++i) {
        token.push_back(kHexDigits[dist(rd)]);
    }
    return token;
}

std::string trimWhitespace(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string summarizeCommandOutput(const std::string& value)
{
    std::string normalized = trimWhitespace(value);
    for (char& ch : normalized) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }

    if (normalized.size() <= kScannerOutputLogLimit) {
        return normalized;
    }

    return normalized.substr(0, kScannerOutputLogLimit) + "...";
}

ParsedScannerModeStatus parseScannerModeStatusOutput(const std::string& output)
{
    ParsedScannerModeStatus parsed;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("loaded_version=", 0) == 0) {
            parsed.loadedVersion = line.substr(std::string("loaded_version=").size());
        }
        else if (line == "monitor_supported=1") {
            parsed.monitorSupported = true;
        }
        else if (line == "stack=nexmon") {
            parsed.stackNexmon = true;
        }
    }
    return parsed;
}

std::string buildMissingScannerDetail(
    bool helperExists, bool nexutilExists, bool nexmonModuleExists, bool nexmonFirmwareExists)
{
    std::vector<std::string> missing;
    if (!helperExists) {
        missing.push_back("dirtsim-nexmon-mode");
    }
    if (!nexutilExists) {
        missing.push_back("nexutil");
    }
    if (!nexmonModuleExists) {
        missing.push_back("Nexmon driver");
    }
    if (!nexmonFirmwareExists) {
        missing.push_back("Nexmon firmware");
    }

    if (missing.empty()) {
        return "Scanner mode status is unavailable.";
    }

    std::string detail = "Scanner mode unavailable: missing ";
    for (size_t i = 0; i < missing.size(); ++i) {
        if (i > 0) {
            detail += (i + 1 == missing.size()) ? " and " : ", ";
        }
        detail += missing[i];
    }
    detail += ".";
    return detail;
}

Result<std::string, ApiError> readFileToString(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<std::string, ApiError>::error(
            ApiError("Failed to open file: " + path.string()));
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return Result<std::string, ApiError>::okay(buffer.str());
}

Result<std::string, ApiError> runCommandCaptureOutput(const std::string& command)
{
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return Result<std::string, ApiError>::error(ApiError("Failed to run command"));
    }

    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    const int status = pclose(pipe);
    if (status == -1) {
        return Result<std::string, ApiError>::error(ApiError("Command failed to exit cleanly"));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return Result<std::string, ApiError>::error(ApiError("Command failed"));
    }

    return Result<std::string, ApiError>::okay(output);
}

Result<std::string, ApiError> extractKeyBody(const std::string& publicKey)
{
    std::istringstream stream(publicKey);
    std::string keyType;
    std::string keyBody;
    stream >> keyType >> keyBody;
    if (keyType.empty() || keyBody.empty()) {
        return Result<std::string, ApiError>::error(ApiError("Invalid public key format"));
    }
    return Result<std::string, ApiError>::okay(keyBody);
}

Result<std::string, ApiError> normalizeAuthorizedKeyLine(const std::string& publicKey)
{
    const std::string normalized = trimWhitespace(publicKey);
    if (normalized.empty()) {
        return Result<std::string, ApiError>::error(ApiError("Client public key is required"));
    }

    for (const unsigned char ch : normalized) {
        if (ch == '\0' || ch == '\n' || ch == '\r') {
            return Result<std::string, ApiError>::error(
                ApiError("Client public key contains invalid control characters"));
        }
        if (std::iscntrl(ch) && ch != '\t') {
            return Result<std::string, ApiError>::error(
                ApiError("Client public key contains invalid control characters"));
        }
    }

    return Result<std::string, ApiError>::okay(normalized);
}

Result<std::string, ApiError> extractFingerprintSha256(const std::string& output)
{
    const std::string token = "SHA256:";
    const auto pos = output.find(token);
    if (pos == std::string::npos) {
        return Result<std::string, ApiError>::error(ApiError("Fingerprint not found"));
    }

    const auto end = output.find_first_of(" \t\r\n", pos);
    if (end == std::string::npos) {
        return Result<std::string, ApiError>::okay(output.substr(pos));
    }
    return Result<std::string, ApiError>::okay(output.substr(pos, end - pos));
}

bool hasMissingCliMessage(const std::string& text)
{
    if (text.find("dirtsim-cli") == std::string::npos) {
        return false;
    }
    if (text.find("not found") != std::string::npos) {
        return true;
    }
    return text.find("No such file or directory") != std::string::npos;
}

bool isMissingCliResult(const OsApi::RemoteCliRun::Okay& result)
{
    if (result.exit_code == 126 || result.exit_code == 127) {
        return true;
    }
    return hasMissingCliMessage(result.stderr) || hasMissingCliMessage(result.stdout);
}

Result<std::vector<std::string>, ApiError> readFileLines(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<std::vector<std::string>, ApiError>::error(
            ApiError("Failed to open file: " + path.string()));
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return Result<std::vector<std::string>, ApiError>::okay(lines);
}

Result<std::monostate, ApiError> writeFileLinesAtomic(
    const std::filesystem::path& path, const std::vector<std::string>& lines)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to create directory: " + path.parent_path().string()));
    }

    auto tempPath = path;
    tempPath += ".tmp";

    std::ofstream file(tempPath);
    if (!file.is_open()) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to open file: " + tempPath.string()));
    }

    for (const auto& line : lines) {
        file << line << "\n";
    }
    file.close();

    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to save file: " + path.string()));
    }

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

Result<std::pair<uid_t, gid_t>, ApiError> getUserIds(const std::string& user)
{
    struct passwd* pwd = getpwnam(user.c_str());
    if (!pwd) {
        return Result<std::pair<uid_t, gid_t>, ApiError>::error(
            ApiError("Failed to resolve user: " + user));
    }

    return Result<std::pair<uid_t, gid_t>, ApiError>::okay(
        std::make_pair(pwd->pw_uid, pwd->pw_gid));
}

std::filesystem::path resolveUserHomeDir(const std::string& user)
{
    struct passwd* pwd = getpwnam(user.c_str());
    if (pwd && pwd->pw_dir) {
        return pwd->pw_dir;
    }
    return std::filesystem::path("/home") / user;
}

Result<std::monostate, ApiError> ensureSshPermissions(
    const std::filesystem::path& dirPath,
    const std::filesystem::path& filePath,
    const std::string& user)
{
    auto idsResult = getUserIds(user);
    if (idsResult.isError()) {
        return Result<std::monostate, ApiError>::error(idsResult.errorValue());
    }

    const auto [uid, gid] = idsResult.value();
    std::error_code error;
    std::filesystem::permissions(
        dirPath, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
    if (error) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to set permissions for " + dirPath.string()));
    }

    std::filesystem::permissions(
        filePath,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to set permissions for " + filePath.string()));
    }

    if (::chown(dirPath.c_str(), uid, gid) != 0) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to set ownership for " + dirPath.string()));
    }
    if (::chown(filePath.c_str(), uid, gid) != 0) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to set ownership for " + filePath.string()));
    }

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}
} // namespace

class LocalProcessBackend {
public:
    struct Config {
        std::string audioArgs;
        std::string audioPath;
        std::string serverPath;
        std::string serverArgs;
        std::string uiPath;
        std::string uiArgs;
        std::string uiDisplay;
        std::string workDir;
    };

    explicit LocalProcessBackend(const Config& config) : config_(config) {}

    Result<std::monostate, ApiError> runCommand(
        const std::string& action, const std::string& unitName)
    {
        const auto service = resolveService(unitName);
        if (!service.has_value()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Unknown service: " + unitName));
        }

        if (action == "start") {
            return startService(service.value());
        }
        if (action == "stop") {
            return stopService(service.value());
        }
        if (action == "restart") {
            return restartService(service.value());
        }

        return Result<std::monostate, ApiError>::error(ApiError("Unknown action: " + action));
    }

    void poll()
    {
        subprocessManager_.isAudioRunning();
        subprocessManager_.isServerRunning();
        subprocessManager_.isUIRunning();
    }

private:
    enum class Service {
        Audio,
        Server,
        Ui,
    };

    std::optional<Service> resolveService(const std::string& unitName) const
    {
        if (unitName == "dirtsim-audio.service" || unitName == "dirtsim-audio") {
            return Service::Audio;
        }
        if (unitName == "dirtsim-server.service" || unitName == "dirtsim-server") {
            return Service::Server;
        }
        if (unitName == "dirtsim-ui.service" || unitName == "dirtsim-ui") {
            return Service::Ui;
        }
        return std::nullopt;
    }

    bool ensureWorkDir(std::string& errorMessage)
    {
        if (config_.workDir.empty()) {
            return true;
        }

        std::error_code error;
        std::filesystem::create_directories(config_.workDir, error);
        if (!error) {
            return true;
        }

        errorMessage = "Failed to create work dir " + config_.workDir + ": " + error.message();
        return false;
    }

    Result<std::monostate, ApiError> startService(Service service)
    {
        if (service == Service::Audio && config_.audioPath.empty()) {
            return Result<std::monostate, ApiError>::error(ApiError("Audio binary not found"));
        }
        if (service == Service::Server && config_.serverPath.empty()) {
            return Result<std::monostate, ApiError>::error(ApiError("Server binary not found"));
        }
        if (service == Service::Ui && config_.uiPath.empty()) {
            return Result<std::monostate, ApiError>::error(ApiError("UI binary not found"));
        }

        std::string errorMessage;
        if (!ensureWorkDir(errorMessage)) {
            return Result<std::monostate, ApiError>::error(ApiError(errorMessage));
        }

        if (service == Service::Audio) {
            if (subprocessManager_.isAudioRunning()) {
                LOG_INFO(State, "Audio already running");
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            }

            Client::SubprocessManager::ProcessOptions options;
            options.workingDirectory = config_.workDir;

            if (!subprocessManager_.launchAudio(config_.audioPath, config_.audioArgs, options)) {
                return Result<std::monostate, ApiError>::error(
                    ApiError("Failed to launch audio process"));
            }

            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }

        if (service == Service::Server) {
            if (subprocessManager_.isServerRunning()) {
                LOG_INFO(State, "Server already running");
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            }

            Client::SubprocessManager::ProcessOptions options;
            options.workingDirectory = config_.workDir;

            if (!subprocessManager_.launchServer(config_.serverPath, config_.serverArgs, options)) {
                return Result<std::monostate, ApiError>::error(
                    ApiError("Failed to launch server process"));
            }

            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }

        if (subprocessManager_.isUIRunning()) {
            LOG_INFO(State, "UI already running");
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }

        Client::SubprocessManager::ProcessOptions options;
        options.workingDirectory = config_.workDir;
        if (!config_.uiDisplay.empty()) {
            options.environmentOverrides.emplace_back("DISPLAY", config_.uiDisplay);
        }

        if (!subprocessManager_.launchUI(config_.uiPath, config_.uiArgs, options)) {
            return Result<std::monostate, ApiError>::error(ApiError("Failed to launch UI process"));
        }

        return Result<std::monostate, ApiError>::okay(std::monostate{});
    }

    Result<std::monostate, ApiError> stopService(Service service)
    {
        if (service == Service::Audio) {
            if (!subprocessManager_.isAudioRunning()) {
                LOG_INFO(State, "Audio already stopped");
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            }
            subprocessManager_.killAudio();
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }

        if (service == Service::Server) {
            if (!subprocessManager_.isServerRunning()) {
                LOG_INFO(State, "Server already stopped");
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            }
            subprocessManager_.killServer();
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }

        if (!subprocessManager_.isUIRunning()) {
            LOG_INFO(State, "UI already stopped");
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }
        subprocessManager_.killUI();
        return Result<std::monostate, ApiError>::okay(std::monostate{});
    }

    Result<std::monostate, ApiError> restartService(Service service)
    {
        auto stopResult = stopService(service);
        if (stopResult.isError()) {
            return stopResult;
        }
        return startService(service);
    }

    Config config_;
    Client::SubprocessManager subprocessManager_;
};

OperatingSystemManager::OperatingSystemManager(uint16_t port) : port_(port)
{
    initializeDefaultDependencies();
    initializeServices();
    setupWebSocketService();
    webSocketToken_ = generateWebSocketToken();
    initializePeerDiscovery();
}

OperatingSystemManager::OperatingSystemManager(uint16_t port, const BackendConfig& backendConfig)
    : port_(port), backendConfig_(backendConfig)
{
    initializeDefaultDependencies();
    initializeServices();
    setupWebSocketService();
    webSocketToken_ = generateWebSocketToken();
    initializePeerDiscovery();
}

OperatingSystemManager::OperatingSystemManager(TestMode mode)
    : enableNetworking_(false),
      dependencies_(std::move(mode.dependencies)),
      backendConfig_(mode.hasBackendConfig ? mode.backendConfig : BackendConfig{})
{
    if (!dependencies_.serviceCommand) {
        dependencies_.serviceCommand = [](const std::string&, const std::string&) {
            return makeMissingDependencyError("serviceCommand");
        };
    }

    if (!dependencies_.systemStatus) {
        dependencies_.systemStatus = [] { return OsApi::SystemStatus::Okay{}; };
    }

    if (!dependencies_.reboot) {
        dependencies_.reboot = [] {};
    }

    if (!dependencies_.commandRunner) {
        dependencies_.commandRunner = [](const std::string&) {
            return Result<std::string, ApiError>::error(
                ApiError("Missing dependency for commandRunner"));
        };
    }
    if (!dependencies_.processRunner) {
        dependencies_.processRunner = [](const std::vector<std::string>&, int) {
            return Result<ProcessRunResult, std::string>::error(
                "Missing dependency for processRunner");
        };
    }
    if (!dependencies_.scannerChannelController) {
        dependencies_.scannerChannelController =
            std::make_shared<UnavailableScannerChannelController>(
                "Missing dependency for scannerChannelController");
    }

    if (!dependencies_.homeDirResolver) {
        dependencies_.homeDirResolver = [](const std::string& user) {
            return resolveUserHomeDir(user);
        };
    }

    if (!dependencies_.sshPermissionsEnsurer) {
        dependencies_.sshPermissionsEnsurer = [](const std::filesystem::path& dirPath,
                                                 const std::filesystem::path& filePath,
                                                 const std::string& user) {
            return ensureSshPermissions(dirPath, filePath, user);
        };
    }

    initializeServices();
    webSocketToken_ = generateWebSocketToken();
}

OperatingSystemManager::~OperatingSystemManager() = default;

OperatingSystemManager::BackendConfig OperatingSystemManager::BackendConfig::fromEnvironment()
{
    BackendConfig config;

    const auto backendValue = getEnvValue("DIRTSIM_OS_BACKEND");
    if (!backendValue.empty()) {
        const auto backend = parseBackendType(backendValue);
        if (backend.has_value()) {
            config.type = backend.value();
        }
    }

    config.serverPath = getEnvValue("DIRTSIM_SERVER_PATH");
    config.serverArgs = getEnvValue("DIRTSIM_SERVER_ARGS");
    config.uiPath = getEnvValue("DIRTSIM_UI_PATH");
    config.uiArgs = getEnvValue("DIRTSIM_UI_ARGS");
    config.audioPath = getEnvValue("DIRTSIM_AUDIO_PATH");
    config.audioArgs = getEnvValue("DIRTSIM_AUDIO_ARGS");
    config.uiBackend = getEnvValue("DIRTSIM_UI_BACKEND");
    config.uiDisplay = getEnvValue("DIRTSIM_UI_DISPLAY");
    config.workDir = getEnvValue("DIRTSIM_WORKDIR");

    return config;
}

Result<std::monostate, std::string> OperatingSystemManager::start()
{
    if (networkService_) {
        const auto networkStartResult = networkService_->start();
        if (networkStartResult.isError()) {
            return Result<std::monostate, std::string>::error(networkStartResult.errorValue());
        }
    }

    if (!enableNetworking_) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    auto listenResult = wsService_.listen(port_, "127.0.0.1");
    if (listenResult.isError()) {
        return listenResult;
    }

    LOG_INFO(Network, "os-manager WebSocket listening on port {}", port_);
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void OperatingSystemManager::initializeServices()
{
    networkService_ = std::make_unique<NetworkService>();
    scannerService_ = std::make_unique<ScannerService>(dependencies_.scannerChannelController);
    networkService_->setSnapshotChangedCallback([this](const NetworkService::Snapshot& snapshot) {
        publishNetworkSnapshotChanged(snapshot);
    });
    scannerService_->setSnapshotChangedCallback(
        [this]() { publishScannerSnapshotChanged(true, std::nullopt); });
}

void OperatingSystemManager::stop()
{
    if (networkService_) {
        networkService_->stop();
    }
    if (scannerService_) {
        scannerService_->stop();
    }

    if (enableNetworking_) {
        wsService_.stopListening();
    }

    if (serverPeerAdvertisement_) {
        serverPeerAdvertisement_->stop();
    }
    if (uiPeerAdvertisement_) {
        uiPeerAdvertisement_->stop();
    }
    if (peerDiscovery_) {
        peerDiscovery_->stop();
    }
}

void OperatingSystemManager::mainLoopRun()
{
    LOG_INFO(State, "Starting main event loop");
    transitionTo(State::Startup{});

    while (!shouldExit()) {
        processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    LOG_INFO(State, "Main event loop exiting (shouldExit=true)");
}

void OperatingSystemManager::requestExit()
{
    setShouldExit(true);
}

void OperatingSystemManager::queueEvent(const Event& event)
{
    LOG_INFO(State, "Queueing event: {}", getEventName(event));
    eventProcessor_.enqueueEvent(event);
}

void OperatingSystemManager::processEvents()
{
    eventProcessor_.processEventsFromQueue(*this);
    if (localBackend_) {
        localBackend_->poll();
    }
}

std::string OperatingSystemManager::getCurrentStateName() const
{
    return State::getCurrentStateName(fsmState_);
}

void OperatingSystemManager::handleEvent(const Event& event)
{
    LOG_INFO(State, "Handling event: {}", getEventName(event));

    std::visit(
        [this](auto&& evt) {
            std::visit(
                [this, &evt](auto&& state) -> void {
                    using StateType = std::decay_t<decltype(state)>;

                    if constexpr (requires { state.onEvent(evt, *this); }) {
                        auto newState = state.onEvent(evt, *this);
                        if (!std::holds_alternative<StateType>(newState.getVariant())) {
                            transitionTo(std::move(newState));
                        }
                        else {
                            fsmState_ = std::move(newState);
                        }
                    }
                    else {
                        LOG_WARN(
                            State,
                            "State {} does not handle event {}",
                            State::getCurrentStateName(fsmState_),
                            getEventName(Event{ evt }));

                        if constexpr (requires {
                                          evt.sendResponse(
                                              std::declval<typename std::decay_t<
                                                  decltype(evt)>::Response>());
                                      }) {
                            auto errorMsg = std::string("Command not supported in state: ")
                                + State::getCurrentStateName(fsmState_);
                            using EventType = std::decay_t<decltype(evt)>;
                            using ResponseType = typename EventType::Response;
                            evt.sendResponse(ResponseType::error(ApiError(errorMsg)));
                        }
                    }
                },
                fsmState_.getVariant());
        },
        event.getVariant());
}

void OperatingSystemManager::setupWebSocketService()
{
    if (!enableNetworking_) {
        return;
    }

    wsService_.registerHandler<OsApi::NetworkSnapshotGet::Cwc>(
        [this](OsApi::NetworkSnapshotGet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::NetworkDiagnosticsModeSet::Cwc>(
        [this](OsApi::NetworkDiagnosticsModeSet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::PeerClientKeyEnsure::Cwc>(
        [this](OsApi::PeerClientKeyEnsure::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::PeersGet::Cwc>(
        [this](OsApi::PeersGet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::RemoteCliRun::Cwc>(
        [this](OsApi::RemoteCliRun::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::SystemStatus::Cwc>(
        [this](OsApi::SystemStatus::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StartServer::Cwc>(
        [this](OsApi::StartServer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StopServer::Cwc>(
        [this](OsApi::StopServer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::RestartServer::Cwc>(
        [this](OsApi::RestartServer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StartAudio::Cwc>(
        [this](OsApi::StartAudio::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StopAudio::Cwc>(
        [this](OsApi::StopAudio::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::RestartAudio::Cwc>(
        [this](OsApi::RestartAudio::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StartUi::Cwc>(
        [this](OsApi::StartUi::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StopUi::Cwc>(
        [this](OsApi::StopUi::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::RestartUi::Cwc>(
        [this](OsApi::RestartUi::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::ScannerConfigGet::Cwc>(
        [this](OsApi::ScannerConfigGet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::ScannerConfigSet::Cwc>(
        [this](OsApi::ScannerConfigSet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::ScannerModeEnter::Cwc>(
        [this](OsApi::ScannerModeEnter::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::ScannerModeExit::Cwc>(
        [this](OsApi::ScannerModeExit::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::ScannerProbeRun::Cwc>(
        [this](OsApi::ScannerProbeRun::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::ScannerSnapshotGet::Cwc>(
        [this](OsApi::ScannerSnapshotGet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::Reboot::Cwc>(
        [this](OsApi::Reboot::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::TrustBundleGet::Cwc>(
        [this](OsApi::TrustBundleGet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::TrustPeer::Cwc>(
        [this](OsApi::TrustPeer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::UntrustPeer::Cwc>(
        [this](OsApi::UntrustPeer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WifiConnectCancel::Cwc>(
        [this](OsApi::WifiConnectCancel::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WifiConnect::Cwc>(
        [this](OsApi::WifiConnect::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WifiDisconnect::Cwc>(
        [this](OsApi::WifiDisconnect::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WifiForget::Cwc>(
        [this](OsApi::WifiForget::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WifiScanRequest::Cwc>(
        [this](OsApi::WifiScanRequest::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WebSocketAccessSet::Cwc>(
        [this](OsApi::WebSocketAccessSet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::WebUiAccessSet::Cwc>(
        [this](OsApi::WebUiAccessSet::Cwc cwc) { queueEvent(cwc); });

    wsService_.setJsonDeserializer([](const std::string& json) -> std::any {
        CommandDeserializerJson deserializer;
        auto result = deserializer.deserialize(json);
        if (result.isError()) {
            throw std::runtime_error(result.errorValue().message);
        }
        return result.value();
    });

    wsService_.setJsonCommandDispatcher(
        [this](
            std::any cmdAny,
            std::shared_ptr<rtc::WebSocket> ws,
            uint64_t correlationId,
            Network::WebSocketService::HandlerInvoker invokeHandler) {
            OsApi::OsApiCommand cmdVariant = std::any_cast<OsApi::OsApiCommand>(cmdAny);

#define DISPATCH_OS_CMD_WITH_RESP(NamespaceType)                                            \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }

#define DISPATCH_OS_CMD_EMPTY(NamespaceType)                                                \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }

            DISPATCH_OS_CMD_EMPTY(OsApi::Reboot);
            DISPATCH_OS_CMD_EMPTY(OsApi::RestartAudio);
            DISPATCH_OS_CMD_EMPTY(OsApi::RestartServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::RestartUi);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::ScannerConfigGet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::ScannerConfigSet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::ScannerModeEnter);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::ScannerModeExit);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::ScannerProbeRun);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::ScannerSnapshotGet);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartAudio);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartUi);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopAudio);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopUi);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::NetworkDiagnosticsModeSet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::NetworkSnapshotGet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::PeerClientKeyEnsure);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::PeersGet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::RemoteCliRun);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::SystemStatus);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::TrustBundleGet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::TrustPeer);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::UntrustPeer);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WifiConnectCancel);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WifiConnect);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WifiDisconnect);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WifiForget);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WifiScanRequest);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WebSocketAccessSet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::WebUiAccessSet);

#undef DISPATCH_OS_CMD_WITH_RESP
#undef DISPATCH_OS_CMD_EMPTY

            LOG_WARN(Network, "Unknown os-manager JSON command");
        });

    LOG_INFO(Network, "os-manager WebSocket handlers registered");
}

void OperatingSystemManager::initializePeerDiscovery()
{
    if (!enableNetworking_) {
        return;
    }

    char hostname[256] = "dirtsim";
    gethostname(hostname, sizeof(hostname));
    peerServiceName_ = hostname;
    peerUiServiceName_ = std::string(hostname) + "-ui";

    peerDiscovery_ = std::make_unique<PeerDiscovery>();
    if (peerDiscovery_->start()) {
        LOG_INFO(Network, "PeerDiscovery started successfully");
    }
    else {
        LOG_WARN(Network, "PeerDiscovery failed to start (Avahi may not be available)");
    }

    serverPeerAdvertisement_ = std::make_unique<PeerAdvertisement>();
    uiPeerAdvertisement_ = std::make_unique<PeerAdvertisement>();
}

OsApi::SystemStatus::Okay OperatingSystemManager::buildSystemStatus()
{
    if (dependencies_.systemStatus) {
        return dependencies_.systemStatus();
    }

    OsApi::SystemStatus::Okay status;
    status.lan_web_ui_enabled = webUiEnabled_;
    status.lan_websocket_enabled = webSocketEnabled_;
    status.lan_websocket_token = webSocketEnabled_ ? webSocketToken_ : "";
    return status;
}

Result<OsApi::NetworkSnapshotGet::Okay, ApiError> OperatingSystemManager::getNetworkSnapshot(
    const OsApi::NetworkSnapshotGet::Command& command)
{
    if (!networkService_) {
        return Result<OsApi::NetworkSnapshotGet::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto snapshotResult = networkService_->getSnapshot(command.forceRefresh);
    if (snapshotResult.isError()) {
        return Result<OsApi::NetworkSnapshotGet::Okay, ApiError>::error(
            ApiError(snapshotResult.errorValue()));
    }

    return Result<OsApi::NetworkSnapshotGet::Okay, ApiError>::okay(
        toApiNetworkSnapshotOkay(snapshotResult.value()));
}

Result<OsApi::ScannerSnapshotGet::Okay, ApiError> OperatingSystemManager::getScannerSnapshot(
    const OsApi::ScannerSnapshotGet::Command& command)
{
    const auto status = readScannerModeStatusInternal();
    if (!status.available) {
        return Result<OsApi::ScannerSnapshotGet::Okay, ApiError>::error(ApiError(status.detail));
    }
    if (!status.active) {
        return Result<OsApi::ScannerSnapshotGet::Okay, ApiError>::error(
            ApiError("Scanner mode is not active."));
    }
    if (!scannerService_) {
        return Result<OsApi::ScannerSnapshotGet::Okay, ApiError>::error(
            ApiError("Scanner service is unavailable."));
    }

    OsApi::ScannerSnapshotGet::Okay okay;
    okay.active = true;

    const uint64_t maxAgeMs = command.maxAgeMs.value_or(15000);
    const size_t maxRadios = static_cast<size_t>(command.maxRadios.value_or(64));

    const auto startResult = scannerService_->start();
    if (startResult.isError()) {
        okay.detail = "Capture unavailable: " + startResult.errorValue();
        return Result<OsApi::ScannerSnapshotGet::Okay, ApiError>::okay(std::move(okay));
    }

    const auto snapshot = scannerService_->snapshot(maxAgeMs, maxRadios);
    okay = toApiScannerSnapshotOkay(snapshot, scannerService_->lastError(), true);
    return Result<OsApi::ScannerSnapshotGet::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::ScannerConfigGet::Okay, ApiError> OperatingSystemManager::getScannerConfig(
    const OsApi::ScannerConfigGet::Command& /*command*/)
{
    const auto status = readScannerModeStatusInternal();
    if (!status.available) {
        return Result<OsApi::ScannerConfigGet::Okay, ApiError>::error(ApiError(status.detail));
    }
    if (!scannerService_) {
        return Result<OsApi::ScannerConfigGet::Okay, ApiError>::error(
            ApiError("Scanner service is unavailable."));
    }

    return Result<OsApi::ScannerConfigGet::Okay, ApiError>::okay(
        OsApi::ScannerConfigGet::Okay{ .config = scannerService_->config() });
}

Result<OsApi::ScannerConfigSet::Okay, ApiError> OperatingSystemManager::setScannerConfig(
    const OsApi::ScannerConfigSet::Command& command)
{
    const auto status = readScannerModeStatusInternal();
    if (!status.available) {
        return Result<OsApi::ScannerConfigSet::Okay, ApiError>::error(ApiError(status.detail));
    }
    if (!scannerService_) {
        return Result<OsApi::ScannerConfigSet::Okay, ApiError>::error(
            ApiError("Scanner service is unavailable."));
    }

    const auto setConfigResult = scannerService_->setConfig(command.config);
    if (setConfigResult.isError()) {
        return Result<OsApi::ScannerConfigSet::Okay, ApiError>::error(
            ApiError(setConfigResult.errorValue()));
    }

    publishScannerSnapshotChanged(status.active, std::nullopt);
    return Result<OsApi::ScannerConfigSet::Okay, ApiError>::okay(
        OsApi::ScannerConfigSet::Okay{ .config = scannerService_->config() });
}

Result<OsApi::ScannerProbeRun::Okay, ApiError> OperatingSystemManager::runScannerProbe(
    const OsApi::ScannerProbeRun::Command& command)
{
    const auto status = readScannerModeStatusInternal();
    if (!status.available) {
        return Result<OsApi::ScannerProbeRun::Okay, ApiError>::error(ApiError(status.detail));
    }
    if (!status.active) {
        return Result<OsApi::ScannerProbeRun::Okay, ApiError>::error(
            ApiError("Scanner mode is not active."));
    }
    if (!scannerService_) {
        return Result<OsApi::ScannerProbeRun::Okay, ApiError>::error(
            ApiError("Scanner service is unavailable."));
    }

    const auto startResult = scannerService_->start();
    if (startResult.isError()) {
        return Result<OsApi::ScannerProbeRun::Okay, ApiError>::error(
            ApiError("Capture unavailable: " + startResult.errorValue()));
    }

    const auto probeResult = scannerService_->runProbe(
        ScannerService::ProbeRequest{
            .tuning = command.tuning,
            .dwellMs = command.dwellMs,
            .sampleCount = command.sampleCount,
        });
    if (probeResult.isError()) {
        return Result<OsApi::ScannerProbeRun::Okay, ApiError>::error(
            ApiError(probeResult.errorValue()));
    }

    OsApi::ScannerProbeRun::Okay okay{
        .tuning = probeResult.value().tuning,
        .dwellMs = probeResult.value().dwellMs,
        .dwells = {},
    };
    okay.dwells.reserve(probeResult.value().dwells.size());
    for (const auto& dwell : probeResult.value().dwells) {
        okay.dwells.push_back(
            OsApi::ScannerProbeRun::ProbeDwellInfo{
                .sawTraffic = dwell.sawTraffic,
                .radiosSeen = dwell.radiosSeen,
                .newRadiosSeen = dwell.newRadiosSeen,
                .strongestSignalDbm = dwell.strongestSignalDbm,
                .observedChannels = dwell.observedChannels,
            });
    }
    return Result<OsApi::ScannerProbeRun::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::NetworkDiagnosticsModeSet::Okay, ApiError> OperatingSystemManager::
    setNetworkDiagnosticsMode(const OsApi::NetworkDiagnosticsModeSet::Command& command)
{
    if (!networkService_) {
        return Result<OsApi::NetworkDiagnosticsModeSet::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto result = networkService_->setDiagnosticsMode(command.active);
    if (result.isError()) {
        return Result<OsApi::NetworkDiagnosticsModeSet::Okay, ApiError>::error(
            ApiError(result.errorValue()));
    }

    return Result<OsApi::NetworkDiagnosticsModeSet::Okay, ApiError>::okay(
        OsApi::NetworkDiagnosticsModeSet::Okay{ .active = command.active });
}

std::vector<PeerInfo> OperatingSystemManager::getPeers() const
{
    if (!peerDiscovery_) {
        return {};
    }
    return peerDiscovery_->getPeers();
}

Result<std::string, ApiError> OperatingSystemManager::runCommandCapture(
    const std::string& command) const
{
    if (dependencies_.commandRunner) {
        return dependencies_.commandRunner(command);
    }
    return runCommandCaptureOutput(command);
}

Result<OsApi::WifiConnect::Okay, ApiError> OperatingSystemManager::wifiConnect(
    const OsApi::WifiConnect::Command& command)
{
    if (!networkService_) {
        return Result<OsApi::WifiConnect::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto result = networkService_->connectBySsid(command.ssid, command.password);
    if (result.isError()) {
        return Result<OsApi::WifiConnect::Okay, ApiError>::error(ApiError(result.errorValue()));
    }

    return Result<OsApi::WifiConnect::Okay, ApiError>::okay(toApiWifiConnectOkay(result.value()));
}

void OperatingSystemManager::wifiConnectAsync(OsApi::WifiConnect::Cwc cwc)
{
    if (!networkService_) {
        cwc.sendResponse(
            Result<OsApi::WifiConnect::Okay, ApiError>::error(
                ApiError("NetworkService is unavailable")));
        return;
    }

    const auto command = cwc.command;
    const auto startResult = networkService_->connectBySsidAsync(
        command.ssid,
        command.password,
        [cwc = std::move(cwc)](Result<Network::WifiConnectResult, std::string> result) mutable {
            if (result.isError()) {
                cwc.sendResponse(
                    Result<OsApi::WifiConnect::Okay, ApiError>::error(
                        ApiError(result.errorValue())));
                return;
            }

            cwc.sendResponse(
                Result<OsApi::WifiConnect::Okay, ApiError>::okay(
                    toApiWifiConnectOkay(result.value())));
        });
    if (startResult.isError()) {
        cwc.sendResponse(
            Result<OsApi::WifiConnect::Okay, ApiError>::error(ApiError(startResult.errorValue())));
    }
}

Result<OsApi::WifiConnectCancel::Okay, ApiError> OperatingSystemManager::wifiConnectCancel(
    const OsApi::WifiConnectCancel::Command& /*command*/)
{
    if (!networkService_) {
        return Result<OsApi::WifiConnectCancel::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto result = networkService_->cancelConnect();
    if (result.isError()) {
        return Result<OsApi::WifiConnectCancel::Okay, ApiError>::error(
            ApiError(result.errorValue()));
    }

    return Result<OsApi::WifiConnectCancel::Okay, ApiError>::okay(
        OsApi::WifiConnectCancel::Okay{ .accepted = true });
}

Result<OsApi::WifiDisconnect::Okay, ApiError> OperatingSystemManager::wifiDisconnect(
    const OsApi::WifiDisconnect::Command& command)
{
    if (!networkService_) {
        return Result<OsApi::WifiDisconnect::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto result = networkService_->disconnect(command.ssid);
    if (result.isError()) {
        return Result<OsApi::WifiDisconnect::Okay, ApiError>::error(ApiError(result.errorValue()));
    }

    return Result<OsApi::WifiDisconnect::Okay, ApiError>::okay(
        toApiWifiDisconnectOkay(result.value()));
}

Result<OsApi::WifiForget::Okay, ApiError> OperatingSystemManager::wifiForget(
    const OsApi::WifiForget::Command& command)
{
    if (!networkService_) {
        return Result<OsApi::WifiForget::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto result = networkService_->forget(command.ssid);
    if (result.isError()) {
        return Result<OsApi::WifiForget::Okay, ApiError>::error(ApiError(result.errorValue()));
    }

    return Result<OsApi::WifiForget::Okay, ApiError>::okay(toApiWifiForgetOkay(result.value()));
}

Result<OsApi::WifiScanRequest::Okay, ApiError> OperatingSystemManager::wifiScanRequest(
    const OsApi::WifiScanRequest::Command& /*command*/)
{
    if (!networkService_) {
        return Result<OsApi::WifiScanRequest::Okay, ApiError>::error(
            ApiError("NetworkService is unavailable"));
    }

    const auto result = networkService_->requestScan();
    if (result.isError()) {
        return Result<OsApi::WifiScanRequest::Okay, ApiError>::error(ApiError(result.errorValue()));
    }

    return Result<OsApi::WifiScanRequest::Okay, ApiError>::okay(
        OsApi::WifiScanRequest::Okay{ .accepted = true });
}

std::filesystem::path OperatingSystemManager::getSshUserHomeDir(const std::string& user) const
{
    if (dependencies_.homeDirResolver) {
        return dependencies_.homeDirResolver(user);
    }
    return resolveUserHomeDir(user);
}

Result<std::monostate, ApiError> OperatingSystemManager::applySshPermissions(
    const std::filesystem::path& dirPath,
    const std::filesystem::path& filePath,
    const std::string& user) const
{
    if (dependencies_.sshPermissionsEnsurer) {
        return dependencies_.sshPermissionsEnsurer(dirPath, filePath, user);
    }
    return ensureSshPermissions(dirPath, filePath, user);
}

std::filesystem::path OperatingSystemManager::getPeerAllowlistPath() const
{
    return std::filesystem::path(resolveWorkDir(backendConfig_.workDir)) / "peer-allowlist.json";
}

std::filesystem::path OperatingSystemManager::getPeerClientKeyPath() const
{
    return std::filesystem::path(resolveWorkDir(backendConfig_.workDir)) / "ssh" / "peer_ed25519";
}

Result<std::vector<PeerTrustBundle>, ApiError> OperatingSystemManager::loadPeerAllowlist() const
{
    const auto path = getPeerAllowlistPath();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return Result<std::vector<PeerTrustBundle>, ApiError>::okay({});
    }

    auto readResult = readFileToString(path);
    if (readResult.isError()) {
        return Result<std::vector<PeerTrustBundle>, ApiError>::error(readResult.errorValue());
    }

    if (trimWhitespace(readResult.value()).empty()) {
        return Result<std::vector<PeerTrustBundle>, ApiError>::okay({});
    }

    try {
        const auto json = nlohmann::json::parse(readResult.value());
        if (!json.is_array()) {
            return Result<std::vector<PeerTrustBundle>, ApiError>::error(
                ApiError("Peer allowlist must be a JSON array"));
        }
        auto allowlist = json.get<std::vector<PeerTrustBundle>>();
        return Result<std::vector<PeerTrustBundle>, ApiError>::okay(std::move(allowlist));
    }
    catch (const std::exception& e) {
        return Result<std::vector<PeerTrustBundle>, ApiError>::error(
            ApiError(std::string("Failed to parse allowlist: ") + e.what()));
    }
}

Result<std::monostate, ApiError> OperatingSystemManager::savePeerAllowlist(
    const std::vector<PeerTrustBundle>& allowlist) const
{
    const auto path = getPeerAllowlistPath();
    nlohmann::json json = allowlist;
    const std::string payload = json.dump(2);

    auto result = writeFileLinesAtomic(path, { payload });
    if (result.isError()) {
        return Result<std::monostate, ApiError>::error(result.errorValue());
    }

    std::error_code error;
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        return Result<std::monostate, ApiError>::error(
            ApiError("Failed to set permissions for " + path.string()));
    }

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

Result<std::string, ApiError> OperatingSystemManager::getHostFingerprintSha256() const
{
    const std::filesystem::path hostKeyPath("/etc/ssh/ssh_host_ecdsa_key.pub");
    std::error_code error;
    if (!std::filesystem::exists(hostKeyPath, error) || error) {
        return Result<std::string, ApiError>::error(
            ApiError("Host key not found: " + hostKeyPath.string()));
    }

    const std::string command =
        "ssh-keygen -l -E sha256 -f " + hostKeyPath.string() + " 2>/dev/null";
    auto outputResult = runCommandCapture(command);
    if (outputResult.isError()) {
        return Result<std::string, ApiError>::error(outputResult.errorValue());
    }

    return extractFingerprintSha256(outputResult.value());
}

Result<std::string, ApiError> OperatingSystemManager::getClientKeyFingerprintSha256() const
{
    auto keyPath = getPeerClientKeyPath();
    keyPath += ".pub";

    std::error_code error;
    if (!std::filesystem::exists(keyPath, error) || error) {
        return Result<std::string, ApiError>::error(
            ApiError("Client key not found: " + keyPath.string()));
    }

    const std::string command = "ssh-keygen -l -E sha256 -f " + keyPath.string() + " 2>/dev/null";
    auto outputResult = runCommandCapture(command);
    if (outputResult.isError()) {
        return Result<std::string, ApiError>::error(outputResult.errorValue());
    }

    return extractFingerprintSha256(outputResult.value());
}

Result<std::string, ApiError> OperatingSystemManager::getPeerClientPublicKey(bool* created)
{
    const auto keyPath = getPeerClientKeyPath();
    auto pubPath = keyPath;
    pubPath += ".pub";

    std::error_code error;
    std::filesystem::create_directories(keyPath.parent_path(), error);
    if (error) {
        return Result<std::string, ApiError>::error(
            ApiError("Failed to create key directory: " + keyPath.parent_path().string()));
    }

    std::filesystem::permissions(
        keyPath.parent_path(),
        std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        return Result<std::string, ApiError>::error(
            ApiError("Failed to set permissions for " + keyPath.parent_path().string()));
    }

    bool generated = false;
    const bool hasPrivate = std::filesystem::exists(keyPath, error) && !error;
    const bool hasPublic = std::filesystem::exists(pubPath, error) && !error;
    if (!hasPrivate) {
        const std::string command =
            "ssh-keygen -t ed25519 -f " + keyPath.string() + " -N \"\" -C \"dirtsim\" 2>/dev/null";
        auto result = runCommandCapture(command);
        if (result.isError()) {
            return Result<std::string, ApiError>::error(result.errorValue());
        }
        generated = true;
    }
    else if (!hasPublic) {
        const std::string command = "ssh-keygen -y -f " + keyPath.string() + " 2>/dev/null";
        auto result = runCommandCapture(command);
        if (result.isError()) {
            return Result<std::string, ApiError>::error(result.errorValue());
        }
        std::ofstream pubFile(pubPath);
        if (!pubFile.is_open()) {
            return Result<std::string, ApiError>::error(
                ApiError("Failed to write public key: " + pubPath.string()));
        }
        pubFile << trimWhitespace(result.value()) << "\n";
        pubFile.close();
    }

    std::filesystem::permissions(
        keyPath,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        return Result<std::string, ApiError>::error(
            ApiError("Failed to set permissions for " + keyPath.string()));
    }

    std::filesystem::permissions(
        pubPath,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
            | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace,
        error);
    if (error) {
        return Result<std::string, ApiError>::error(
            ApiError("Failed to set permissions for " + pubPath.string()));
    }

    auto readResult = readFileToString(pubPath);
    if (readResult.isError()) {
        return Result<std::string, ApiError>::error(readResult.errorValue());
    }

    if (created) {
        *created = generated;
    }

    return Result<std::string, ApiError>::okay(trimWhitespace(readResult.value()));
}

Result<PeerTrustBundle, ApiError> OperatingSystemManager::buildTrustBundle(bool* created)
{
    bool keyCreated = false;
    auto publicKeyResult = getPeerClientPublicKey(&keyCreated);
    if (publicKeyResult.isError()) {
        return Result<PeerTrustBundle, ApiError>::error(publicKeyResult.errorValue());
    }

    auto fingerprintResult = getHostFingerprintSha256();
    if (fingerprintResult.isError()) {
        return Result<PeerTrustBundle, ApiError>::error(fingerprintResult.errorValue());
    }

    char hostname[256] = "dirtsim";
    gethostname(hostname, sizeof(hostname));

    PeerTrustBundle bundle;
    bundle.host = hostname;
    bundle.ssh_user = "dirtsim";
    bundle.ssh_port = 22;
    bundle.host_fingerprint_sha256 = fingerprintResult.value();
    bundle.client_pubkey = publicKeyResult.value();

    if (created) {
        *created = keyCreated;
    }

    return Result<PeerTrustBundle, ApiError>::okay(bundle);
}

Result<OsApi::PeerClientKeyEnsure::Okay, ApiError> OperatingSystemManager::ensurePeerClientKey()
{
    bool created = false;
    auto publicKeyResult = getPeerClientPublicKey(&created);
    if (publicKeyResult.isError()) {
        return Result<OsApi::PeerClientKeyEnsure::Okay, ApiError>::error(
            publicKeyResult.errorValue());
    }

    auto fingerprintResult = getClientKeyFingerprintSha256();
    if (fingerprintResult.isError()) {
        return Result<OsApi::PeerClientKeyEnsure::Okay, ApiError>::error(
            fingerprintResult.errorValue());
    }

    OsApi::PeerClientKeyEnsure::Okay okay;
    okay.created = created;
    okay.public_key = publicKeyResult.value();
    okay.fingerprint_sha256 = fingerprintResult.value();
    return Result<OsApi::PeerClientKeyEnsure::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::RemoteCliRun::Okay, ApiError> OperatingSystemManager::remoteCliRun(
    const OsApi::RemoteCliRun::Command& command)
{
    if (command.host.empty()) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(ApiError("Host is required"));
    }

    const auto allowlistPath = getPeerAllowlistPath();
    std::error_code error;
    if (!std::filesystem::exists(allowlistPath, error) || error) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Peer allowlist not found"));
    }

    auto allowlistResult = loadPeerAllowlist();
    if (allowlistResult.isError()) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(allowlistResult.errorValue());
    }

    const auto& allowlist = allowlistResult.value();
    auto it =
        std::find_if(allowlist.begin(), allowlist.end(), [&command](const PeerTrustBundle& entry) {
            return entry.host == command.host;
        });
    if (it == allowlist.end()) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Host not found in allowlist"));
    }

    if (!dependencies_.remoteCliRunner) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Remote CLI runner not configured"));
    }

    int timeoutMs = kDefaultRemoteCommandTimeoutMs;
    if (command.timeout_ms.has_value() && command.timeout_ms.value() > 0) {
        timeoutMs = command.timeout_ms.value();
    }

    std::vector<std::string> argv;
    argv.reserve(command.args.size() + 1);
    argv.push_back("dirtsim-cli");
    for (const auto& arg : command.args) {
        argv.push_back(arg);
    }

    auto result = dependencies_.remoteCliRunner(*it, argv, timeoutMs);
    if (result.isError()) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(result.errorValue());
    }

    if (isMissingCliResult(result.value())) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("dirtsim-cli not found on remote host"));
    }

    if (result.value().stdout.size() > RemoteSshExecutor::kMaxStdoutBytes
        || result.value().stderr.size() > RemoteSshExecutor::kMaxStderrBytes) {
        return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
            ApiError("Remote CLI output exceeded limit"));
    }

    return Result<OsApi::RemoteCliRun::Okay, ApiError>::okay(result.value());
}

OperatingSystemManager::ScannerModeStatusInfo OperatingSystemManager::
    readScannerModeStatusInternal() const
{
    ScannerModeStatusInfo info;

    std::error_code error;
    const bool helperExists = std::filesystem::exists(kScannerModeHelperPath, error) && !error;
    error.clear();
    const bool nexutilExists = std::filesystem::exists(kScannerNexutilPath, error) && !error;
    error.clear();
    const bool nexmonModuleExists = std::filesystem::exists(kNexmonModulePath, error) && !error;
    error.clear();
    const bool nexmonFirmwareExists = std::filesystem::exists(kNexmonFirmwarePath, error) && !error;

    info.available = helperExists && nexutilExists && nexmonModuleExists && nexmonFirmwareExists;
    if (!info.available) {
        info.detail = buildMissingScannerDetail(
            helperExists, nexutilExists, nexmonModuleExists, nexmonFirmwareExists);
        return info;
    }

    auto statusOutputResult = runScannerHelperCommand("status", kScannerHelperStatusTimeoutMs);
    if (statusOutputResult.isError()) {
        info.available = false;
        info.detail =
            "Scanner mode status is unavailable: " + statusOutputResult.errorValue().message;
        return info;
    }

    const auto parsed = parseScannerModeStatusOutput(statusOutputResult.value());
    info.stack_nexmon = parsed.stackNexmon;

    error.clear();
    const bool scannerStateFileExists =
        std::filesystem::exists(std::filesystem::path(kScannerStateFile), error) && !error;
    info.active = info.stack_nexmon && scannerStateFileExists;

    if (info.active) {
        info.detail = "Scanner mode active on wlan0. Return to Wi-Fi to restore normal "
                      "networking.";
    }
    else if (info.stack_nexmon) {
        info.detail = "Experimental scanner stack is loaded, but scanner mode is inactive.";
    }
    else {
        info.detail = "Scanner mode available. Enter scanner mode to survey channels on wlan0.";
    }

    if (info.stack_nexmon && !parsed.monitorSupported) {
        info.detail = "Nexmon stack loaded, but monitor support is unavailable.";
    }

    return info;
}

Result<OperatingSystemManager::ScannerModeRuntimeState, ApiError> OperatingSystemManager::
    readScannerModeState() const
{
    std::error_code error;
    const bool exists =
        std::filesystem::exists(std::filesystem::path(kScannerStateFile), error) && !error;
    if (!exists) {
        return Result<ScannerModeRuntimeState, ApiError>::okay(ScannerModeRuntimeState{});
    }

    std::ifstream file(kScannerStateFile);
    if (!file.is_open()) {
        return Result<ScannerModeRuntimeState, ApiError>::error(
            ApiError("Failed to read scanner state"));
    }

    nlohmann::json json;
    try {
        file >> json;
    }
    catch (const std::exception& e) {
        return Result<ScannerModeRuntimeState, ApiError>::error(
            ApiError(std::string("Failed to parse scanner state: ") + e.what()));
    }

    ScannerModeRuntimeState state;
    state.active = json.value("active", false);
    if (json.contains("restore_ssid") && !json.at("restore_ssid").is_null()) {
        state.restoreSsid = json.at("restore_ssid").get<std::string>();
    }

    return Result<ScannerModeRuntimeState, ApiError>::okay(std::move(state));
}

Result<std::monostate, ApiError> OperatingSystemManager::setScannerModeState(
    const ScannerModeRuntimeState& state) const
{
    std::error_code error;
    if (state.active) {
        std::filesystem::create_directories(kScannerStateDir, error);
        if (error) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Failed to create scanner state directory"));
        }

        std::ofstream file(kScannerStateFile, std::ios::trunc);
        if (!file.is_open()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Failed to write scanner state"));
        }

        nlohmann::json json = {
            { "active", true },
            { "restore_ssid",
              state.restoreSsid.has_value() ? nlohmann::json(*state.restoreSsid)
                                            : nlohmann::json(nullptr) },
        };
        file << json.dump() << '\n';
        return Result<std::monostate, ApiError>::okay(std::monostate{});
    }

    std::filesystem::remove(kScannerStateFile, error);
    if (error) {
        return Result<std::monostate, ApiError>::error(ApiError("Failed to clear scanner state"));
    }

    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

Result<ProcessRunResult, ApiError> OperatingSystemManager::runProcessCapture(
    const std::vector<std::string>& argv, int timeoutMs) const
{
    if (!dependencies_.processRunner) {
        return Result<ProcessRunResult, ApiError>::error(
            ApiError("Missing dependency for processRunner"));
    }

    const auto result = dependencies_.processRunner(argv, timeoutMs);
    if (result.isError()) {
        return Result<ProcessRunResult, ApiError>::error(ApiError(result.errorValue()));
    }

    return Result<ProcessRunResult, ApiError>::okay(result.value());
}

Result<std::string, ApiError> OperatingSystemManager::runScannerHelperCommand(
    const std::string& action, int timeoutMs) const
{
    const auto result = runProcessCapture({ kScannerModeHelperPath, action }, timeoutMs);
    if (result.isError()) {
        return Result<std::string, ApiError>::error(
            ApiError("Scanner helper " + action + " failed: " + result.errorValue().message));
    }

    const auto& process = result.value();
    const std::string output = trimWhitespace(process.output);
    const std::string outputSummary = summarizeCommandOutput(output);
    if (process.exitCode != 0) {
        std::string message =
            "Scanner helper " + action + " failed (exit " + std::to_string(process.exitCode) + ")";
        if (!outputSummary.empty()) {
            message += ": " + outputSummary;
        }
        return Result<std::string, ApiError>::error(ApiError(message));
    }

    if (!outputSummary.empty()) {
        LOG_INFO(State, "Scanner helper {}: {}", action, outputSummary);
    }
    else {
        LOG_INFO(State, "Scanner helper {} completed.", action);
    }

    return Result<std::string, ApiError>::okay(output);
}

Result<std::monostate, ApiError> OperatingSystemManager::restoreWifiAfterScannerMode(
    const std::optional<std::string>& restoreSsid) const
{
    if (!restoreSsid.has_value() || restoreSsid->empty()) {
        return Result<std::monostate, ApiError>::okay(std::monostate{});
    }
    if (!networkService_) {
        return makeMissingDependencyError("networkService");
    }

    LOG_INFO(State, "Restoring Wi-Fi after scanner mode to '{}'.", *restoreSsid);
    logNetworkSnapshot(networkService_.get(), "Scanner restore initial snapshot", restoreSsid);

    std::string lastError = "Timed out waiting for Wi-Fi restore";
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string attemptLabel =
            "Scanner restore attempt " + std::to_string(attempt + 1) + "/3";
        std::this_thread::sleep_for(std::chrono::seconds(attempt == 0 ? 2 : 3));

        logNetworkSnapshot(
            networkService_.get(), attemptLabel + " pre-connect snapshot", restoreSsid);
        const auto connectResult = networkService_->connectBySsid(*restoreSsid, std::nullopt);
        if (!connectResult.isError()) {
            LOG_INFO(State, "{} succeeded for '{}'.", attemptLabel, *restoreSsid);
            logNetworkSnapshot(
                networkService_.get(), attemptLabel + " post-success snapshot", restoreSsid);
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        }

        lastError = connectResult.errorValue();
        LOG_WARN(State, "{} failed for '{}': {}", attemptLabel, *restoreSsid, lastError);
        logNetworkSnapshot(
            networkService_.get(), attemptLabel + " post-failure snapshot", restoreSsid);
        if (networkService_) {
            const auto scanResult = networkService_->requestScan();
            if (scanResult.isError()) {
                SLOG_WARN(
                    "Failed to request Wi-Fi scan after restore attempt: {}",
                    scanResult.errorValue());
            }
            else {
                LOG_INFO(State, "{} requested Wi-Fi scan.", attemptLabel);
            }
        }
    }

    return Result<std::monostate, ApiError>::error(
        ApiError("Failed to restore Wi-Fi connection to '" + *restoreSsid + "': " + lastError));
}

void OperatingSystemManager::bestEffortRestoreWifiFromScannerMode() const
{
    const auto exitResult = runScannerHelperCommand("exit", kScannerHelperExitTimeoutMs);
    if (exitResult.isError()) {
        SLOG_WARN("Best-effort scanner restore failed: {}", exitResult.errorValue().message);
    }

    if (networkService_) {
        const auto scanResult = networkService_->requestScan();
        if (scanResult.isError()) {
            SLOG_WARN(
                "Failed to request Wi-Fi scan during best-effort restore: {}",
                scanResult.errorValue());
        }
    }

    const auto stateResult = setScannerModeState(ScannerModeRuntimeState{});
    if (stateResult.isError()) {
        SLOG_WARN("Failed to clear scanner runtime state: {}", stateResult.errorValue().message);
    }
}

Result<OsApi::ScannerModeEnter::Okay, ApiError> OperatingSystemManager::enterScannerMode()
{
    LOG_INFO(State, "Entering scanner mode.");
    const auto status = readScannerModeStatusInternal();
    if (!status.available) {
        return Result<OsApi::ScannerModeEnter::Okay, ApiError>::error(ApiError(status.detail));
    }

    if (status.active) {
        OsApi::ScannerModeEnter::Okay okay;
        okay.active = true;
        okay.detail = "Scanner mode already active.";
        return Result<OsApi::ScannerModeEnter::Okay, ApiError>::okay(std::move(okay));
    }

    std::optional<std::string> restoreSsid;
    if (networkService_) {
        const auto snapshotResult = networkService_->getSnapshot(true);
        if (snapshotResult.isValue() && snapshotResult.value().status.connected
            && !snapshotResult.value().status.ssid.empty()) {
            restoreSsid = snapshotResult.value().status.ssid;
        }
        else if (snapshotResult.isError()) {
            SLOG_WARN(
                "Failed to refresh network snapshot before scanner mode: {}",
                snapshotResult.errorValue());
        }
    }

    if (restoreSsid.has_value()) {
        LOG_INFO(State, "Scanner mode will restore Wi-Fi SSID '{}'.", *restoreSsid);
    }
    else {
        LOG_INFO(State, "Scanner mode has no Wi-Fi SSID to restore.");
    }

    const auto enterResult = runScannerHelperCommand("enter", kScannerHelperEnterTimeoutMs);
    if (enterResult.isError()) {
        bestEffortRestoreWifiFromScannerMode();
        return Result<OsApi::ScannerModeEnter::Okay, ApiError>::error(enterResult.errorValue());
    }

    const auto stateResult =
        setScannerModeState(ScannerModeRuntimeState{ .active = true, .restoreSsid = restoreSsid });
    if (stateResult.isError()) {
        bestEffortRestoreWifiFromScannerMode();
        return Result<OsApi::ScannerModeEnter::Okay, ApiError>::error(stateResult.errorValue());
    }

    std::optional<std::string> captureError;
    if (scannerService_) {
        const auto captureStartResult = scannerService_->start();
        if (captureStartResult.isError()) {
            captureError = captureStartResult.errorValue();
            SLOG_WARN("Scanner capture failed to start: {}", *captureError);
        }
    }

    OsApi::ScannerModeEnter::Okay okay;
    okay.active = true;
    if (captureError.has_value()) {
        LOG_WARN(State, "Scanner mode entered, but capture failed: {}", *captureError);
        okay.detail = "Scanner mode active on wlan0, but capture failed: " + *captureError;
        publishScannerSnapshotChanged(true, okay.detail);
    }
    else {
        LOG_INFO(State, "Scanner mode entered successfully.");
        okay.detail = "Scanner mode active on wlan0.";
    }
    return Result<OsApi::ScannerModeEnter::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::ScannerModeExit::Okay, ApiError> OperatingSystemManager::exitScannerMode()
{
    LOG_INFO(State, "Exiting scanner mode.");
    if (scannerService_) {
        scannerService_->stop();
    }

    std::optional<std::string> restoreSsid;
    const auto runtimeStateResult = readScannerModeState();
    if (runtimeStateResult.isValue()) {
        restoreSsid = runtimeStateResult.value().restoreSsid;
    }
    else {
        SLOG_WARN(
            "Failed to read scanner runtime state: {}", runtimeStateResult.errorValue().message);
    }

    const auto status = readScannerModeStatusInternal();
    if (!status.stack_nexmon && !status.active) {
        const auto clearStateResult = setScannerModeState(ScannerModeRuntimeState{});
        if (clearStateResult.isError()) {
            SLOG_WARN(
                "Failed to clear scanner runtime state: {}", clearStateResult.errorValue().message);
        }

        OsApi::ScannerModeExit::Okay okay;
        okay.active = false;
        okay.detail = "Normal Wi-Fi mode already active.";
        publishScannerSnapshotChanged(false, okay.detail);
        return Result<OsApi::ScannerModeExit::Okay, ApiError>::okay(std::move(okay));
    }

    const auto exitResult = runScannerHelperCommand("exit", kScannerHelperExitTimeoutMs);
    if (exitResult.isError()) {
        return Result<OsApi::ScannerModeExit::Okay, ApiError>::error(exitResult.errorValue());
    }
    logNetworkSnapshot(networkService_.get(), "After scanner helper exit", restoreSsid);

    if (networkService_) {
        const auto scanResult = networkService_->requestScan();
        if (scanResult.isError()) {
            SLOG_WARN(
                "Failed to request Wi-Fi scan after scanner exit: {}", scanResult.errorValue());
        }
        else {
            LOG_INFO(State, "Requested Wi-Fi scan after scanner exit.");
        }
    }
    logNetworkSnapshot(networkService_.get(), "After scanner exit scan request", restoreSsid);

    const auto restoreResult = restoreWifiAfterScannerMode(restoreSsid);
    if (restoreResult.isError()) {
        return Result<OsApi::ScannerModeExit::Okay, ApiError>::error(restoreResult.errorValue());
    }

    const auto stateResult = setScannerModeState(ScannerModeRuntimeState{});
    if (stateResult.isError()) {
        return Result<OsApi::ScannerModeExit::Okay, ApiError>::error(stateResult.errorValue());
    }

    OsApi::ScannerModeExit::Okay okay;
    okay.active = false;
    if (restoreSsid.has_value() && !restoreSsid->empty()) {
        LOG_INFO(State, "Scanner mode exited. Restored Wi-Fi SSID '{}'.", *restoreSsid);
        okay.detail = "Restored normal Wi-Fi connection to '" + *restoreSsid + "'.";
    }
    else {
        LOG_INFO(State, "Scanner mode exited. Restored normal Wi-Fi mode.");
        okay.detail = "Restored normal Wi-Fi mode.";
    }
    publishScannerSnapshotChanged(false, okay.detail);
    return Result<OsApi::ScannerModeExit::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::TrustBundleGet::Okay, ApiError> OperatingSystemManager::getTrustBundle()
{
    bool created = false;
    auto bundleResult = buildTrustBundle(&created);
    if (bundleResult.isError()) {
        return Result<OsApi::TrustBundleGet::Okay, ApiError>::error(bundleResult.errorValue());
    }

    OsApi::TrustBundleGet::Okay okay;
    okay.bundle = bundleResult.value();
    okay.client_key_created = created;
    return Result<OsApi::TrustBundleGet::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::TrustPeer::Okay, ApiError> OperatingSystemManager::trustPeer(
    const OsApi::TrustPeer::Command& command)
{
    PeerTrustBundle bundle = command.bundle;
    // We always manage local authorized_keys for the fixed local account.
    // The bundle's ssh_user is only used as the *remote* SSH login user for outbound commands.
    static constexpr const char* kLocalAuthorizedKeysUser = "dirtsim";
    if (bundle.host.empty()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(ApiError("Host is required"));
    }
    if (bundle.host_fingerprint_sha256.empty()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(
            ApiError("Host fingerprint is required"));
    }
    if (bundle.client_pubkey.empty()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(
            ApiError("Client public key is required"));
    }
    auto normalizedPubKeyResult = normalizeAuthorizedKeyLine(bundle.client_pubkey);
    if (normalizedPubKeyResult.isError()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(normalizedPubKeyResult.errorValue());
    }
    bundle.client_pubkey = normalizedPubKeyResult.value();
    if (bundle.ssh_user.empty()) {
        bundle.ssh_user = "dirtsim";
    }
    if (bundle.ssh_port == 0) {
        bundle.ssh_port = 22;
    }

    auto allowlistResult = loadPeerAllowlist();
    if (allowlistResult.isError()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(allowlistResult.errorValue());
    }

    auto allowlist = allowlistResult.value();
    bool allowlistUpdated = false;
    auto it =
        std::find_if(allowlist.begin(), allowlist.end(), [&bundle](const PeerTrustBundle& entry) {
            return entry.host == bundle.host;
        });

    if (it == allowlist.end()) {
        allowlist.push_back(bundle);
        allowlistUpdated = true;
    }
    else {
        if (it->ssh_user != bundle.ssh_user || it->ssh_port != bundle.ssh_port
            || it->host_fingerprint_sha256 != bundle.host_fingerprint_sha256
            || it->client_pubkey != bundle.client_pubkey) {
            *it = bundle;
            allowlistUpdated = true;
        }
    }

    if (allowlistUpdated) {
        auto saveResult = savePeerAllowlist(allowlist);
        if (saveResult.isError()) {
            return Result<OsApi::TrustPeer::Okay, ApiError>::error(saveResult.errorValue());
        }
    }

    const std::string sshUser = kLocalAuthorizedKeysUser;
    const auto homeDir = getSshUserHomeDir(sshUser);
    if (homeDir.empty()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(
            ApiError("Failed to resolve home directory for " + sshUser));
    }

    const std::filesystem::path sshDir = homeDir / ".ssh";
    const std::filesystem::path authorizedKeys = sshDir / "authorized_keys";

    auto keyBodyResult = extractKeyBody(bundle.client_pubkey);
    if (keyBodyResult.isError()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(keyBodyResult.errorValue());
    }

    std::error_code error;
    std::filesystem::create_directories(sshDir, error);
    if (error) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(
            ApiError("Failed to create " + sshDir.string()));
    }

    std::vector<std::string> lines;
    bool keyPresent = false;
    if (std::filesystem::exists(authorizedKeys, error) && !error) {
        auto readResult = readFileLines(authorizedKeys);
        if (readResult.isError()) {
            return Result<OsApi::TrustPeer::Okay, ApiError>::error(readResult.errorValue());
        }
        lines = readResult.value();

        for (const auto& line : lines) {
            const auto trimmed = trimWhitespace(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                continue;
            }
            auto bodyResult = extractKeyBody(trimmed);
            if (bodyResult.isError()) {
                continue;
            }
            if (bodyResult.value() == keyBodyResult.value()) {
                keyPresent = true;
                break;
            }
        }
    }

    bool keyAdded = false;
    if (!keyPresent) {
        lines.push_back(bundle.client_pubkey);
        keyAdded = true;
        auto writeResult = writeFileLinesAtomic(authorizedKeys, lines);
        if (writeResult.isError()) {
            return Result<OsApi::TrustPeer::Okay, ApiError>::error(writeResult.errorValue());
        }
    }

    if (!std::filesystem::exists(authorizedKeys, error) || error) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(
            ApiError("authorized_keys is missing after update"));
    }

    auto permsResult = applySshPermissions(sshDir, authorizedKeys, sshUser);
    if (permsResult.isError()) {
        return Result<OsApi::TrustPeer::Okay, ApiError>::error(permsResult.errorValue());
    }

    OsApi::TrustPeer::Okay okay;
    okay.allowlist_updated = allowlistUpdated;
    okay.authorized_key_added = keyAdded;
    return Result<OsApi::TrustPeer::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::UntrustPeer::Okay, ApiError> OperatingSystemManager::untrustPeer(
    const OsApi::UntrustPeer::Command& command)
{
    // We always manage local authorized_keys for the fixed local account.
    // The allowlist entry's ssh_user is only used as the *remote* SSH login user for outbound
    // commands.
    static constexpr const char* kLocalAuthorizedKeysUser = "dirtsim";
    if (command.host.empty()) {
        return Result<OsApi::UntrustPeer::Okay, ApiError>::error(ApiError("Host is required"));
    }

    auto allowlistResult = loadPeerAllowlist();
    if (allowlistResult.isError()) {
        return Result<OsApi::UntrustPeer::Okay, ApiError>::error(allowlistResult.errorValue());
    }

    auto allowlist = allowlistResult.value();
    auto it =
        std::find_if(allowlist.begin(), allowlist.end(), [&command](const PeerTrustBundle& entry) {
            return entry.host == command.host;
        });

    if (it == allowlist.end()) {
        return Result<OsApi::UntrustPeer::Okay, ApiError>::error(
            ApiError("Peer not found in allowlist"));
    }

    PeerTrustBundle removed = *it;
    allowlist.erase(it);

    auto saveResult = savePeerAllowlist(allowlist);
    if (saveResult.isError()) {
        return Result<OsApi::UntrustPeer::Okay, ApiError>::error(saveResult.errorValue());
    }

    const std::string sshUser = kLocalAuthorizedKeysUser;
    const auto homeDir = getSshUserHomeDir(sshUser);
    if (homeDir.empty()) {
        return Result<OsApi::UntrustPeer::Okay, ApiError>::error(
            ApiError("Failed to resolve home directory for " + sshUser));
    }

    const std::filesystem::path sshDir = homeDir / ".ssh";
    const std::filesystem::path authorizedKeys = sshDir / "authorized_keys";

    bool keyRemoved = false;
    std::error_code error;
    if (std::filesystem::exists(authorizedKeys, error) && !error) {
        auto keyBodyResult = extractKeyBody(removed.client_pubkey);
        if (keyBodyResult.isError()) {
            return Result<OsApi::UntrustPeer::Okay, ApiError>::error(keyBodyResult.errorValue());
        }

        auto readResult = readFileLines(authorizedKeys);
        if (readResult.isError()) {
            return Result<OsApi::UntrustPeer::Okay, ApiError>::error(readResult.errorValue());
        }

        std::vector<std::string> filtered;
        filtered.reserve(readResult.value().size());
        for (const auto& line : readResult.value()) {
            const auto trimmed = trimWhitespace(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                filtered.push_back(line);
                continue;
            }
            auto bodyResult = extractKeyBody(trimmed);
            if (bodyResult.isError()) {
                filtered.push_back(line);
                continue;
            }
            if (bodyResult.value() == keyBodyResult.value()) {
                keyRemoved = true;
                continue;
            }
            filtered.push_back(line);
        }

        if (keyRemoved) {
            auto writeResult = writeFileLinesAtomic(authorizedKeys, filtered);
            if (writeResult.isError()) {
                return Result<OsApi::UntrustPeer::Okay, ApiError>::error(writeResult.errorValue());
            }
        }
    }

    OsApi::UntrustPeer::Okay okay;
    okay.allowlist_removed = true;
    okay.authorized_key_removed = keyRemoved;
    return Result<OsApi::UntrustPeer::Okay, ApiError>::okay(std::move(okay));
}

Result<OsApi::WebSocketAccessSet::Okay, ApiError> OperatingSystemManager::setWebSocketAccess(
    bool enabled)
{
    constexpr int kTimeoutMs = 2000;
    const std::string token = enabled ? webSocketToken_ : "";

    auto setServerAccess = [](bool accessEnabled,
                              const std::string& accessToken,
                              int timeoutMs) -> Result<std::monostate, ApiError> {
        Network::WebSocketService client;
        const std::string address = "ws://localhost:8080";
        auto connectResult = client.connect(address, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Failed to connect to server: " + connectResult.errorValue()));
        }

        Api::WebSocketAccessSet::Command cmd{ .enabled = accessEnabled, .token = accessToken };
        auto response =
            client.sendCommandAndGetResponse<Api::WebSocketAccessSet::Okay>(cmd, timeoutMs);
        client.disconnect();

        if (response.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Server WebSocketAccessSet failed: " + response.errorValue()));
        }
        const auto inner = response.value();
        if (inner.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Server WebSocketAccessSet failed: " + inner.errorValue().message));
        }

        return Result<std::monostate, ApiError>::okay(std::monostate{});
    };

    auto setUiAccess = [](bool accessEnabled,
                          const std::string& accessToken,
                          int timeoutMs) -> Result<std::monostate, ApiError> {
        Network::WebSocketService client;
        const std::string address = "ws://localhost:7070";
        auto connectResult = client.connect(address, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Failed to connect to UI: " + connectResult.errorValue()));
        }

        UiApi::WebSocketAccessSet::Command cmd{ .enabled = accessEnabled, .token = accessToken };
        auto response =
            client.sendCommandAndGetResponse<UiApi::WebSocketAccessSet::Okay>(cmd, timeoutMs);
        client.disconnect();

        if (response.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("UI WebSocketAccessSet failed: " + response.errorValue()));
        }
        const auto inner = response.value();
        if (inner.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("UI WebSocketAccessSet failed: " + inner.errorValue().message));
        }

        return Result<std::monostate, ApiError>::okay(std::monostate{});
    };

    if (!enabled && webUiEnabled_) {
        const auto webUiResult = setWebUiAccess(false);
        if (webUiResult.isError()) {
            return Result<OsApi::WebSocketAccessSet::Okay, ApiError>::error(
                webUiResult.errorValue());
        }
    }

    const auto serverResult = setServerAccess(enabled, token, kTimeoutMs);
    if (serverResult.isError()) {
        return Result<OsApi::WebSocketAccessSet::Okay, ApiError>::error(serverResult.errorValue());
    }

    const auto uiResult = setUiAccess(enabled, token, kTimeoutMs);
    if (uiResult.isError()) {
        if (enabled) {
            setServerAccess(false, "", kTimeoutMs);
        }
        return Result<OsApi::WebSocketAccessSet::Okay, ApiError>::error(uiResult.errorValue());
    }

    webSocketEnabled_ = enabled;
    setPeerAdvertisementEnabled(enabled);

    OsApi::WebSocketAccessSet::Okay okay;
    okay.enabled = enabled;
    okay.token = enabled ? webSocketToken_ : "";
    return Result<OsApi::WebSocketAccessSet::Okay, ApiError>::okay(std::move(okay));
}

void OperatingSystemManager::setPeerAdvertisementEnabled(bool enabled)
{
    if (!serverPeerAdvertisement_ || !uiPeerAdvertisement_) {
        return;
    }

    const auto [serverPort, uiPort] = computePeerAdvertisementPorts();

    if (enabled) {
        const std::string serverServiceName =
            peerServiceName_.empty() ? "dirtsim" : peerServiceName_;
        serverPeerAdvertisement_->setServiceName(serverServiceName);
        serverPeerAdvertisement_->setPort(serverPort);
        serverPeerAdvertisement_->setRole(PeerRole::Physics);
        if (!serverPeerAdvertisement_->start()) {
            LOG_WARN(Network, "PeerAdvertisement failed to start for server");
        }

        const std::string uiServiceName =
            peerUiServiceName_.empty() ? "dirtsim-ui" : peerUiServiceName_;
        uiPeerAdvertisement_->setServiceName(uiServiceName);
        uiPeerAdvertisement_->setPort(uiPort);
        uiPeerAdvertisement_->setRole(PeerRole::Ui);
        if (!uiPeerAdvertisement_->start()) {
            LOG_WARN(Network, "PeerAdvertisement failed to start for UI");
        }
        return;
    }

    serverPeerAdvertisement_->stop();
    uiPeerAdvertisement_->stop();
}

std::pair<uint16_t, uint16_t> OperatingSystemManager::computePeerAdvertisementPorts() const
{
    auto parsePortValue = [](const std::string& text, uint16_t defaultPort) -> uint16_t {
        if (text.empty()) {
            return defaultPort;
        }
        try {
            const int value = std::stoi(text);
            if (value <= 0 || value > 65535) {
                return defaultPort;
            }
            return static_cast<uint16_t>(value);
        }
        catch (const std::exception&) {
            return defaultPort;
        }
    };

    auto findPortToken = [](const std::string& args) -> std::optional<std::string> {
        if (args.empty()) {
            return std::nullopt;
        }

        std::istringstream iss(args);
        std::string token;
        while (iss >> token) {
            if (token == "-p" || token == "--port") {
                std::string port;
                if (iss >> port) {
                    return port;
                }
            }
            if (token.rfind("-p", 0) == 0 && token.size() > 2) {
                return token.substr(2);
            }
            if (token.rfind("--port=", 0) == 0 && token.size() > 7) {
                return token.substr(7);
            }
        }
        return std::nullopt;
    };

    // Server websocket port is configurable via args/env (DIRTSIM_SERVER_ARGS). For local-backend,
    // match the same defaulting behavior used when launching processes.
    const std::string serverArgs =
        backendConfig_.serverArgs.empty() ? "-p 8080" : backendConfig_.serverArgs;
    const uint16_t serverPort = parsePortValue(resolveServerPort(serverArgs), 8080);

    // UI websocket port defaults to 7070 (CLI->UI websocket). If the UI ever gains a port flag,
    // honor it here.
    const uint16_t uiPort = parsePortValue(findPortToken(backendConfig_.uiArgs).value_or(""), 7070);

    return { serverPort, uiPort };
}

Result<OsApi::WebUiAccessSet::Okay, ApiError> OperatingSystemManager::setWebUiAccess(bool enabled)
{
    constexpr int kTimeoutMs = 2000;

    if (enabled && !webSocketEnabled_) {
        const auto webSocketResult = setWebSocketAccess(true);
        if (webSocketResult.isError()) {
            return Result<OsApi::WebUiAccessSet::Okay, ApiError>::error(
                webSocketResult.errorValue());
        }
    }

    auto setServerAccess = [](bool accessEnabled,
                              int timeoutMs) -> Result<std::monostate, ApiError> {
        Network::WebSocketService client;
        const std::string address = "ws://localhost:8080";
        auto connectResult = client.connect(address, timeoutMs);
        if (connectResult.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Failed to connect to server: " + connectResult.errorValue()));
        }

        Api::WebUiAccessSet::Command cmd{ .enabled = accessEnabled, .token = "" };
        auto response = client.sendCommandAndGetResponse<Api::WebUiAccessSet::Okay>(cmd, timeoutMs);
        client.disconnect();

        if (response.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Server WebUiAccessSet failed: " + response.errorValue()));
        }
        const auto inner = response.value();
        if (inner.isError()) {
            return Result<std::monostate, ApiError>::error(
                ApiError("Server WebUiAccessSet failed: " + inner.errorValue().message));
        }

        return Result<std::monostate, ApiError>::okay(std::monostate{});
    };

    const auto serverResult = setServerAccess(enabled, kTimeoutMs);
    if (serverResult.isError()) {
        return Result<OsApi::WebUiAccessSet::Okay, ApiError>::error(serverResult.errorValue());
    }

    webUiEnabled_ = enabled;

    OsApi::WebUiAccessSet::Okay okay;
    okay.enabled = enabled;
    okay.token = webSocketEnabled_ ? webSocketToken_ : "";
    return Result<OsApi::WebUiAccessSet::Okay, ApiError>::okay(std::move(okay));
}

void OperatingSystemManager::publishNetworkSnapshotChanged(const NetworkService::Snapshot& snapshot)
{
    if (!enableNetworking_) {
        return;
    }

    const OsApi::NetworkSnapshotChanged event{
        .snapshot = toApiNetworkSnapshotOkay(snapshot),
    };
    const Network::MessageEnvelope envelope{
        .id = 0,
        .message_type = std::string(OsApi::NetworkSnapshotChanged::name()),
        .payload = Network::serialize_payload(event),
    };
    wsService_.broadcastBinary(Network::serialize_envelope(envelope));
}

void OperatingSystemManager::publishScannerSnapshotChanged(
    bool active, const std::optional<std::string>& detailOverride)
{
    if (!enableNetworking_ || !scannerService_) {
        return;
    }

    const auto snapshot =
        scannerService_->snapshot(kScannerPushSnapshotMaxAgeMs, kScannerPushSnapshotMaxRadios);
    const OsApi::ScannerSnapshotChanged event{
        .snapshot = toApiScannerSnapshotOkay(
            snapshot, scannerService_->lastError(), active, detailOverride),
    };
    const Network::MessageEnvelope envelope{
        .id = 0,
        .message_type = std::string(OsApi::ScannerSnapshotChanged::name()),
        .payload = Network::serialize_payload(event),
    };
    wsService_.broadcastBinary(Network::serialize_envelope(envelope));
}

OperatingSystemManager::DiskStats OperatingSystemManager::getDiskStats(
    const std::string& path) const
{
    struct statvfs buf;
    if (statvfs(path.c_str(), &buf) != 0) {
        return DiskStats{};
    }

    const uint64_t blockSize = buf.f_frsize > 0 ? buf.f_frsize : buf.f_bsize;
    DiskStats stats;
    stats.total_bytes = static_cast<uint64_t>(buf.f_blocks) * blockSize;
    stats.free_bytes = static_cast<uint64_t>(buf.f_bavail) * blockSize;
    return stats;
}

std::string OperatingSystemManager::getAudioHealth(int timeoutMs)
{
    Network::WebSocketService client;
    const auto disconnectIfConnected = [&client]() {
        if (client.isConnected()) {
            client.disconnect();
        }
    };

    try {
        const std::string address = "ws://localhost:6060";
        auto connectResult = client.connect(address, timeoutMs);
        if (connectResult.isError()) {
            return "Error: " + connectResult.errorValue();
        }

        AudioApi::StatusGet::Command statusCmd{};
        auto statusResult =
            client.sendCommandAndGetResponse<AudioApi::StatusGet::Okay>(statusCmd, timeoutMs);
        disconnectIfConnected();

        if (statusResult.isError()) {
            return "Error: " + statusResult.errorValue();
        }

        const auto& response = statusResult.value();
        if (response.isError()) {
            return "Error: " + response.errorValue().message;
        }

        return "OK";
    }
    catch (const std::exception& e) {
        disconnectIfConnected();
        return "Error: " + std::string(e.what());
    }
    catch (...) {
        disconnectIfConnected();
        return "Error: unknown audio health exception";
    }
}

std::string OperatingSystemManager::getServerHealth(int timeoutMs)
{
    Network::WebSocketService client;
    const std::string address = "ws://localhost:8080";
    auto connectResult = client.connect(address, timeoutMs);
    if (connectResult.isError()) {
        return "Error: " + connectResult.errorValue();
    }

    Api::StatusGet::Command statusCmd{};
    auto statusResult =
        client.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, timeoutMs);
    client.disconnect();

    if (statusResult.isError()) {
        return "Error: " + statusResult.errorValue();
    }

    const auto& response = statusResult.value();
    if (response.isError()) {
        return "Error: " + response.errorValue().message;
    }

    const auto& okay = response.value();
    if (okay.state == "Error") {
        if (!okay.error_message.empty()) {
            return "Error: " + okay.error_message;
        }
        return "Error: server in Error state";
    }

    return "OK";
}

std::string OperatingSystemManager::getUiHealth(int timeoutMs)
{
    Network::WebSocketService client;
    const std::string address = "ws://localhost:7070";
    auto connectResult = client.connect(address, timeoutMs);
    if (connectResult.isError()) {
        return "Error: " + connectResult.errorValue();
    }

    UiApi::StatusGet::Command statusCmd{};
    auto statusResult =
        client.sendCommandAndGetResponse<UiApi::StatusGet::Okay>(statusCmd, timeoutMs);
    client.disconnect();

    if (statusResult.isError()) {
        return "Error: " + statusResult.errorValue();
    }

    const auto& response = statusResult.value();
    if (response.isError()) {
        return "Error: " + response.errorValue().message;
    }

    const auto& okay = response.value();
    if (!okay.connected_to_server) {
        return "Error: UI not connected to server";
    }

    return "OK";
}

Result<std::monostate, ApiError> OperatingSystemManager::startService(const std::string& unitName)
{
    return dependencies_.serviceCommand("start", unitName);
}

Result<std::monostate, ApiError> OperatingSystemManager::stopService(const std::string& unitName)
{
    return dependencies_.serviceCommand("stop", unitName);
}

Result<std::monostate, ApiError> OperatingSystemManager::restartService(const std::string& unitName)
{
    return dependencies_.serviceCommand("restart", unitName);
}

Result<std::monostate, ApiError> OperatingSystemManager::runServiceCommand(
    const std::string& action, const std::string& unitName)
{
    if (!dependencies_.systemCommand) {
        return makeMissingDependencyError("systemCommand");
    }

    if (action == "restart") {
        const std::string resetCommand = "systemctl reset-failed " + unitName;
        const int resetResult = dependencies_.systemCommand(resetCommand);
        if (resetResult == -1) {
            SLOG_WARN("systemctl reset-failed failed to start for {}", unitName);
        }
        else if (!WIFEXITED(resetResult) || WEXITSTATUS(resetResult) != 0) {
            SLOG_WARN("systemctl reset-failed failed for {}", unitName);
        }
    }

    const std::string command = "systemctl " + action + " " + unitName;
    const int result = dependencies_.systemCommand(command);
    if (result == -1) {
        return Result<std::monostate, ApiError>::error(ApiError("systemctl failed to start"));
    }

    if (WIFEXITED(result) && WEXITSTATUS(result) == 0) {
        return Result<std::monostate, ApiError>::okay(std::monostate{});
    }

    return Result<std::monostate, ApiError>::error(
        ApiError("systemctl " + action + " failed for " + unitName));
}

void OperatingSystemManager::scheduleReboot()
{
    if (dependencies_.reboot) {
        dependencies_.reboot();
    }
}

void OperatingSystemManager::scheduleRebootInternal()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    sync();
    const int result = ::reboot(RB_AUTOBOOT);
    if (result != 0) {
        SLOG_ERROR("Reboot failed: {}", std::strerror(errno));
    }
}

void OperatingSystemManager::transitionTo(State::Any newState)
{
    std::string oldStateName = getCurrentStateName();

    invokeOnExit(fsmState_, *this);

    auto expectedIndex = newState.getVariant().index();
    fsmState_ = std::move(newState);

    std::string newStateName = getCurrentStateName();
    LOG_INFO(State, "OsManager::StateMachine: {} -> {}", oldStateName, newStateName);

    fsmState_ = invokeOnEnter(std::move(fsmState_), *this);

    if (fsmState_.getVariant().index() != expectedIndex) {
        transitionTo(std::move(fsmState_));
    }
}

static LocalProcessBackend::Config resolveLocalProcessConfig(
    const OperatingSystemManager::BackendConfig& backendConfig)
{
    LocalProcessBackend::Config config;
    config.audioPath = resolveBinaryPath(backendConfig.audioPath, "dirtsim-audio");
    config.serverPath = resolveBinaryPath(backendConfig.serverPath, "dirtsim-server");
    config.uiPath = resolveBinaryPath(backendConfig.uiPath, "dirtsim-ui");
    config.audioArgs = backendConfig.audioArgs.empty() ? "-p 6060" : backendConfig.audioArgs;
    config.serverArgs = backendConfig.serverArgs.empty() ? "-p 8080" : backendConfig.serverArgs;

    const std::string uiBackend = backendConfig.uiBackend.empty() ? "x11" : backendConfig.uiBackend;
    const auto serverPort = resolveServerPort(config.serverArgs);
    config.uiArgs = resolveUiArgs(backendConfig.uiArgs, uiBackend, serverPort);
    config.uiDisplay = resolveUiDisplay(backendConfig.uiDisplay);
    config.workDir = resolveWorkDir(backendConfig.workDir);

    return config;
}

void OperatingSystemManager::initializeDefaultDependencies()
{
    if (backendConfig_.type == BackendType::LocalProcess) {
        auto config = resolveLocalProcessConfig(backendConfig_);
        localBackend_ = std::make_unique<LocalProcessBackend>(config);

        LOG_INFO(State, "Using local process backend");
        dependencies_.serviceCommand =
            [this](const std::string& action, const std::string& unitName) {
                return localBackend_->runCommand(action, unitName);
            };
        dependencies_.systemStatus = [this]() { return buildSystemStatusInternal(); };
        dependencies_.reboot = [] { LOG_WARN(State, "Reboot requested in local backend"); };
        dependencies_.commandRunner = [](const std::string& command) {
            return runCommandCaptureOutput(command);
        };
        dependencies_.processRunner = [](const std::vector<std::string>& argv, int timeoutMs) {
            return OsManager::runProcessCapture(argv, timeoutMs);
        };
        dependencies_.homeDirResolver = [](const std::string& user) {
            return resolveUserHomeDir(user);
        };
        dependencies_.sshPermissionsEnsurer = [](const std::filesystem::path& dirPath,
                                                 const std::filesystem::path& filePath,
                                                 const std::string& user) {
            return ensureSshPermissions(dirPath, filePath, user);
        };
        dependencies_.remoteCliRunner =
            [this](
                const PeerTrustBundle& peer, const std::vector<std::string>& argv, int timeoutMs) {
                RemoteSshExecutor executor(getPeerClientKeyPath());
                return executor.run(peer, argv, timeoutMs);
            };
        dependencies_.scannerChannelController = std::make_shared<NexmonChannelController>();
        return;
    }

    dependencies_.systemCommand = [](const std::string& command) {
        return std::system(command.c_str());
    };
    dependencies_.serviceCommand = [this](const std::string& action, const std::string& unitName) {
        return runServiceCommand(action, unitName);
    };
    dependencies_.systemStatus = [this]() { return buildSystemStatusInternal(); };
    dependencies_.reboot = [this]() { scheduleRebootInternal(); };
    dependencies_.commandRunner = [](const std::string& command) {
        return runCommandCaptureOutput(command);
    };
    dependencies_.processRunner = [](const std::vector<std::string>& argv, int timeoutMs) {
        return OsManager::runProcessCapture(argv, timeoutMs);
    };
    dependencies_.homeDirResolver = [](const std::string& user) {
        return resolveUserHomeDir(user);
    };
    dependencies_.sshPermissionsEnsurer = [](const std::filesystem::path& dirPath,
                                             const std::filesystem::path& filePath,
                                             const std::string& user) {
        return ensureSshPermissions(dirPath, filePath, user);
    };
    dependencies_.remoteCliRunner =
        [this](const PeerTrustBundle& peer, const std::vector<std::string>& argv, int timeoutMs) {
            RemoteSshExecutor executor(getPeerClientKeyPath());
            return executor.run(peer, argv, timeoutMs);
        };
    dependencies_.scannerChannelController = std::make_shared<NexmonChannelController>();
}

OsApi::SystemStatus::Okay OperatingSystemManager::buildSystemStatusInternal()
{
    OsApi::SystemStatus::Okay status;

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        status.uptime_seconds = static_cast<uint64_t>(info.uptime);
    }

    const auto metrics = systemMetrics_.get();
    status.cpu_percent = metrics.cpu_percent;
    status.memory_total_kb = metrics.memory_total_kb;
    if (metrics.memory_total_kb >= metrics.memory_used_kb) {
        status.memory_free_kb = metrics.memory_total_kb - metrics.memory_used_kb;
    }

    const auto rootStats = getDiskStats("/");
    status.disk_free_bytes_root = rootStats.free_bytes;
    status.disk_total_bytes_root = rootStats.total_bytes;

    const auto dataStats = getDiskStats("/data");
    status.disk_free_bytes_data = dataStats.free_bytes;
    status.disk_total_bytes_data = dataStats.total_bytes;

    status.audio_status = getAudioHealth(1500);
    status.server_status = getServerHealth(1500);
    status.ui_status = getUiHealth(1500);
    status.lan_web_ui_enabled = webUiEnabled_;
    status.lan_websocket_enabled = webSocketEnabled_;
    status.lan_websocket_token = webSocketEnabled_ ? webSocketToken_ : "";
    const auto scannerStatus = readScannerModeStatusInternal();
    status.scanner_mode_available = scannerStatus.available;
    status.scanner_mode_active = scannerStatus.active;
    status.scanner_mode_detail = scannerStatus.detail;

    return status;
}

} // namespace OsManager
} // namespace DirtSim
