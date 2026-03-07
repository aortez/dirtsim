#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/Timers.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/StateMachine.h"
#include "server/api/TimerStatsGet.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {
namespace State {

void SimPaused::onEnter(StateMachine& /*dsm*/)
{
    if (previousState.session.isNesSession()) {
        spdlog::info(
            "SimPaused: Simulation paused at step {} (NES session preserved)",
            previousState.stepCount);
        return;
    }

    spdlog::info(
        "SimPaused: Simulation paused at step {} (World preserved)", previousState.stepCount);
}

void SimPaused::onExit(StateMachine& /*dsm*/)
{
    spdlog::info("SimPaused: Exiting paused state");
}

State::Any SimPaused::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    spdlog::info("SimPaused: Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state (World destroyed with SimPaused).
    return Shutdown{};
}

State::Any SimPaused::onEvent(const Api::StateGet::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::StateGet::Response;

    // Return cached WorldData from paused state.
    auto cachedPtr = dsm.getCachedWorldData();
    if (cachedPtr) {
        Api::StateGet::Okay responseData;
        responseData.worldData = *cachedPtr;
        cwc.sendResponse(Response::okay(std::move(responseData)));
    }
    else if (const WorldData* worldData = previousState.session.getWorldData()) {
        Api::StateGet::Okay responseData;
        responseData.worldData = *worldData;
        cwc.sendResponse(Response::okay(std::move(responseData)));
    }
    else {
        cwc.sendResponse(Response::error(ApiError("No state available")));
    }
    return std::move(*this);
}

State::Any SimPaused::onEvent(const Api::NesApuGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::NesApuGet::Response;

    auto nes = previousState.session.requireNesWorld();
    if (nes.isError()) {
        cwc.sendResponse(Response::error(ApiError("NesApuGet requires active NES scenario")));
        return std::move(*this);
    }

    const auto snapshot = nes.value().driver->copyRuntimeApuSnapshot();
    if (!snapshot.has_value()) {
        cwc.sendResponse(Response::error(ApiError("APU snapshot not available")));
        return std::move(*this);
    }

    Api::NesApuGet::Okay okay;
    okay.pulse1_enabled = snapshot->pulse1Enabled;
    okay.pulse2_enabled = snapshot->pulse2Enabled;
    okay.triangle_enabled = snapshot->triangleEnabled;
    okay.noise_enabled = snapshot->noiseEnabled;
    okay.pulse1_length_counter = snapshot->pulse1LengthCounter;
    okay.pulse2_length_counter = snapshot->pulse2LengthCounter;
    okay.triangle_length_counter = snapshot->triangleLengthCounter;
    okay.noise_length_counter = snapshot->noiseLengthCounter;
    okay.pulse1_timer_period = snapshot->pulse1TimerPeriod;
    okay.pulse2_timer_period = snapshot->pulse2TimerPeriod;
    okay.triangle_timer_period = snapshot->triangleTimerPeriod;
    okay.noise_timer_period = snapshot->noiseTimerPeriod;
    okay.pulse1_duty = snapshot->pulse1Duty;
    okay.pulse2_duty = snapshot->pulse2Duty;
    okay.noise_mode = snapshot->noiseMode;
    okay.frame_counter_mode_5step = snapshot->frameCounterMode5Step;
    okay.register_write_count = snapshot->registerWriteCount;
    okay.total_samples_generated = snapshot->totalSamplesGenerated;
    okay.audio_underruns = snapshot->audioUnderruns;
    okay.audio_overruns = snapshot->audioOverruns;
    okay.audio_callback_calls = snapshot->audioCallbackCalls;
    okay.audio_samples_dropped = snapshot->audioSamplesDropped;

    cwc.sendResponse(Response::okay(okay));
    return std::move(*this);
}

State::Any SimPaused::onEvent(const Api::PerfStatsGet::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::PerfStatsGet::Response;

    // Gather performance statistics from timers.
    auto& timers = dsm.getTimers();

    Api::PerfStatsGet::Okay stats;
    stats.fps = previousState.actualFPS;

    // Physics timing.
    stats.physics_calls = timers.getCallCount("physics_step");
    stats.physics_total_ms = timers.getAccumulatedTime("physics_step");
    stats.physics_avg_ms =
        stats.physics_calls > 0 ? stats.physics_total_ms / stats.physics_calls : 0.0;

    // Serialization timing.
    stats.serialization_calls = timers.getCallCount("serialize_worlddata");
    stats.serialization_total_ms = timers.getAccumulatedTime("serialize_worlddata");
    stats.serialization_avg_ms = stats.serialization_calls > 0
        ? stats.serialization_total_ms / stats.serialization_calls
        : 0.0;

    // Cache update timing.
    stats.cache_update_calls = timers.getCallCount("cache_update");
    stats.cache_update_total_ms = timers.getAccumulatedTime("cache_update");
    stats.cache_update_avg_ms =
        stats.cache_update_calls > 0 ? stats.cache_update_total_ms / stats.cache_update_calls : 0.0;

    // Network send timing.
    stats.network_send_calls = timers.getCallCount("network_send");
    stats.network_send_total_ms = timers.getAccumulatedTime("network_send");
    stats.network_send_avg_ms =
        stats.network_send_calls > 0 ? stats.network_send_total_ms / stats.network_send_calls : 0.0;

    spdlog::info(
        "SimPaused: API perf_stats_get returning {} physics steps, {} serializations",
        stats.physics_calls,
        stats.serialization_calls);

    cwc.sendResponse(Response::okay(std::move(stats)));
    return std::move(*this);
}

State::Any SimPaused::onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& dsm)
{
    using Response = Api::TimerStatsGet::Response;

    auto& timers = dsm.getTimers();
    std::vector<std::string> timerNames = timers.getAllTimerNames();

    Api::TimerStatsGet::Okay okay;

    for (const auto& name : timerNames) {
        Api::TimerStatsGet::TimerEntry entry;
        entry.total_ms = timers.getAccumulatedTime(name);
        entry.calls = timers.getCallCount(name);
        entry.avg_ms = entry.calls > 0 ? entry.total_ms / entry.calls : 0.0;
        okay.timers[name] = entry;
    }

    spdlog::info("SimPaused: API timer_stats_get returning {} timers", okay.timers.size());

    cwc.sendResponse(Response::okay(std::move(okay)));
    return std::move(*this);
}

// Future handlers:
// - SimRun: Resume with new parameters → std::move(previousState)
// - SimStop: Destroy world and return to Idle

} // namespace State
} // namespace Server
} // namespace DirtSim
