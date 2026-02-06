#include "OperatingSystemManager.h"
#include "audio/api/StatusGet.h"
#include "cli/SubprocessManager.h"
#include "core/LoggingChannels.h"
#include "core/StateLifecycle.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/JsonProtocol.h"
#include "os-manager/network/CommandDeserializerJson.h"
#include "os-manager/network/PeerAdvertisement.h"
#include "os-manager/network/PeerDiscovery.h"
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

Result<std::monostate, ApiError> makeMissingDependencyError(const std::string& name)
{
    return Result<std::monostate, ApiError>::error(ApiError("Missing dependency for " + name));
}

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
    setupWebSocketService();
    webSocketToken_ = generateWebSocketToken();
    initializePeerDiscovery();
}

OperatingSystemManager::OperatingSystemManager(uint16_t port, const BackendConfig& backendConfig)
    : port_(port), backendConfig_(backendConfig)
{
    initializeDefaultDependencies();
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

void OperatingSystemManager::stop()
{
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
    wsService_.registerHandler<OsApi::Reboot::Cwc>(
        [this](OsApi::Reboot::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::TrustBundleGet::Cwc>(
        [this](OsApi::TrustBundleGet::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::TrustPeer::Cwc>(
        [this](OsApi::TrustPeer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::UntrustPeer::Cwc>(
        [this](OsApi::UntrustPeer::Cwc cwc) { queueEvent(cwc); });
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
            DISPATCH_OS_CMD_EMPTY(OsApi::StartAudio);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartUi);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopAudio);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopUi);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::PeerClientKeyEnsure);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::PeersGet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::RemoteCliRun);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::SystemStatus);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::TrustBundleGet);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::TrustPeer);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::UntrustPeer);
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
    const std::string address = "ws://localhost:6060";
    auto connectResult = client.connect(address, timeoutMs);
    if (connectResult.isError()) {
        return "Error: " + connectResult.errorValue();
    }

    AudioApi::StatusGet::Command statusCmd{};
    auto statusResult =
        client.sendCommandAndGetResponse<AudioApi::StatusGet::Okay>(statusCmd, timeoutMs);
    client.disconnect();

    if (statusResult.isError()) {
        return "Error: " + statusResult.errorValue();
    }

    const auto& response = statusResult.value();
    if (response.isError()) {
        return "Error: " + response.errorValue().message;
    }

    return "OK";
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

    return status;
}

} // namespace OsManager
} // namespace DirtSim
