#include "BenchmarkRunner.h"
#include "CleanupRunner.h"
#include "CommandDispatcher.h"
#include "CommandRegistry.h"
#include "FunctionalTestRunner.h"
#include "GenomeDbBenchmark.h"
#include "IntegrationTest.h"
#include "RunAllRunner.h"
#include "TrainRunner.h"
#include "core/LoggingChannels.h"
#include "core/ReflectSerializer.h"
#include "core/input/GamepadManager.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/ClientHello.h"
#include "core/network/WebSocketService.h"
#include "core/network/WifiManager.h"
#include "server/api/EventSubscribe.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStart.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/StatusGet.h"
#include <algorithm>
#include <args.hxx>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

using namespace DirtSim;

// Base64 decoding for screenshot data.
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

// Helper function to sort timer_stats by total_ms in descending order.
// Returns an array of objects (to preserve sort order) instead of a JSON object.
nlohmann::json sortTimerStats(const nlohmann::json& timer_stats)
{
    if (timer_stats.empty()) {
        return nlohmann::json::array();
    }

    // Convert to vector of pairs for sorting.
    std::vector<std::pair<std::string, nlohmann::json>> timer_pairs;
    for (auto it = timer_stats.begin(); it != timer_stats.end(); ++it) {
        timer_pairs.push_back({ it.key(), it.value() });
    }

    // Sort by total_ms descending.
    std::sort(timer_pairs.begin(), timer_pairs.end(), [](const auto& a, const auto& b) {
        double a_total = a.second.value("total_ms", 0.0);
        double b_total = b.second.value("total_ms", 0.0);
        return a_total > b_total;
    });

    // Build as array of objects with "name" field to preserve order.
    nlohmann::json sorted_timers = nlohmann::json::array();
    for (const auto& pair : timer_pairs) {
        nlohmann::json entry = pair.second;
        entry["name"] = pair.first;
        sorted_timers.push_back(entry);
    }

    return sorted_timers;
}

// CLI-specific commands (not server/UI API commands).
struct CliCommandInfo {
    std::string name;
    std::string description;
};

static const std::vector<CliCommandInfo> CLI_COMMANDS = {
    { "benchmark", "Run performance benchmark (launches server)" },
    { "cleanup", "Clean up rogue dirtsim processes" },
    { "functional-test", "Run functional tests against a running UI/server" },
    { "gamepad-test", "Test gamepad input (prints state to console)" },
    { "genome-db-benchmark", "Test genome CRUD correctness and performance" },
    { "integration_test", "Run integration test (launches server + UI)" },
    { "network", "WiFi status, saved/open networks, connect, and forget (NetworkManager)" },
    { "run-all", "Launch server + UI and monitor (exits when UI closes)" },
    { "screenshot", "Capture screenshot from UI and save as PNG" },
    { "test_binary", "Test binary protocol with type-safe StatusGet command" },
    { "train", "Run evolution training with JSON config" },
    { "watch", "Subscribe to server broadcasts and dump to stdout" },
};

std::vector<CliCommandInfo> getSortedCliCommands()
{
    std::vector<CliCommandInfo> commands = CLI_COMMANDS;
    std::sort(commands.begin(), commands.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return commands;
}

template <typename CommandList>
std::vector<std::string> getSortedCommandNames(const CommandList& commands)
{
    std::vector<std::string> names;
    names.reserve(commands.size());
    for (const auto& cmd : commands) {
        names.emplace_back(cmd);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string buildCliCommandHelp()
{
    std::string help = "CLI Commands:\n";
    const auto sortedCommands = getSortedCliCommands();
    for (const auto& cmd : sortedCommands) {
        help += "  " + cmd.name + " - " + cmd.description + "\n";
    }
    return help;
}

template <typename CommandList>
std::string buildApiCommandHelp(const std::string& title, const CommandList& commands)
{
    std::string help = title + ":\n";
    const auto names = getSortedCommandNames(commands);
    for (const auto& name : names) {
        help += "  " + name + "\n";
    }
    return help;
}

std::string getGlobalHelp()
{
    std::string help = "Available targets:\n";
    help += "  benchmark\n";
    help += "  cleanup\n";
    help += "  functional-test\n";
    help += "  gamepad-test\n";
    help += "  genome-db-benchmark\n";
    help += "  integration_test\n";
    help += "  network\n";
    help += "  os-manager\n";
    help += "  run-all\n";
    help += "  screenshot\n";
    help += "  server\n";
    help += "  test_binary\n";
    help += "  train\n";
    help += "  ui\n";
    help += "  watch\n\n";
    help += buildCliCommandHelp();
    help += "\nTarget-specific help:\n";
    help += "  cli server help\n";
    help += "  cli ui help\n";
    help += "  cli os-manager help\n";
    help += "  cli network help\n";
    return help;
}

std::string getUiHelp()
{
    std::string help;
    help += "Usage: cli ui <command> [params]\n\n";
    help += "Options:\n";
    help += "  --address=ws://host:7070   Override default UI WebSocket URL\n";
    help += "  --example                  Print default JSON for a command\n";
    help += "  --timeout=MS               Response timeout in milliseconds\n\n";
    help += buildApiCommandHelp("UI API Commands (ws://localhost:7070)", Client::UI_COMMAND_NAMES);
    help += "\nExamples:\n";
    help += "  cli ui StatusGet\n";
    help += "  cli ui ScreenGrab --example\n";
    help += "  cli --address ws://dirtsim.local:7070 ui StatusGet\n";
    return help;
}

std::string getServerHelp()
{
    std::string help;
    help += "Usage: cli server <command> [params]\n\n";
    help += "Options:\n";
    help += "  --address=ws://host:8080   Override default server WebSocket URL\n";
    help += "  --example                  Print default JSON for a command\n";
    help += "  --timeout=MS               Response timeout in milliseconds\n\n";
    help += buildApiCommandHelp(
        "Server API Commands (ws://localhost:8080)", Client::SERVER_COMMAND_NAMES);
    help += "\nExamples:\n";
    help += "  cli server StatusGet\n";
    help += "  cli server SimRun --example\n";
    help += "  cli --address ws://dirtsim.local:8080 server StatusGet\n";
    return help;
}

std::string getOsManagerHelp()
{
    std::string help;
    help += "Usage: cli os-manager <command> [params]\n\n";
    help += "Options:\n";
    help += "  --address=ws://host:9090   Override default os-manager WebSocket URL\n";
    help += "  --example                  Print default JSON for a command\n";
    help += "  --timeout=MS               Response timeout in milliseconds\n\n";
    help += buildApiCommandHelp(
        "OS Manager API Commands (ws://localhost:9090)", Client::OS_COMMAND_NAMES);
    help += "\nExamples:\n";
    help += "  cli os-manager SystemStatus\n";
    help += "  cli os-manager WebUiAccessSet '{\"enabled\": true}'\n";
    help += "  cli --address ws://dirtsim.local:9090 os-manager SystemStatus\n";
    return help;
}

std::string getNetworkHelp()
{
    std::string help;
    help += "Usage: cli network <command> [args]\n\n";
    help += "Commands:\n";
    help += "  status\n";
    help += "  list\n";
    help += "  scan\n";
    help += "  connect <ssid> [--password \"secret\"]\n";
    help += "  disconnect [ssid]\n";
    help += "  forget <ssid>\n\n";
    help += "Examples:\n";
    help += "  cli network status\n";
    help += "  cli network list\n";
    help += "  cli network scan\n";
    help += "  cli network connect \"MySSID\" --password \"secret\"\n";
    help += "  cli network disconnect\n";
    help += "  cli network forget \"MySSID\"\n";
    return help;
}

std::string getTargetHelp(const std::string& targetName)
{
    if (targetName == "server") {
        return getServerHelp();
    }
    if (targetName == "ui") {
        return getUiHelp();
    }
    if (targetName == "os-manager") {
        return getOsManagerHelp();
    }
    if (targetName == "network") {
        return getNetworkHelp();
    }
    return getGlobalHelp();
}

std::string getCommandListHelp()
{
    return getGlobalHelp();
}

std::string getExamplesHelp()
{
    std::string examples = "Examples:\n\n";
    examples += "  cli ui StatusGet\n";
    examples += "  cli server StatusGet\n";
    examples += "  cli os-manager SystemStatus\n";
    examples += "  cli os-manager WebUiAccessSet '{\"enabled\": true}'\n";
    examples += "  cli os-manager WebSocketAccessSet '{\"enabled\": true}'\n";
    examples += "  cli os-manager StartServer\n";
    examples += "  cli os-manager StopUi\n";
    examples += "  cli os-manager RestartServer\n";
    examples += "  cli --address ws://dirtsim.local:9090 os-manager SystemStatus\n";
    examples += "  cli run-all\n";
    examples += "  cli network status\n";

    // Screenshot examples.
    examples += "\nScreenshot:\n";
    examples += "  cli screenshot output.png                              # Local UI\n";
    examples += "  cli screenshot --address ws://dirtsim.local:7070 out.png  # Remote UI\n";

    // Functional test examples.
    examples += "\nFunctional Tests:\n";
    examples += "  cli functional-test canExit\n";
    examples += "  cli functional-test canExit --restart\n";
    examples += "  cli functional-test canTrain\n";
    examples += "  cli functional-test canSetGenerationsAndTrain\n";
    examples += "  cli functional-test canPlantTreeSeed\n";
    examples += "  cli functional-test canLoadGenomeFromBrowser\n";
    examples += "  cli functional-test canOpenTrainingConfigPanel\n";
    examples += "  cli functional-test canExit --ui-address ws://dirtsim.local:7070 "
                "--server-address ws://dirtsim.local:8080\n";
    examples += "  cli functional-test canExit --os-manager-address ws://dirtsim.local:9090\n";

    // Target-specific help.
    examples += "\nTarget-specific help:\n";
    examples += "  cli ui help\n";
    examples += "  cli server help\n";
    examples += "  cli os-manager help\n";
    examples += "  cli network help\n";
    return examples;
}

std::string buildCommand(const std::string& commandName, const std::string& jsonParams)
{
    nlohmann::json cmd;
    cmd["command"] = commandName;

    // If params provided, merge them in.
    if (!jsonParams.empty()) {
        try {
            nlohmann::json params = nlohmann::json::parse(jsonParams);
            // Merge params into cmd.
            for (auto& [key, value] : params.items()) {
                cmd[key] = value;
            }
        }
        catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Error parsing JSON parameters: " << e.what() << std::endl;
            return "";
        }
    }

    return cmd.dump();
}

std::string deriveServerAddressFromUi(const std::string& uiAddress)
{
    const std::string uiPort = ":7070";
    const std::string serverPort = ":8080";
    const auto portPos = uiAddress.rfind(uiPort);
    if (portPos == std::string::npos) {
        return "";
    }

    std::string serverAddress = uiAddress;
    serverAddress.replace(portPos, uiPort.size(), serverPort);
    return serverAddress;
}

std::string deriveOsManagerAddressFromUi(const std::string& uiAddress)
{
    const std::string uiPort = ":7070";
    const std::string osPort = ":9090";
    const auto portPos = uiAddress.rfind(uiPort);
    if (portPos == std::string::npos) {
        return "";
    }

    std::string osAddress = uiAddress;
    osAddress.replace(portPos, uiPort.size(), osPort);
    return osAddress;
}

std::string extractHost(const std::string& address)
{
    const auto schemePos = address.find("://");
    const size_t hostStart = schemePos == std::string::npos ? 0 : schemePos + 3;
    const size_t hostEnd = address.find_first_of(":/?", hostStart);
    if (hostEnd == std::string::npos) {
        return address.substr(hostStart);
    }
    return address.substr(hostStart, hostEnd - hostStart);
}

std::string extractPort(const std::string& address, const std::string& defaultPort)
{
    const auto schemePos = address.find("://");
    const size_t hostStart = schemePos == std::string::npos ? 0 : schemePos + 3;
    const size_t portPos = address.find(':', hostStart);
    if (portPos == std::string::npos) {
        return defaultPort;
    }
    const size_t portEnd = address.find_first_of("/?", portPos + 1);
    if (portEnd == std::string::npos) {
        return address.substr(portPos + 1);
    }
    return address.substr(portPos + 1, portEnd - portPos - 1);
}

bool isLocalHost(const std::string& host)
{
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

bool isLocalAddress(const std::string& address)
{
    return isLocalHost(extractHost(address));
}

bool canConnect(const std::string& address, int timeoutMs)
{
    Network::WebSocketService client;
    auto connectResult = client.connect(address, timeoutMs);
    if (connectResult.isError()) {
        return false;
    }
    client.disconnect();
    return true;
}

bool waitForWebSocketReady(const std::string& address, int timeoutMs)
{
    const auto startTime = std::chrono::steady_clock::now();
    while (true) {
        if (canConnect(address, 1000)) {
            return true;
        }
        const auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string detectUiBackend()
{
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char* x11Display = std::getenv("DISPLAY");

    if (waylandDisplay && waylandDisplay[0] != '\0') {
        return "wayland";
    }
    if (x11Display && x11Display[0] != '\0') {
        return "x11";
    }
    return "x11";
}

bool spawnProcess(const std::string& path, const std::vector<std::string>& args)
{
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Error: Failed to fork process for " << path << std::endl;
        return false;
    }

    if (pid == 0) {
        std::vector<char*> execArgs;
        execArgs.reserve(args.size() + 2);
        execArgs.push_back(const_cast<char*>(path.c_str()));
        for (const auto& arg : args) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        execArgs.push_back(nullptr);
        execv(path.c_str(), execArgs.data());
        std::cerr << "Error: exec failed for " << path << std::endl;
        _exit(1);
    }

    return true;
}

bool restartLocalServices(
    const std::string& uiAddress, const std::string& serverAddress, int timeoutMs)
{
    std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
    std::filesystem::path binDir = exePath.parent_path();
    std::filesystem::path serverPath = binDir / "dirtsim-server";
    std::filesystem::path uiPath = binDir / "dirtsim-ui";

    if (!std::filesystem::exists(serverPath)) {
        std::cerr << "Error: Cannot find server binary at " << serverPath << std::endl;
        return false;
    }

    if (!std::filesystem::exists(uiPath)) {
        std::cerr << "Error: Cannot find UI binary at " << uiPath << std::endl;
        return false;
    }

    const bool serverRunning = canConnect(serverAddress, timeoutMs);
    if (!serverRunning) {
        const std::string serverPort = extractPort(serverAddress, "8080");
        std::cerr << "Launching server on port " << serverPort << "..." << std::endl;
        if (!spawnProcess(serverPath.string(), { "-p", serverPort })) {
            return false;
        }
        const int readyTimeoutMs = timeoutMs < 10000 ? 10000 : timeoutMs;
        if (!waitForWebSocketReady(serverAddress, readyTimeoutMs)) {
            std::cerr << "Error: Server did not become ready at " << serverAddress << std::endl;
            return false;
        }
    }
    else {
        std::cerr << "Server already running; skipping launch." << std::endl;
    }

    if (canConnect(uiAddress, timeoutMs)) {
        std::cerr << "UI already running; skipping launch." << std::endl;
        return true;
    }

    const std::string backend = detectUiBackend();
    const std::string serverHost = extractHost(serverAddress);
    if (serverHost.empty()) {
        std::cerr << "Error: Could not parse server host from " << serverAddress << std::endl;
        return false;
    }
    const std::string serverPort = extractPort(serverAddress, "8080");
    std::cerr << "Launching UI (" << backend << " backend)..." << std::endl;
    return spawnProcess(
        uiPath.string(), { "-b", backend, "--connect", serverHost + ":" + serverPort });
}

int main(int argc, char** argv)
{
    // Initialize logging channels (creates default logger named "cli" to stderr).
    LoggingChannels::initialize(spdlog::level::info, spdlog::level::debug, "cli", true);

    // Parse command line arguments.
    args::ArgumentParser parser(
        "Sparkle Duck CLI Client",
        "Send commands to Sparkle Duck server or UI via WebSocket.\n\n" + getExamplesHelp());

    args::HelpFlag help(parser, "help", "Display this help menu", { 'h', "help" });
    args::Flag verbose(parser, "verbose", "Enable debug logging", { 'v', "verbose" });
    args::ValueFlag<int> timeout(
        parser, "timeout", "Response timeout in milliseconds (default: 5000)", { 't', "timeout" });
    args::ValueFlag<std::string> addressOverride(
        parser, "address", "Override default WebSocket URL", { "address" });
    args::Flag example(
        parser, "example", "Print default JSON for command without sending it", { "example" });
    args::ValueFlag<std::string> uiAddressOverride(
        parser, "ui-address", "Functional test: UI WebSocket URL override", { "ui-address" });
    args::ValueFlag<std::string> serverAddressOverride(
        parser,
        "server-address",
        "Functional test: server WebSocket URL override",
        { "server-address" });
    args::ValueFlag<std::string> osManagerAddressOverride(
        parser,
        "os-manager-address",
        "Functional test: os-manager WebSocket URL override",
        { "os-manager-address" });
    args::Flag restartFunctionalTest(
        parser, "restart", "Functional test: restart local UI/server after canExit", { "restart" });

    // Benchmark-specific flags.
    args::ValueFlag<int> benchSteps(
        parser, "steps", "Benchmark: number of simulation steps (default: 120)", { "steps" }, 120);
    args::ValueFlag<std::string> benchScenario(
        parser,
        "scenario",
        "Benchmark: scenario name (default: Benchmark)",
        { "scenario" },
        "Benchmark");
    args::ValueFlag<int> benchWorldSize(
        parser,
        "size",
        "Benchmark: world grid size (default: scenario default)",
        { "world-size", "size" });
    args::Flag compareCache(
        parser,
        "compare-cache",
        "Benchmark: Run twice to compare cached vs non-cached performance",
        { "compare-cache" });

    args::ValueFlag<int> genomeCount(
        parser,
        "count",
        "Genome benchmark: number of genomes for perf test (default: 100)",
        { "count" },
        100);
    args::ValueFlag<std::string> networkPassword(
        parser, "password", "Network: WiFi password for connect", { 'p', "password" });

    args::Positional<std::string> target(
        parser, "target", "Target: 'server', 'ui', 'os-manager', or a CLI command like 'network'");
    args::Positional<std::string> command(parser, "command", getCommandListHelp());
    args::Positional<std::string> params(
        parser, "params", "Optional JSON object with command parameters");

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

    // Configure logging.
    if (verbose) {
        spdlog::set_level(spdlog::level::debug);
    }
    else {
        spdlog::set_level(spdlog::level::err);
    }

    // Require target argument.
    if (!target) {
        std::cerr << "Error: target is required ('server', 'ui', 'network', etc.)\n\n";
        std::cerr << parser;
        return 1;
    }

    std::string targetName = args::get(target);

    // Handle special CLI commands (benchmark, run-all, etc.).
    if (targetName == "benchmark") {
        // Set log level to error for clean JSON output (unless --verbose).
        if (!verbose) {
            spdlog::set_level(spdlog::level::err);
        }

        // Find server binary (assume it's in same directory as CLI).
        std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path binDir = exePath.parent_path();
        std::filesystem::path serverPath = binDir / "dirtsim-server";

        if (!std::filesystem::exists(serverPath)) {
            std::cerr << "Error: Cannot find server binary at " << serverPath << std::endl;
            return 1;
        }

        // Run benchmark.
        // Follow args library pattern: check presence before get.
        int actualSteps = benchSteps ? args::get(benchSteps) : 120;
        std::string actualScenario = benchScenario ? args::get(benchScenario) : "Benchmark";
        std::string remoteAddress = addressOverride ? args::get(addressOverride) : "";
        (void)benchWorldSize; // Unused - world size now determined by scenario.

        if (compareCache) {
            // Compare full system (cache + OpenMP) vs baseline.
            Client::BenchmarkRunner runner;

            spdlog::set_level(spdlog::level::info);
            spdlog::info("Running benchmark WITH cache + OpenMP (default)...");
            auto results_cached =
                runner.run(serverPath.string(), actualSteps, actualScenario, 0, remoteAddress);

            spdlog::info("Running benchmark WITHOUT cache or OpenMP (baseline)...");
            auto results_direct = runner.runWithServerArgs(
                serverPath.string(),
                actualSteps,
                actualScenario,
                "--no-grid-cache --no-openmp",
                0,
                remoteAddress);

            // Build comparison output.
            nlohmann::json comparison;
            comparison["scenario"] = actualScenario;
            comparison["steps"] = actualSteps;

            // Serialize results and sort timer_stats.
            nlohmann::json cached_json = ReflectSerializer::to_json(results_cached);
            if (!results_cached.timer_stats.empty()) {
                cached_json["timer_stats"] = sortTimerStats(results_cached.timer_stats);
            }

            nlohmann::json direct_json = ReflectSerializer::to_json(results_direct);
            if (!results_direct.timer_stats.empty()) {
                direct_json["timer_stats"] = sortTimerStats(results_direct.timer_stats);
            }

            comparison["with_cache_and_openmp"] = cached_json;
            comparison["without_cache_or_openmp_baseline"] = direct_json;

            // Calculate speedup.
            double cached_fps = results_cached.server_fps;
            double direct_fps = results_direct.server_fps;
            double speedup = (cached_fps / direct_fps - 1.0) * 100.0;

            comparison["speedup_percent"] = speedup;

            std::cout << comparison.dump(2) << std::endl;

            return 0;
        }
        else {
            // Single run (default behavior).
            Client::BenchmarkRunner runner;
            auto results =
                runner.run(serverPath.string(), actualSteps, actualScenario, 0, remoteAddress);

            // Output results as JSON using ReflectSerializer.
            nlohmann::json resultJson = ReflectSerializer::to_json(results);

            // Add timer_stats (already in JSON format), sorted by total_ms descending.
            if (!results.timer_stats.empty()) {
                resultJson["timer_stats"] = sortTimerStats(results.timer_stats);
            }

            std::cout << resultJson.dump(2) << std::endl;
        }
        return 0;
    }

    // Handle cleanup command (find and kill rogue processes).
    if (targetName == "cleanup") {
        // Always show cleanup output (unless explicitly verbose).
        if (!verbose) {
            spdlog::set_level(spdlog::level::info);
        }

        Client::CleanupRunner cleanup;
        auto results = cleanup.run();
        return results.empty() ? 0 : 0; // Always return 0 on success.
    }

    // Handle gamepad-test command (prints gamepad state to console).
    if (targetName == "gamepad-test") {
        std::cout << "Gamepad test mode. Press Ctrl+C to exit.\n" << std::endl;

        GamepadManager manager;

        if (!manager.isAvailable()) {
            std::cerr << "Error: SDL gamecontroller subsystem not available." << std::endl;
            return 1;
        }

        // Track previous state to detect changes.
        std::vector<GamepadState> prevStates;

        // Poll loop.
        while (true) {
            manager.poll();

            // Report newly connected gamepads.
            for (size_t idx : manager.getNewlyConnected()) {
                std::cout << "[Gamepad " << idx << "] Connected: " << manager.getGamepadName(idx)
                          << std::endl;
            }

            // Report newly disconnected gamepads.
            for (size_t idx : manager.getNewlyDisconnected()) {
                std::cout << "[Gamepad " << idx << "] Disconnected" << std::endl;
            }

            // Resize prevStates if needed.
            while (prevStates.size() < manager.getDeviceCount()) {
                prevStates.emplace_back();
            }

            // Print state for each connected gamepad (only on change).
            for (size_t i = 0; i < manager.getDeviceCount(); ++i) {
                const GamepadState* state = manager.getGamepadState(i);
                if (!state || !state->connected) {
                    continue;
                }

                GamepadState& prev = prevStates[i];

                // Check if state changed (with small deadzone for analog).
                bool changed = false;
                if (std::abs(state->stick_x - prev.stick_x) > 0.05f) changed = true;
                if (std::abs(state->stick_y - prev.stick_y) > 0.05f) changed = true;
                if (state->dpad_x != prev.dpad_x) changed = true;
                if (state->dpad_y != prev.dpad_y) changed = true;
                if (state->button_a != prev.button_a) changed = true;
                if (state->button_b != prev.button_b) changed = true;

                if (changed) {
                    std::cout << "[Gamepad " << i << "] " << "stick_x: " << std::fixed
                              << std::setprecision(2) << std::setw(6) << state->stick_x << "  "
                              << "stick_y: " << std::setw(6) << state->stick_y << "  " << "dpad: ("
                              << static_cast<int>(state->dpad_x) << ", "
                              << static_cast<int>(state->dpad_y) << ")  "
                              << "A: " << (state->button_a ? "true " : "false") << "  "
                              << "B: " << (state->button_b ? "true " : "false") << std::endl;

                    prev = *state;
                }
            }

            // Sleep to avoid busy-waiting (~60Hz poll rate).
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        return 0;
    }

    if (targetName == "genome-db-benchmark") {
        if (!verbose) {
            spdlog::set_level(spdlog::level::info);
        }

        int count = genomeCount ? args::get(genomeCount) : 100;

        Client::GenomeDbBenchmark benchmark;
        auto results = benchmark.run(count);

        nlohmann::json output = ReflectSerializer::to_json(results);
        std::cout << output.dump(2) << std::endl;

        return results.correctnessPassed ? 0 : 1;
    }

    if (targetName == "functional-test") {
        if (!command) {
            std::cerr << "Error: functional-test requires a test name\n\n";
            std::cerr << "Usage: cli functional-test canExit\n";
            return 1;
        }

        const std::string testName = args::get(command);
        if (testName != "canExit" && testName != "canTrain"
            && testName != "canSetGenerationsAndTrain" && testName != "canPlantTreeSeed"
            && testName != "canLoadGenomeFromBrowser" && testName != "canOpenTrainingConfigPanel") {
            std::cerr << "Error: unknown functional test '" << testName << "'\n";
            std::cerr << "Valid tests: canExit, canTrain, canSetGenerationsAndTrain, "
                         "canPlantTreeSeed, canLoadGenomeFromBrowser, "
                         "canOpenTrainingConfigPanel\n";
            return 1;
        }

        int timeoutMs = timeout ? args::get(timeout) : 5000;
        std::string uiAddress = uiAddressOverride
            ? args::get(uiAddressOverride)
            : (addressOverride ? args::get(addressOverride) : "ws://localhost:7070");
        std::string serverAddress =
            serverAddressOverride ? args::get(serverAddressOverride) : "ws://localhost:8080";
        std::string osManagerAddress =
            osManagerAddressOverride ? args::get(osManagerAddressOverride) : "ws://localhost:9090";
        if (!serverAddressOverride && (uiAddressOverride || addressOverride)) {
            const std::string derived = deriveServerAddressFromUi(uiAddress);
            if (!derived.empty()) {
                serverAddress = derived;
            }
        }
        if (!osManagerAddressOverride && (uiAddressOverride || addressOverride)) {
            const std::string derived = deriveOsManagerAddressFromUi(uiAddress);
            if (!derived.empty()) {
                osManagerAddress = derived;
            }
        }

        Client::FunctionalTestRunner runner;
        Client::FunctionalTestSummary summary{};
        if (testName == "canExit") {
            summary = runner.runCanExit(uiAddress, serverAddress, osManagerAddress, timeoutMs);
        }
        else if (testName == "canTrain") {
            summary = runner.runCanTrain(uiAddress, serverAddress, osManagerAddress, timeoutMs);
        }
        else if (testName == "canSetGenerationsAndTrain") {
            summary = runner.runCanSetGenerationsAndTrain(
                uiAddress, serverAddress, osManagerAddress, timeoutMs);
        }
        else if (testName == "canLoadGenomeFromBrowser") {
            summary = runner.runCanLoadGenomeFromBrowser(
                uiAddress, serverAddress, osManagerAddress, timeoutMs);
        }
        else if (testName == "canOpenTrainingConfigPanel") {
            summary = runner.runCanOpenTrainingConfigPanel(
                uiAddress, serverAddress, osManagerAddress, timeoutMs);
        }
        else {
            summary =
                runner.runCanPlantTreeSeed(uiAddress, serverAddress, osManagerAddress, timeoutMs);
        }
        std::cout << summary.toJson().dump() << std::endl;
        int exitCode = summary.result.isError() ? 1 : 0;
        if (restartFunctionalTest) {
            if (testName != "canExit") {
                std::cerr << "Warning: --restart is only supported for canExit; skipping."
                          << std::endl;
            }
            else if (summary.result.isError()) {
                std::cerr << "Warning: --restart skipped due to test failure." << std::endl;
            }
            else if (!isLocalAddress(uiAddress) || !isLocalAddress(serverAddress)) {
                std::cerr << "Warning: --restart requires local UI/server addresses; skipping."
                          << std::endl;
            }
            else {
                std::cerr << "Restarting local server/UI..." << std::endl;
                if (!restartLocalServices(uiAddress, serverAddress, timeoutMs)) {
                    exitCode = 1;
                }
            }
        }
        return exitCode;
    }

    if (targetName == "integration_test") {
        // Find server and UI binaries (assume they're in same directory as CLI).
        std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path binDir = exePath.parent_path();
        std::filesystem::path serverPath = binDir / "dirtsim-server";
        std::filesystem::path uiPath = binDir / "dirtsim-ui";

        if (!std::filesystem::exists(serverPath)) {
            std::cerr << "Error: Cannot find server binary at " << serverPath << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(uiPath)) {
            std::cerr << "Error: Cannot find UI binary at " << uiPath << std::endl;
            return 1;
        }

        // Run integration test.
        Client::IntegrationTest test;
        return test.run(serverPath.string(), uiPath.string());
    }

    // Handle run-all command (launches server and UI, monitors until UI exits).
    if (targetName == "run-all") {
        // Find server and UI binaries (assume they're in same directory as CLI).
        std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path binDir = exePath.parent_path();
        std::filesystem::path serverPath = binDir / "dirtsim-server";
        std::filesystem::path uiPath = binDir / "dirtsim-ui";

        if (!std::filesystem::exists(serverPath)) {
            std::cerr << "Error: Cannot find server binary at " << serverPath << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(uiPath)) {
            std::cerr << "Error: Cannot find UI binary at " << uiPath << std::endl;
            return 1;
        }

        // Run server and UI.
        auto result = Client::runAll(serverPath.string(), uiPath.string());
        if (result.isError()) {
            std::cerr << "Error: " << result.errorValue() << std::endl;
            return 1;
        }
        return 0;
    }

    // Handle screenshot command - capture UI screen and save as PNG.
    if (targetName == "screenshot") {
        // Get output filename from command argument.
        std::string outputFile;
        if (command) {
            outputFile = args::get(command);
        }
        else {
            // Generate default filename with timestamp.
            auto now = std::chrono::system_clock::now();
            auto timestamp =
                std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            outputFile = "screenshot_" + std::to_string(timestamp) + ".png";
        }

        // Determine UI address.
        std::string uiAddress;
        if (addressOverride) {
            uiAddress = args::get(addressOverride);
        }
        else {
            uiAddress = "ws://localhost:7070";
        }

        int timeoutMs = timeout ? args::get(timeout) : 10000;

        std::cerr << "Capturing screenshot from " << uiAddress << "..." << std::endl;

        // Connect to UI.
        Network::WebSocketService client;
        auto connectResult = client.connect(uiAddress, timeoutMs);
        if (connectResult.isError()) {
            std::cerr << "Failed to connect to UI at " << uiAddress << ": "
                      << connectResult.errorValue() << std::endl;
            return 1;
        }

        // Build typed ScreenGrab command.
        UiApi::ScreenGrab::Command cmd{
            .scale = 1.0,
            .format = UiApi::ScreenGrab::Format::Png,
            .quality = 23,
        };

        auto result = client.sendCommandAndGetResponse(cmd, timeoutMs);
        if (result.isError()) {
            std::cerr << "ScreenGrab command failed: " << result.errorValue() << std::endl;
            client.disconnect();
            return 1;
        }

        // Extract typed response from Result.
        auto okay = result.value();

        // Verify format.
        if (okay.format != UiApi::ScreenGrab::Format::Png) {
            std::cerr << "Unexpected format in response" << std::endl;
            client.disconnect();
            return 1;
        }

        // Decode base64 to PNG bytes.
        auto pngData = base64Decode(okay.data);
        if (pngData.empty()) {
            std::cerr << "Failed to decode base64 data" << std::endl;
            client.disconnect();
            return 1;
        }

        // Write PNG to file.
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to open output file: " << outputFile << std::endl;
            client.disconnect();
            return 1;
        }

        outFile.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
        outFile.close();

        std::cerr << "✓ Screenshot saved to " << outputFile << " (" << okay.width << "x"
                  << okay.height << ", " << pngData.size() << " bytes)" << std::endl;

        client.disconnect();
        return 0;
    }

    // Handle watch command - subscribe to server broadcasts and dump to stdout.
    if (targetName == "watch") {
        std::string serverAddress;
        if (addressOverride) {
            serverAddress = args::get(addressOverride);
        }
        else {
            serverAddress = "ws://localhost:8080";
        }

        std::cerr << "Connecting to " << serverAddress << " to watch broadcasts..." << std::endl;
        std::cerr << "Press Ctrl+C to exit.\n" << std::endl;

        Network::WebSocketService client;
        client.setProtocol(Network::Protocol::BINARY);
        Network::ClientHello hello{
            .protocolVersion = Network::kClientHelloProtocolVersion,
            .wantsRender = true,
            .wantsEvents = true,
        };
        client.setClientHello(hello);

        // Set up binary message handler.
        std::atomic<bool> connected{ false };
        client.onBinary([](const std::vector<std::byte>& data) {
            try {
                auto envelope = Network::deserialize_envelope(data);

                // Check for EvolutionProgress messages.
                if (envelope.message_type == "EvolutionProgress") {
                    auto progress =
                        Network::deserialize_payload<Api::EvolutionProgress>(envelope.payload);
                    nlohmann::json output = progress.toJson();
                    output["_type"] = envelope.message_type;
                    std::cout << output.dump() << std::endl;
                }
                else {
                    // Generic output for other message types.
                    nlohmann::json output;
                    output["_type"] = envelope.message_type;
                    output["_payload_size"] = envelope.payload.size();
                    std::cout << output.dump() << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error parsing message: " << e.what() << std::endl;
            }
        });

        client.onDisconnected([&connected]() {
            std::cerr << "Disconnected from server." << std::endl;
            connected = false;
        });

        // Connect.
        auto connectResult = client.connect(serverAddress, 5000);
        if (connectResult.isError()) {
            std::cerr << "Failed to connect: " << connectResult.errorValue() << std::endl;
            return 1;
        }

        // Subscribe to event stream.
        Api::EventSubscribe::Command eventCmd{
            .enabled = true,
            .connectionId = "",
        };
        auto eventResult =
            client.sendCommandAndGetResponse<Api::EventSubscribe::Okay>(eventCmd, 5000);
        if (eventResult.isError()) {
            std::cerr << "Failed to subscribe to event stream: " << eventResult.errorValue()
                      << std::endl;
            client.disconnect();
            return 1;
        }
        if (eventResult.value().isError()) {
            std::cerr << "EventSubscribe rejected: " << eventResult.value().errorValue().message
                      << std::endl;
            client.disconnect();
            return 1;
        }

        // Subscribe to broadcasts by sending RenderFormatSet.
        Api::RenderFormatSet::Command subCmd{
            .format = RenderFormat::EnumType::Basic,
            .connectionId = "", // Server fills this in.
        };
        auto subResult = client.sendCommandAndGetResponse<Api::RenderFormatSet::Okay>(subCmd, 5000);
        if (subResult.isError()) {
            std::cerr << "Failed to subscribe: " << subResult.errorValue() << std::endl;
            client.disconnect();
            return 1;
        }

        connected = true;
        std::cerr << "Connected and subscribed. Watching for broadcasts..." << std::endl;

        // Block until disconnected or interrupted.
        while (connected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        client.disconnect();
        return 0;
    }

    // Handle test_binary command - tests binary protocol with type-safe StatusGet.
    if (targetName == "test_binary") {
        // Get address from override or use command as address for backward compatibility.
        std::string testAddress;
        if (addressOverride) {
            testAddress = args::get(addressOverride);
        }
        else if (command) {
            testAddress = args::get(command);
        }
        else {
            std::cerr << "Error: address is required for test_binary command\n\n";
            std::cerr << "Usage: cli test_binary --address ws://localhost:8080\n";
            return 1;
        }

        std::cerr << "Testing binary protocol with StatusGet command..." << std::endl;

        // Create client in binary mode.
        Network::WebSocketService client;
        client.setProtocol(Network::Protocol::BINARY);

        // Connect.
        auto connectResult = client.connect(testAddress, 5000);
        if (connectResult.isError()) {
            std::cerr << "Failed to connect: " << connectResult.errorValue() << std::endl;
            return 1;
        }

        std::cerr << "Connected using BINARY protocol" << std::endl;

        // Build binary envelope manually.
        Api::StatusGet::Command cmd{}; // Empty command.
        uint64_t id = 1;
        auto envelope = Network::make_command_envelope(id, cmd);

        std::cerr << "Sending StatusGet via binary MessageEnvelope..." << std::endl;

        // Send and receive binary.
        auto envResult = client.sendBinaryAndReceive(envelope, 5000);
        if (envResult.isError()) {
            std::cerr << "Binary send/receive failed: " << envResult.errorValue() << std::endl;
            return 1;
        }

        std::cerr << "Received binary response, extracting result..." << std::endl;

        // Extract typed result from envelope.
        auto result = Network::extract_result<Api::StatusGet::Okay, ApiError>(envResult.value());
        if (result.isError()) {
            std::cerr << "Command failed: " << result.errorValue().message << std::endl;
            return 1;
        }

        // Success! Output result as JSON to stdout (machine-readable).
        const auto& status = result.value();
        nlohmann::json output = ReflectSerializer::to_json(status);
        std::cout << output.dump(2) << std::endl;

        // Human-readable success message to stderr.
        std::cerr << "✓ Binary protocol test PASSED" << std::endl;
        if (status.scenario_id.has_value()) {
            std::cerr << "  Scenario: " << toString(status.scenario_id.value()) << std::endl;
        }
        std::cerr << "  Grid: " << status.width << "x" << status.height << std::endl;
        std::cerr << "  Timestep: " << status.timestep << std::endl;

        client.disconnect();
        return 0;
    }

    // Handle train command - run evolution training with JSON config.
    if (targetName == "train") {
        // Show progress output unless explicitly verbose (then show debug too).
        if (!verbose) {
            spdlog::set_level(spdlog::level::info);
        }

        // Find server binary.
        std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path binDir = exePath.parent_path();
        std::filesystem::path serverPath = binDir / "dirtsim-server";

        std::string remoteAddress = addressOverride ? args::get(addressOverride) : "";

        if (remoteAddress.empty() && !std::filesystem::exists(serverPath)) {
            std::cerr << "Error: Cannot find server binary at " << serverPath << std::endl;
            return 1;
        }

        // Parse JSON config if provided, otherwise use defaults.
        Api::EvolutionStart::Command config;
        if (command) {
            try {
                nlohmann::json configJson = nlohmann::json::parse(args::get(command));
                config = Api::EvolutionStart::Command::fromJson(configJson);
            }
            catch (const std::exception& e) {
                std::cerr << "Error parsing JSON config: " << e.what() << std::endl;
                std::cerr << "\nExample config:\n";
                std::cerr << R"({
  "evolution": {
    "populationSize": 50,
    "tournamentSize": 3,
    "maxGenerations": 100,
    "maxSimulationTime": 600.0
  },
  "mutation": {
    "rate": 0.015,
    "sigma": 0.05,
    "resetRate": 0.0005
  },
  "scenarioId": "TreeGermination",
  "organismType": "TREE",
  "population": [
    {
      "brainKind": "NeuralNet",
      "count": 50,
      "randomCount": 50
    }
  ]
})" << std::endl;
                return 1;
            }
        }

        // Run training with signal handling for graceful Ctrl+C shutdown.
        Client::TrainRunner runner;

        // Install SIGINT handler for graceful shutdown.
        // Note: Must use C-style function pointer, not lambda.
        static Client::TrainRunner* g_runner = nullptr;
        static auto sigintHandler = +[](int) -> void {
            if (g_runner) {
                std::cerr << "\n[Ctrl+C detected - stopping training gracefully...]\n"
                          << std::flush;
                g_runner->requestStop();
            }
        };

        g_runner = &runner;
        auto oldHandler = std::signal(SIGINT, sigintHandler);

        auto results = runner.run(serverPath.string(), config, remoteAddress);

        // Restore old signal handler.
        std::signal(SIGINT, oldHandler);
        g_runner = nullptr;

        // Output results as JSON to stdout.
        nlohmann::json output = ReflectSerializer::to_json(results);
        std::cout << output.dump(2) << std::endl;

        return results.completed ? 0 : 1;
    }

    if (targetName == "network") {
        if (!command) {
            std::cerr << "Error: command is required for network target\n\n";
            std::cerr << getNetworkHelp();
            return 1;
        }

        const std::string subcommand = args::get(command);
        if (subcommand == "help") {
            std::cout << getNetworkHelp();
            return 0;
        }
        Network::WifiManager wifi;

        if (subcommand == "status") {
            const auto statusResult = wifi.getStatus();
            if (statusResult.isError()) {
                std::cerr << "Error: " << statusResult.errorValue() << std::endl;
                return 1;
            }

            nlohmann::json output = ReflectSerializer::to_json(statusResult.value());
            std::cout << output.dump(2) << std::endl;
            return 0;
        }

        if (subcommand == "list") {
            const auto listResult = wifi.listNetworks();
            if (listResult.isError()) {
                std::cerr << "Error: " << listResult.errorValue() << std::endl;
                return 1;
            }

            nlohmann::json networks = nlohmann::json::array();
            for (const auto& network : listResult.value()) {
                networks.push_back(ReflectSerializer::to_json(network));
            }

            nlohmann::json output;
            output["networks"] = networks;
            std::cout << output.dump(2) << std::endl;
            return 0;
        }

        if (subcommand == "scan") {
            const auto scanResult = wifi.listAccessPoints();
            if (scanResult.isError()) {
                std::cerr << "Error: " << scanResult.errorValue() << std::endl;
                return 1;
            }

            nlohmann::json accessPoints = nlohmann::json::array();
            for (const auto& ap : scanResult.value()) {
                accessPoints.push_back(ReflectSerializer::to_json(ap));
            }

            nlohmann::json output;
            output["access_points"] = accessPoints;
            std::cout << output.dump(2) << std::endl;
            return 0;
        }

        if (subcommand == "connect") {
            if (!params) {
                std::cerr << "Error: SSID is required for network connect\n\n";
                std::cerr << "Usage: cli network connect \"MySSID\" [--password \"secret\"]\n";
                return 1;
            }

            const std::string ssid = args::get(params);
            std::optional<std::string> password;
            if (networkPassword) {
                const std::string value = args::get(networkPassword);
                if (!value.empty()) {
                    password = value;
                }
            }

            const auto connectResult = wifi.connectBySsid(ssid, password);
            if (connectResult.isError()) {
                std::cerr << "Error: " << connectResult.errorValue() << std::endl;
                return 1;
            }

            nlohmann::json output = ReflectSerializer::to_json(connectResult.value());
            std::cout << output.dump(2) << std::endl;
            return 0;
        }

        if (subcommand == "disconnect") {
            std::optional<std::string> ssid;
            if (params) {
                ssid = args::get(params);
            }

            const auto disconnectResult = wifi.disconnect(ssid);
            if (disconnectResult.isError()) {
                std::cerr << "Error: " << disconnectResult.errorValue() << std::endl;
                return 1;
            }

            nlohmann::json output = ReflectSerializer::to_json(disconnectResult.value());
            std::cout << output.dump(2) << std::endl;
            return 0;
        }

        if (subcommand == "forget") {
            if (!params) {
                std::cerr << "Error: SSID is required for network forget\n\n";
                std::cerr << "Usage: cli network forget \"MySSID\"\n";
                return 1;
            }

            const std::string ssid = args::get(params);
            const auto forgetResult = wifi.forget(ssid);
            if (forgetResult.isError()) {
                std::cerr << "Error: " << forgetResult.errorValue() << std::endl;
                return 1;
            }

            nlohmann::json output = ReflectSerializer::to_json(forgetResult.value());
            std::cout << output.dump(2) << std::endl;
            return 0;
        }

        std::cerr << "Error: unknown network command '" << subcommand << "'\n";
        std::cerr << "Valid commands: status, list, scan, connect, disconnect, forget\n";
        return 1;
    }

    // Handle server/ui targets - normal command mode.
    if (targetName != "server" && targetName != "ui" && targetName != "os-manager") {
        std::cerr << "Error: unknown target '" << targetName << "'\n";
        std::cerr << "Valid targets: server, ui, benchmark, cleanup, gamepad-test, "
                     "functional-test, genome-db-benchmark, integration_test, network, "
                     "os-manager, run-all, test_binary, train\n\n";
        std::cerr << parser;
        return 1;
    }

    // Require command argument for server/ui targets.
    if (!command) {
        std::cerr << "Error: command is required for " << targetName << " target\n\n";
        std::cerr << getTargetHelp(targetName);
        return 1;
    }

    std::string commandName = args::get(command);
    if (commandName == "help") {
        std::cout << getTargetHelp(targetName);
        return 0;
    }
    if (example) {
        Client::CommandDispatcher dispatcher;
        auto dispatchTarget =
            (targetName == "server") ? Client::Target::Server : Client::Target::Ui;
        auto exampleResult = dispatcher.getExample(dispatchTarget, commandName);
        if (exampleResult.isError()) {
            std::cerr << "Failed to build example: " << exampleResult.errorValue().message
                      << std::endl;
            return 1;
        }

        std::cout << exampleResult.value().dump(2) << std::endl;
        return 0;
    }

    // Determine address (override or default).
    std::string address;
    if (addressOverride) {
        address = args::get(addressOverride);
    }
    else {
        // Default addresses based on target.
        if (targetName == "server") {
            address = "ws://localhost:8080";
        }
        else if (targetName == "ui") {
            address = "ws://localhost:7070";
        }
        else if (targetName == "os-manager") {
            address = "ws://localhost:9090";
        }
    }

    int timeoutMs = timeout ? args::get(timeout) : 5000;

    // Parse command body (if provided).
    nlohmann::json bodyJson;
    if (params) {
        try {
            bodyJson = nlohmann::json::parse(args::get(params));
        }
        catch (const nlohmann::json::parse_error& e) {
            std::cerr << "Error parsing JSON parameters: " << e.what() << std::endl;
            return 1;
        }
    }

    // Connect to target using WebSocketClient.
    Network::WebSocketService client;

    auto connectResult = client.connect(address, timeoutMs);
    if (connectResult.isError()) {
        std::cerr << "Failed to connect to " << address << ": " << connectResult.errorValue()
                  << std::endl;
        return 1;
    }

    // Dispatch command using type-safe dispatcher.
    Client::CommandDispatcher dispatcher;
    Client::Target dispatchTarget = Client::Target::OsManager;
    if (targetName == "server") {
        dispatchTarget = Client::Target::Server;
    }
    else if (targetName == "ui") {
        dispatchTarget = Client::Target::Ui;
    }
    auto responseResult = dispatcher.dispatch(dispatchTarget, client, commandName, bodyJson);
    if (responseResult.isError()) {
        const auto& errorMessage = responseResult.errorValue().message;
        bool printedStructured = false;
        try {
            nlohmann::json errorJson = nlohmann::json::parse(errorMessage);
            std::cerr << errorJson.dump() << std::endl;
            printedStructured = true;
        }
        catch (const nlohmann::json::parse_error&) {
        }
        if (!printedStructured) {
            std::cerr << "Failed to execute command: " << errorMessage << std::endl;
        }
        return 1;
    }

    std::string response = responseResult.value();

    // Special handling for DiagramGet - extract and display just the diagram.
    if (commandName == "DiagramGet") {
        auto printDiagram = [](const std::string& responseText) {
            try {
                nlohmann::json responseJson = nlohmann::json::parse(responseText);
                spdlog::debug("Parsed response JSON: {}", responseJson.dump(2));

                if (responseJson.contains("value") && responseJson["value"].contains("diagram")) {
                    std::cout << responseJson["value"]["diagram"].get<std::string>() << std::endl;
                }
                else {
                    spdlog::warn("Response doesn't contain expected diagram structure");
                    std::cout << responseText << std::endl;
                }
            }
            catch (const nlohmann::json::parse_error& e) {
                spdlog::error("JSON parse error: {}", e.what());
                std::cout << responseText << std::endl;
            }
        };

        printDiagram(response);

        bool wantEmoji = true;
        if (bodyJson.is_object() && bodyJson.contains("style") && bodyJson["style"].is_string()) {
            wantEmoji = bodyJson["style"].get<std::string>() != "Emoji";
        }

        if (wantEmoji) {
            nlohmann::json emojiBody = bodyJson.is_object() ? bodyJson : nlohmann::json::object();
            emojiBody["style"] = "Emoji";
            auto emojiResult = dispatcher.dispatch(dispatchTarget, client, commandName, emojiBody);
            if (emojiResult.isError()) {
                spdlog::warn("Emoji DiagramGet failed: {}", emojiResult.errorValue().message);
            }
            else {
                std::cout << std::endl;
                printDiagram(emojiResult.value());
            }
        }
    }
    else {
        // Output response to stdout.
        std::cout << response << std::endl;
    }

    client.disconnect();
    return 0;
}
