#pragma once
#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>
#include <string>

namespace DirtSim::Ui {

/// Per-phase timing accumulator for display backend run loops.
struct PhaseStats {
    double totalMs = 0.0;
    double maxMs = 0.0;

    void record(double ms)
    {
        totalMs += ms;
        if (ms > maxMs) {
            maxMs = ms;
        }
    }

    void reset()
    {
        totalMs = 0.0;
        maxMs = 0.0;
    }
};

/// Loop timing instrumentation shared across display backends.
struct LoopTimingStats {
    using Clock = std::chrono::steady_clock;

    int loopCount = 0;
    double totalLoopMs = 0.0;
    double totalLoopMsSq = 0.0;
    double minLoopMs = 1e9;
    double maxLoopMs = 0.0;
    PhaseStats processEvents;
    PhaseStats timerHandler;
    Clock::time_point lastLogTime = Clock::now();

    void recordLoop(double loopMs)
    {
        totalLoopMs += loopMs;
        totalLoopMsSq += loopMs * loopMs;
        if (loopMs < minLoopMs) {
            minLoopMs = loopMs;
        }
        if (loopMs > maxLoopMs) {
            maxLoopMs = loopMs;
        }
        loopCount++;
    }

    bool shouldLog() { return Clock::now() - lastLogTime >= std::chrono::seconds(10); }

    void log(const std::string& backendName)
    {
        lastLogTime = Clock::now();
        if (loopCount == 0) {
            return;
        }
        const double avgLoop = totalLoopMs / loopCount;
        const double variance = (totalLoopMsSq / loopCount) - (avgLoop * avgLoop);
        const double stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
        spdlog::info("{} loop timing ({} iters):", backendName, loopCount);
        spdlog::info(
            "  Loop: {:.2f}ms avg (min={:.2f} max={:.2f} stddev={:.2f})",
            avgLoop,
            minLoopMs,
            maxLoopMs,
            stddev);
        spdlog::info(
            "  processEvents: {:.2f}ms avg (max={:.2f})",
            processEvents.totalMs / loopCount,
            processEvents.maxMs);
        spdlog::info(
            "  lv_timer_handler: {:.2f}ms avg (max={:.2f})",
            timerHandler.totalMs / loopCount,
            timerHandler.maxMs);
    }

    void reset()
    {
        loopCount = 0;
        totalLoopMs = 0.0;
        totalLoopMsSq = 0.0;
        minLoopMs = 1e9;
        maxLoopMs = 0.0;
        processEvents.reset();
        timerHandler.reset();
    }

    void logAndReset(const std::string& backendName)
    {
        log(backendName);
        reset();
    }
};

} // namespace DirtSim::Ui
