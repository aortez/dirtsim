#include "OperatingSystemManager.h"
#include "cli/SubprocessManager.h"
#include "core/LoggingChannels.h"
#include "core/StateLifecycle.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/JsonProtocol.h"
#include "os-manager/network/CommandDeserializerJson.h"
#include "server/api/StatusGet.h"
#include "ui/state-machine/api/StatusGet.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <sys/reboot.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace DirtSim {
namespace OsManager {

namespace {
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
} // namespace

class LocalProcessBackend {
public:
    struct Config {
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
        subprocessManager_.isServerRunning();
        subprocessManager_.isUIRunning();
    }

private:
    enum class Service {
        Server,
        Ui,
    };

    std::optional<Service> resolveService(const std::string& unitName) const
    {
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
}

OperatingSystemManager::OperatingSystemManager(uint16_t port, const BackendConfig& backendConfig)
    : port_(port), backendConfig_(backendConfig)
{
    initializeDefaultDependencies();
    setupWebSocketService();
}

OperatingSystemManager::OperatingSystemManager(TestMode mode)
    : enableNetworking_(false), dependencies_(std::move(mode.dependencies))
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

    auto listenResult = wsService_.listen(port_);
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
                                          evt.sendResponse(std::declval<typename std::decay_t<
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

    wsService_.registerHandler<OsApi::SystemStatus::Cwc>(
        [this](OsApi::SystemStatus::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StartServer::Cwc>(
        [this](OsApi::StartServer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StopServer::Cwc>(
        [this](OsApi::StopServer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::RestartServer::Cwc>(
        [this](OsApi::RestartServer::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StartUi::Cwc>(
        [this](OsApi::StartUi::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::StopUi::Cwc>(
        [this](OsApi::StopUi::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::RestartUi::Cwc>(
        [this](OsApi::RestartUi::Cwc cwc) { queueEvent(cwc); });
    wsService_.registerHandler<OsApi::Reboot::Cwc>(
        [this](OsApi::Reboot::Cwc cwc) { queueEvent(cwc); });

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
            DISPATCH_OS_CMD_EMPTY(OsApi::RestartServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::RestartUi);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::StartUi);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopServer);
            DISPATCH_OS_CMD_EMPTY(OsApi::StopUi);
            DISPATCH_OS_CMD_WITH_RESP(OsApi::SystemStatus);

#undef DISPATCH_OS_CMD_WITH_RESP
#undef DISPATCH_OS_CMD_EMPTY

            LOG_WARN(Network, "Unknown os-manager JSON command");
        });

    LOG_INFO(Network, "os-manager WebSocket handlers registered");
}

OsApi::SystemStatus::Okay OperatingSystemManager::buildSystemStatus()
{
    if (dependencies_.systemStatus) {
        return dependencies_.systemStatus();
    }

    OsApi::SystemStatus::Okay status;
    return status;
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
    config.serverPath = resolveBinaryPath(backendConfig.serverPath, "dirtsim-server");
    config.uiPath = resolveBinaryPath(backendConfig.uiPath, "dirtsim-ui");
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

    status.server_status = getServerHealth(1500);
    status.ui_status = getUiHealth(1500);

    return status;
}

} // namespace OsManager
} // namespace DirtSim
