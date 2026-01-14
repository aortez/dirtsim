#include "BenchmarkRunner.h"
#include "CleanupRunner.h"
#include "CommandDispatcher.h"
#include "CommandRegistry.h"
#include "IntegrationTest.h"
#include "RunAllRunner.h"
#include "TrainRunner.h"
#include "core/LoggingChannels.h"
#include "core/ReflectSerializer.h"
#include "core/input/GamepadManager.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStart.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/StatusGet.h"
#include <args.hxx>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

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
    { "gamepad-test", "Test gamepad input (prints state to console)" },
    { "integration_test", "Run integration test (launches server + UI)" },
    { "run-all", "Launch server + UI and monitor (exits when UI closes)" },
    { "screenshot", "Capture screenshot from UI and save as PNG" },
    { "test_binary", "Test binary protocol with type-safe StatusGet command" },
    { "train", "Run evolution training with JSON config" },
    { "watch", "Subscribe to server broadcasts and dump to stdout" },
};

std::string getCommandListHelp()
{
    std::string help = "Available commands:\n\n";

    // CLI-specific commands.
    help += "CLI Commands:\n";
    for (const auto& cmd : CLI_COMMANDS) {
        help += "  " + cmd.name + " - " + cmd.description + "\n";
    }

    // Auto-generated server API commands.
    help += "\nServer API Commands (ws://localhost:8080):\n";
    for (const auto& cmdName : Client::SERVER_COMMAND_NAMES) {
        help += "  " + std::string(cmdName) + "\n";
    }

    // Auto-generated UI API commands.
    help += "\nUI API Commands (ws://localhost:7070):\n";
    for (const auto& cmdName : Client::UI_COMMAND_NAMES) {
        help += "  " + std::string(cmdName) + "\n";
    }

    return help;
}

std::string getExamplesHelp()
{
    std::string examples = "Examples:\n\n";

    // CLI-specific examples.
    examples += "CLI Commands:\n";
    for (const auto& cmd : CLI_COMMANDS) {
        examples += "  cli " + cmd.name + "\n";
    }

    // Server API examples (show a few common ones).
    examples += "\nServer API Examples:\n";
    examples += "  cli server StatusGet\n";
    examples += "  cli server SimRun '{\"scenario\": \"sandbox\"}'\n";
    examples += "  cli server DiagramGet\n";
    examples += "  cli server CellSet '{\"x\": 50, \"y\": 50, \"material\": \"WATER\", \"fill\": "
                "1.0}'\n";

    // Remote server.
    examples += "\nRemote Server:\n";
    examples += "  cli --address ws://dirtsim.local:8080 server StatusGet\n";
    examples += "  cli --address ws://dirtsim.local:8080 server SimRun '{\"scenario\": "
                "\"tree_germination\"}'\n";

    // UI API examples.
    examples += "\nUI API Examples:\n";
    examples += "  cli ui StatusGet\n";
    examples += "  cli ui ScreenGrab\n";
    examples += "  cli --address ws://dirtsim.local:7070 ui StatusGet\n";

    // Screenshot examples.
    examples += "\nScreenshot:\n";
    examples += "  cli screenshot output.png                              # Local UI\n";
    examples += "  cli screenshot --address ws://dirtsim.local:7070 out.png  # Remote UI\n";

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

int main(int argc, char** argv)
{
    // Initialize logging channels (creates default logger named "cli" to stderr).
    LoggingChannels::initialize(spdlog::level::info, spdlog::level::debug, "cli");

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

    args::Positional<std::string> target(parser, "target", "Target component: 'server' or 'ui'");
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
        std::cerr << "Error: target is required ('server', 'ui', 'benchmark', etc.)\n\n";
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

    // Handle integration_test command (auto-launches server and UI).
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

        auto result = client.sendCommand(cmd, timeoutMs);
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

        // Subscribe to broadcasts by sending RenderFormatSet.
        Api::RenderFormatSet::Command subCmd{
            .format = RenderFormat::EnumType::Basic,
            .connectionId = "", // Server fills this in.
        };
        auto subResult = client.sendCommand<Api::RenderFormatSet::Okay>(subCmd, 5000);
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
            catch (const nlohmann::json::parse_error& e) {
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
  "scenarioId": "TreeGermination"
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

    // Handle server/ui targets - normal command mode.
    if (targetName != "server" && targetName != "ui") {
        std::cerr << "Error: unknown target '" << targetName << "'\n";
        std::cerr << "Valid targets: server, ui, benchmark, cleanup, gamepad-test, "
                     "integration_test, run-all, test_binary, train\n\n";
        std::cerr << parser;
        return 1;
    }

    // Require command argument for server/ui targets.
    if (!command) {
        std::cerr << "Error: command is required for " << targetName << " target\n\n";
        std::cerr << parser;
        return 1;
    }

    std::string commandName = args::get(command);

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
    auto dispatchTarget = (targetName == "server") ? Client::Target::Server : Client::Target::Ui;
    auto responseResult = dispatcher.dispatch(dispatchTarget, client, commandName, bodyJson);
    if (responseResult.isError()) {
        std::cerr << "Failed to execute command: " << responseResult.errorValue().message
                  << std::endl;
        return 1;
    }

    std::string response = responseResult.value();

    // Special handling for DiagramGet - extract and display just the diagram.
    if (commandName == "DiagramGet") {
        try {
            nlohmann::json responseJson = nlohmann::json::parse(response);
            spdlog::debug("Parsed response JSON: {}", responseJson.dump(2));

            if (responseJson.contains("value") && responseJson["value"].contains("diagram")) {
                std::cout << responseJson["value"]["diagram"].get<std::string>() << std::endl;
            }
            else {
                // Fallback: display raw response.
                spdlog::warn("Response doesn't contain expected diagram structure");
                std::cout << response << std::endl;
            }
        }
        catch (const nlohmann::json::parse_error& e) {
            spdlog::error("JSON parse error: {}", e.what());
            std::cout << response << std::endl;
        }
    }
    else {
        // Output response to stdout.
        std::cout << response << std::endl;
    }

    client.disconnect();
    return 0;
}
