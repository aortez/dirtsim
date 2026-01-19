#include "OperatingSystemManager.h"
#include "core/LoggingChannels.h"
#include <args.hxx>
#include <csignal>
#include <iostream>
#include <optional>

using namespace DirtSim;

static OsManager::OperatingSystemManager* g_manager = nullptr;

std::optional<OsManager::OperatingSystemManager::BackendType> parseBackendType(
    const std::string& value)
{
    if (value == "systemd") {
        return OsManager::OperatingSystemManager::BackendType::Systemd;
    }
    if (value == "local") {
        return OsManager::OperatingSystemManager::BackendType::LocalProcess;
    }
    return std::nullopt;
}

void signalHandler(int signum)
{
    SLOG_INFO("Interrupt signal ({}) received, shutting down...", signum);
    if (g_manager) {
        g_manager->requestExit();
    }
}

int main(int argc, char** argv)
{
    args::ArgumentParser parser(
        "DirtSim OS Manager",
        "Privileged process for system control and health reporting via WebSocket.");
    args::HelpFlag help(parser, "help", "Display this help menu", { 'h', "help" });
    args::ValueFlag<uint16_t> portArg(
        parser, "port", "WebSocket port (default: 9090)", { 'p', "port" });
    args::ValueFlag<std::string> logConfig(
        parser,
        "log-config",
        "Path to logging config JSON file (default: logging-config.json)",
        { "log-config" },
        "logging-config.json");
    args::ValueFlag<std::string> logChannels(
        parser,
        "channels",
        "Override log channels (e.g., network:debug,*:off)",
        { 'C', "channels" });
    args::ValueFlag<std::string> backend(
        parser, "backend", "Backend: systemd or local (default: systemd)", { "backend" });

    try {
        parser.ParseCLI(argc, argv);
    }
    catch (const args::Help&) {
        std::cout << parser;
        return 0;
    }
    catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    const uint16_t port = portArg ? args::get(portArg) : 9090;

    LoggingChannels::initializeFromConfig(args::get(logConfig), "os-manager");
    if (logChannels) {
        LoggingChannels::configureFromString(args::get(logChannels));
        SLOG_INFO("Applied channel overrides: {}", args::get(logChannels));
    }

    auto backendConfig = OsManager::OperatingSystemManager::BackendConfig::fromEnvironment();
    if (backend) {
        const auto parsed = parseBackendType(args::get(backend));
        if (!parsed.has_value()) {
            std::cerr << "Error: invalid backend '" << args::get(backend)
                      << "'. Use 'systemd' or 'local'." << std::endl;
            return 1;
        }
        backendConfig.type = parsed.value();
    }

    OsManager::OperatingSystemManager manager(port, backendConfig);
    g_manager = &manager;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto startResult = manager.start();
    if (startResult.isError()) {
        SLOG_ERROR("Failed to start os-manager: {}", startResult.errorValue());
        return 1;
    }

    manager.mainLoopRun();
    manager.stop();
    SLOG_INFO("os-manager shut down cleanly");
    return 0;
}
