#include "BenchmarkRunner.h"
#include "server/api/Exit.h"
#include "server/api/PerfStatsGet.h"
#include "server/api/ScenarioSwitch.h"
#include "server/api/SimRun.h"
#include "server/api/StatusGet.h"
#include "server/api/TimerStatsGet.h"
#include "server/api/WorldResize.h"
#include <chrono>
#include <spdlog/spdlog.h>
#include <thread>

namespace DirtSim {
namespace Client {

BenchmarkRunner::BenchmarkRunner()
{}

BenchmarkRunner::~BenchmarkRunner()
{}

BenchmarkResults BenchmarkRunner::run(
    const std::string& serverPath, uint32_t steps, const std::string& scenario, int worldSize)
{
    // Delegate to runWithServerArgs with no extra arguments.
    return runWithServerArgs(serverPath, steps, scenario, "", worldSize);
}

BenchmarkResults BenchmarkRunner::runWithServerArgs(
    const std::string& serverPath,
    uint32_t steps,
    const std::string& scenario,
    const std::string& serverArgs,
    int worldSize)
{
    BenchmarkResults results;
    results.scenario = scenario;
    results.steps = steps;

    // Build combined server arguments.
    std::string combinedArgs = "--log-config benchmark-logging-config.json " + serverArgs;

    // Launch server with custom arguments.
    if (!subprocessManager_.launchServer(serverPath, combinedArgs)) {
        spdlog::error("BenchmarkRunner: Failed to launch server with args: {}", serverArgs);
        return results;
    }

    if (!subprocessManager_.waitForServerReady("ws://localhost:8080", 10)) {
        spdlog::error("BenchmarkRunner: Server failed to start");
        return results;
    }

    auto connectResult = client_.connect("ws://localhost:8080");
    if (connectResult.isError()) {
        spdlog::error(
            "BenchmarkRunner: Failed to connect to server: {}", connectResult.errorValue());
        return results;
    }

    // Start simulation.
    auto benchmarkStart = std::chrono::steady_clock::now();

    Api::SimRun::Command simRunCmd;
    simRunCmd.timestep = 0.016;
    simRunCmd.max_steps = static_cast<int>(steps);

    auto simRunResult = client_.sendCommand<Api::SimRun::Okay>(simRunCmd, 5000);
    if (simRunResult.isError()) {
        spdlog::error("BenchmarkRunner: SimRun failed: {}", simRunResult.errorValue());
        return results;
    }
    if (simRunResult.value().isError()) {
        spdlog::error(
            "BenchmarkRunner: SimRun error: {}", simRunResult.value().errorValue().message);
        return results;
    }

    spdlog::info("BenchmarkRunner: Started simulation ({} steps)", steps);

    // Switch to requested scenario.
    spdlog::info("BenchmarkRunner: Switching to scenario '{}'", scenario);
    Api::ScenarioSwitch::Command scenarioCmd;
    scenarioCmd.scenario_id = scenario;

    auto scenarioResult = client_.sendCommand<Api::ScenarioSwitch::Okay>(scenarioCmd, 5000);
    if (scenarioResult.isError()) {
        spdlog::error("BenchmarkRunner: ScenarioSwitch failed: {}", scenarioResult.errorValue());
        return results;
    }
    if (scenarioResult.value().isError()) {
        spdlog::error(
            "BenchmarkRunner: ScenarioSwitch error: {}",
            scenarioResult.value().errorValue().message);
        return results;
    }
    spdlog::info("BenchmarkRunner: Scenario '{}' loaded", scenario);

    // Resize world if size specified (must be done after ScenarioSwitch).
    if (worldSize > 0) {
        spdlog::info("BenchmarkRunner: Resizing world to {}x{}", worldSize, worldSize);
        Api::WorldResize::Command resizeCmd;
        resizeCmd.width = static_cast<uint32_t>(worldSize);
        resizeCmd.height = static_cast<uint32_t>(worldSize);

        auto resizeResult = client_.sendCommand<std::monostate>(resizeCmd, 5000);
        if (resizeResult.isError()) {
            spdlog::error("BenchmarkRunner: World resize failed: {}", resizeResult.errorValue());
            return results;
        }
        if (resizeResult.value().isError()) {
            spdlog::error(
                "BenchmarkRunner: World resize error: {}",
                resizeResult.value().errorValue().message);
            return results;
        }
        spdlog::info("BenchmarkRunner: World resized successfully");
    }

    // Poll StatusGet until simulation completes.
    int timeoutSec = (steps * 50) / 1000 + 10;
    bool benchmarkComplete = false;

    while (true) {
        // Check if server is still alive.
        if (!subprocessManager_.isServerRunning()) {
            spdlog::error("BenchmarkRunner: Server process died during benchmark!");
            spdlog::error("BenchmarkRunner: Check dirtsim.log for crash details");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Poll current step using lightweight StatusGet (not StateGet).
        Api::StatusGet::Command statusCmd;
        auto statusResult = client_.sendCommand<Api::StatusGet::Okay>(statusCmd, 1000);
        if (statusResult.isError()) {
            continue; // Retry on transport error.
        }
        if (statusResult.value().isError()) {
            continue; // Retry on API error.
        }

        const auto& status = statusResult.value().value();

        // Capture world dimensions on first successful query.
        if (results.grid_size == "28x28" && status.width > 0 && status.height > 0) {
            results.grid_size = std::to_string(status.width) + "x" + std::to_string(status.height);
            spdlog::info("BenchmarkRunner: World size {}x{}", status.width, status.height);
        }

        if (status.timestep >= steps) {
            spdlog::info(
                "BenchmarkRunner: Benchmark complete (step {} >= target {})",
                status.timestep,
                steps);
            benchmarkComplete = true;
            break;
        }

        // Check timeout.
        auto elapsed = std::chrono::steady_clock::now() - benchmarkStart;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > timeoutSec) {
            spdlog::error("BenchmarkRunner: Timeout waiting for completion ({}s)", timeoutSec);
            break;
        }
    }

    auto benchmarkEnd = std::chrono::steady_clock::now();
    results.duration_sec = std::chrono::duration<double>(benchmarkEnd - benchmarkStart).count();

    if (!benchmarkComplete) {
        spdlog::error("BenchmarkRunner: Benchmark did not complete");
        client_.disconnect();
        return results;
    }

    // Query performance stats.
    spdlog::info("BenchmarkRunner: Requesting PerfStats from server");
    Api::PerfStatsGet::Command perfCmd;
    auto perfResult = client_.sendCommand<Api::PerfStatsGet::Okay>(perfCmd, 2000);
    if (perfResult.isError()) {
        spdlog::warn("Failed to get perf stats: {}", perfResult.errorValue());
    }
    else if (perfResult.value().isError()) {
        spdlog::warn("PerfStats error: {}", perfResult.value().errorValue().message);
    }
    else {
        const auto& perf = perfResult.value().value();
        results.server_fps = perf.fps;
        results.server_physics_avg_ms = perf.physics_avg_ms;
        results.server_physics_total_ms = perf.physics_total_ms;
        results.server_physics_calls = perf.physics_calls;
        results.server_serialization_avg_ms = perf.serialization_avg_ms;
        results.server_serialization_total_ms = perf.serialization_total_ms;
        results.server_serialization_calls = perf.serialization_calls;
        results.server_cache_update_avg_ms = perf.cache_update_avg_ms;
        results.server_network_send_avg_ms = perf.network_send_avg_ms;

        spdlog::info(
            "BenchmarkRunner: Server stats - fps: {:.1f}, physics: {:.1f}ms avg, "
            "serialization: {:.1f}ms avg",
            results.server_fps,
            results.server_physics_avg_ms,
            results.server_serialization_avg_ms);
    }

    // Query detailed timer statistics.
    spdlog::info("BenchmarkRunner: Requesting TimerStats from server");
    Api::TimerStatsGet::Command timerCmd;
    auto timerResult = client_.sendCommand<Api::TimerStatsGet::Okay>(timerCmd, 2000);
    if (timerResult.isError()) {
        spdlog::warn("Failed to get timer stats: {}", timerResult.errorValue());
    }
    else if (timerResult.value().isError()) {
        spdlog::warn("TimerStats error: {}", timerResult.value().errorValue().message);
    }
    else {
        results.timer_stats = timerResult.value().value().toJson();
        spdlog::info("BenchmarkRunner: Received {} timer stats", results.timer_stats.size());
    }

    // Send exit command to cleanly shut down server.
    spdlog::info("BenchmarkRunner: Sending Exit command to server");
    Api::Exit::Command exitCmd;
    client_.sendCommand<std::monostate>(exitCmd, 1000);
    // Ignore result - server closes connection after receiving exit command.

    // Disconnect and cleanup.
    client_.disconnect();

    return results;
}

nlohmann::json BenchmarkRunner::queryPerfStats()
{
    Api::PerfStatsGet::Command cmd;
    auto result = client_.sendCommand<Api::PerfStatsGet::Okay>(cmd);
    if (result.isError()) {
        spdlog::warn("Failed to query perf stats: {}", result.errorValue());
        return nlohmann::json::object();
    }
    if (result.value().isError()) {
        spdlog::warn("PerfStats error: {}", result.value().errorValue().message);
        return nlohmann::json::object();
    }

    return result.value().value().toJson();
}

} // namespace Client
} // namespace DirtSim
