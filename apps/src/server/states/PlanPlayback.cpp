#include "State.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "server/StateMachine.h"
#include "server/api/PlanPlaybackStopped.h"
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {
namespace {

void broadcastPlanPlaybackStopped(
    StateMachine& dsm, UUID planId, Api::PlanPlaybackStopReason reason)
{
    const Api::PlanPlaybackStopped stopped{
        .planId = planId,
        .reason = reason,
    };
    dsm.broadcastEventData(Api::PlanPlaybackStopped::name(), Network::serialize_payload(stopped));
}

void broadcastPlanPlaybackRender(
    StateMachine& dsm, const SearchSupport::SmbPlanExecution& execution)
{
    static const std::vector<OrganismId> emptyOrganismGrid{};
    const auto scenarioId = Scenario::EnumType::NesSuperMarioBros;
    const auto scenarioConfig = makeDefaultConfig(scenarioId);
    dsm.broadcastRenderMessage(
        execution.getWorldData(),
        emptyOrganismGrid,
        scenarioId,
        scenarioConfig,
        std::nullopt,
        execution.getScenarioVideoFrame());
}

} // namespace

void PlanPlayback::onEnter(StateMachine& dsm)
{
    LOG_INFO(State, "PlanPlayback: Entered");
    dsm.updateCachedWorldData(execution.getWorldData());
    broadcastPlanPlaybackRender(dsm, execution);
}

void PlanPlayback::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "PlanPlayback: Exiting");
}

std::optional<Any> PlanPlayback::tick(StateMachine& dsm)
{
    const auto tickResult = execution.tick();
    if (tickResult.frameAdvanced) {
        dsm.updateCachedWorldData(execution.getWorldData());
        broadcastPlanPlaybackRender(dsm, execution);
    }

    if (tickResult.error.has_value()) {
        LOG_ERROR(State, "PlanPlayback: {}", tickResult.error.value());
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
