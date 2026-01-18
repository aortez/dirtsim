#include "BenchmarkRunner.h"
#include "server/api/Exit.h"
#include "server/api/PerfStatsGet.h"
#include "server/api/SimRun.h"
#include "server/api/SimStop.h"
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
    const std::string& serverPath,
    int steps,
    const std::string& scenario,
    int worldSize,
    const std::string& remoteAddress)
{
    // Delegate to runWithServerArgs with no extra arguments.
    return runWithServerArgs(serverPath, steps, scenario, "", worldSize, remoteAddress);
}

BenchmarkResults BenchmarkRunner::runWithServerArgs(
    const std::string& serverPath,
    int steps,
    const std::string& scenario,
    const std::string& serverArgs,
    int worldSize,
    const std::string& remoteAddress)
{
    BenchmarkResults results;
    results.scenario = scenario;
    results.steps = steps;

    // Determine connection address.
    std::string connectAddress = remoteAddress.empty() ? "ws://localhost:8080" : remoteAddress;
    bool isRemote = !remoteAddress.empty();

    // Only launch local server if no remote address specified.
    if (!isRemote) {
        // Build combined server arguments.
        std::string combinedArgs = "--log-config benchmark-logging-config.json " + serverArgs;

        // Launch server with custom arguments.
        if (!subprocessManager_.launchServer(serverPath, combinedArgs)) {
            spdlog::error("BenchmarkRunner: Failed to launch server with args: {}", serverArgs);
            return results;
        }

        if (!subprocessManager_.waitForServerReady(connectAddress, 10)) {
            spdlog::error("BenchmarkRunner: Server failed to start");
            return results;
        }
    }
    else {
        spdlog::info("BenchmarkRunner: Using remote server at {}", connectAddress);
    }

    auto connectResult = client_.connect(connectAddress);
    if (connectResult.isError()) {
        spdlog::error(
            "BenchmarkRunner: Failed to connect to server: {}", connectResult.errorValue());
        return results;
    }

    // Query server state to determine if we need to stop existing simulation.
    spdlog::info("BenchmarkRunner: Querying server state");
    Api::StatusGet::Command statusCmd;
    auto statusResult = client_.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, 2000);
    if (statusResult.isError()) {
        spdlog::error("BenchmarkRunner: StatusGet failed: {}", statusResult.errorValue());
        return results;
    }
    if (statusResult.value().isError()) {
        spdlog::error(
            "BenchmarkRunner: StatusGet error: {}", statusResult.value().errorValue().message);
        return results;
    }

    const auto& status = statusResult.value().value();
    spdlog::info("BenchmarkRunner: Server state is '{}'", status.state);

    // If server is already running a simulation, stop it first to transition to Idle.
    if (status.state == "SimRunning") {
        spdlog::info("BenchmarkRunner: Stopping existing simulation");
        Api::SimStop::Command stopCmd;
        auto stopResult = client_.sendCommandAndGetResponse<std::monostate>(stopCmd, 2000);
        if (stopResult.isError()) {
            spdlog::error("BenchmarkRunner: SimStop failed: {}", stopResult.errorValue());
            return results;
        }
        if (stopResult.value().isError()) {
            spdlog::error(
                "BenchmarkRunner: SimStop error: {}", stopResult.value().errorValue().message);
            return results;
        }
        spdlog::info("BenchmarkRunner: Server transitioned to Idle state");
    }

    // Start simulation with requested scenario (server is now in Idle state).
    spdlog::info("BenchmarkRunner: Starting simulation with scenario '{}'", scenario);

    auto scenarioId = Scenario::fromString(scenario);
    if (!scenarioId.has_value()) {
        spdlog::error("BenchmarkRunner: Invalid scenario name: {}", scenario);
        return results;
    }

    Api::SimRun::Command simRunCmd;
    simRunCmd.timestep = 0.016;
    simRunCmd.max_steps = steps;
    simRunCmd.scenario_id = scenarioId;

    auto simRunResult = client_.sendCommandAndGetResponse<Api::SimRun::Okay>(simRunCmd, 5000);
    if (simRunResult.isError()) {
        spdlog::error("BenchmarkRunner: SimRun failed: {}", simRunResult.errorValue());
        return results;
    }
    if (simRunResult.value().isError()) {
        spdlog::error(
            "BenchmarkRunner: SimRun error: {}", simRunResult.value().errorValue().message);
        return results;
    }

    spdlog::info("BenchmarkRunner: Simulation started ({} steps, scenario '{}')", steps, scenario);

    // Resize world if size specified (must be done after SimRun creates the world).
    if (worldSize > 0) {
        spdlog::info("BenchmarkRunner: Resizing world to {}x{}", worldSize, worldSize);
        Api::WorldResize::Command resizeCmd;
        resizeCmd.width = static_cast<int16_t>(worldSize);
        resizeCmd.height = static_cast<int16_t>(worldSize);

        auto resizeResult = client_.sendCommandAndGetResponse<std::monostate>(resizeCmd, 5000);
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

    // Start benchmark timer after all setup is complete.
    auto benchmarkStart = std::chrono::steady_clock::now();

    // Poll StatusGet until simulation completes.
    int timeoutSec = (steps * 50) / 1000 + 10;
    bool benchmarkComplete = false;

    while (true) {
        // Check if server is still alive (only for local server).
        if (!isRemote && !subprocessManager_.isServerRunning()) {
            spdlog::error("BenchmarkRunner: Server process died during benchmark!");
            spdlog::error("BenchmarkRunner: Check dirtsim.log for crash details");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Poll current step using lightweight StatusGet (not StateGet).
        Api::StatusGet::Command statusCmd;
        auto statusResult =
            client_.sendCommandAndGetResponse<Api::StatusGet::Okay>(statusCmd, 2000);
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
    auto perfResult = client_.sendCommandAndGetResponse<Api::PerfStatsGet::Okay>(perfCmd, 2000);
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
    auto timerResult = client_.sendCommandAndGetResponse<Api::TimerStatsGet::Okay>(timerCmd, 2000);
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

    // Send exit command to cleanly shut down server (only for local server).
    if (!isRemote) {
        spdlog::info("BenchmarkRunner: Sending Exit command to server");
        Api::Exit::Command exitCmd;
        client_.sendCommandAndGetResponse<std::monostate>(exitCmd, 1000);
        // Ignore result - server closes connection after receiving exit command.
    }

    // Disconnect and cleanup.
    client_.disconnect();

    return results;
}

nlohmann::json BenchmarkRunner::queryPerfStats()
{
    Api::PerfStatsGet::Command cmd;
    auto result = client_.sendCommandAndGetResponse<Api::PerfStatsGet::Okay>(cmd);
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
