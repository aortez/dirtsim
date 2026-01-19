#include "OperatingSystemManager.h"
#include "core/LoggingChannels.h"
#include <args.hxx>
#include <csignal>
#include <iostream>

using namespace DirtSim;

static OsManager::OperatingSystemManager* g_manager = nullptr;

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

    OsManager::OperatingSystemManager manager(port);
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
