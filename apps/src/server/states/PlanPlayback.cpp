#include "SearchBroadcastHelpers.h"
#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "server/StateMachine.h"
#include "server/api/PlanPlaybackStopped.h"
#include <thread>

namespace DirtSim {
namespace Server {
namespace State {
namespace {

void broadcastPlanPlaybackStopped(
    StateMachine& dsm,
    UUID planId,
    Api::PlanPlaybackStopReason reason,
    std::string errorMessage = "")
{
    const Api::PlanPlaybackStopped stopped{
        .planId = planId,
        .reason = reason,
        .errorMessage = std::move(errorMessage),
    };
    dsm.broadcastEventData(Api::PlanPlaybackStopped::name(), Network::serialize_payload(stopped));
}

bool shouldWaitForPlaybackDelay(
    uint32_t delayMs,
    std::chrono::steady_clock::time_point lastTickTime,
    std::chrono::steady_clock::time_point now)
{
    if (delayMs == 0u || lastTickTime == std::chrono::steady_clock::time_point{}) {
        return false;
    }

    return now - lastTickTime < std::chrono::milliseconds(delayMs);
}

} // namespace

void PlanPlayback::onEnter(StateMachine& dsm)
{
    LOG_INFO(State, "PlanPlayback: Entered");
    dsm.updateCachedWorldData(execution.getWorldData());
    renderBroadcasted_ = false;
    if (execution.hasRenderableFrame()) {
        broadcastExecutionRender(dsm, execution);
        renderBroadcasted_ = true;
    }
    lastPlaybackTickTime_ = {};
}

void PlanPlayback::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "PlanPlayback: Exiting");
}

std::optional<Any> PlanPlayback::tick(StateMachine& dsm)
{
    const auto now = std::chrono::steady_clock::now();
    if (!execution.isCompleted()
        && shouldWaitForPlaybackDelay(planPlaybackFrameDelayMs, lastPlaybackTickTime_, now)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return std::nullopt;
    }

    const auto tickResult = execution.tick();
    lastPlaybackTickTime_ = now;
    if (execution.hasRenderableFrame() && (!renderBroadcasted_ || tickResult.frameAdvanced)) {
        dsm.updateCachedWorldData(execution.getWorldData());
        broadcastExecutionRender(dsm, execution);
        renderBroadcasted_ = true;
    }

    if (tickResult.error.has_value()) {
        LOG_ERROR(State, "PlanPlayback: {}", tickResult.error.value());
        broadcastPlanPlaybackStopped(
            dsm, planId, Api::PlanPlaybackStopReason::Error, tickResult.error.value());
        return Idle{};
    }

    if (tickResult.completed) {
        broadcastPlanPlaybackStopped(dsm, planId, Api::PlanPlaybackStopReason::Completed);
        return Idle{};
    }

    return std::nullopt;
}

Any PlanPlayback::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

Any PlanPlayback::onEvent(const Api::PlanPlaybackPauseSet::Cwc& cwc, StateMachine& /*dsm*/)
{
    execution.pauseSet(cwc.command.paused);
    cwc.sendResponse(
        Api::PlanPlaybackPauseSet::Response::okay(
            Api::PlanPlaybackPauseSet::Okay{ .paused = execution.isPaused() }));
    return std::move(*this);
}

Any PlanPlayback::onEvent(const Api::PlanPlaybackStop::Cwc& cwc, StateMachine& dsm)
{
    execution.stop();
    broadcastPlanPlaybackStopped(dsm, planId, Api::PlanPlaybackStopReason::Stopped);
    cwc.sendResponse(
        Api::PlanPlaybackStop::Response::okay(Api::PlanPlaybackStop::Okay{ .stopped = true }));
    return Idle{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim
