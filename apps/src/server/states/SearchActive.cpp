#include "State.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "server/PlanRepository.h"
#include "server/StateMachine.h"
#include "server/api/PlanSaved.h"
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {

namespace {

constexpr auto kProgressBroadcastInterval = std::chrono::milliseconds(100);

void broadcastSearchProgress(StateMachine& dsm, const Api::SearchProgress& progress)
{
    dsm.broadcastEventData(Api::SearchProgress::name(), Network::serialize_payload(progress));
}

void broadcastSearchRender(StateMachine& dsm, const SearchSupport::SmbPlanExecution& execution)
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

void SearchActive::onEnter(StateMachine& dsm)
{
    LOG_INFO(State, "SearchActive: Entered");
    dsm.updateCachedWorldData(execution.getWorldData());
    broadcastSearchRender(dsm, execution);
    broadcastSearchProgress(dsm, execution.getProgress());
    lastProgressBroadcastTime_ = std::chrono::steady_clock::now();
}

void SearchActive::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "SearchActive: Exiting");
}

std::optional<Any> SearchActive::tick(StateMachine& dsm)
{
    const auto tickResult = execution.tick();
    if (tickResult.frameAdvanced) {
        dsm.updateCachedWorldData(execution.getWorldData());
        broadcastSearchRender(dsm, execution);
    }

    const auto now = std::chrono::steady_clock::now();
    if (tickResult.frameAdvanced || now - lastProgressBroadcastTime_ >= kProgressBroadcastInterval
        || tickResult.completed) {
        broadcastSearchProgress(dsm, execution.getProgress());
        lastProgressBroadcastTime_ = now;
    }

    if (tickResult.error.has_value()) {
        LOG_ERROR(State, "SearchActive: {}", tickResult.error.value());
        return Idle{};
    }

    if (!tickResult.completed) {
        return std::nullopt;
    }

    auto storeResult = dsm.getPlanRepository().store(execution.getPlan());
    if (storeResult.isError()) {
        LOG_ERROR(State, "SearchActive: Failed to store plan: {}", storeResult.errorValue());
        return Idle{};
    }

    const Api::PlanSaved saved{
        .summary = execution.getPlan().summary,
    };
    dsm.broadcastEventData(Api::PlanSaved::name(), Network::serialize_payload(saved));
    return Idle{};
}

Any SearchActive::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

Any SearchActive::onEvent(const Api::SearchPauseSet::Cwc& cwc, StateMachine& dsm)
{
    execution.pauseSet(cwc.command.paused);

    Api::SearchPauseSet::Okay response{
        .paused = execution.isPaused(),
    };
    cwc.sendResponse(Api::SearchPauseSet::Response::okay(std::move(response)));
    broadcastSearchProgress(dsm, execution.getProgress());
    lastProgressBroadcastTime_ = std::chrono::steady_clock::now();
    return std::move(*this);
}

Any SearchActive::onEvent(const Api::SearchProgressGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    Api::SearchProgressGet::Okay response{
        .progress = execution.getProgress(),
    };
    cwc.sendResponse(Api::SearchProgressGet::Response::okay(std::move(response)));
    return std::move(*this);
}

Any SearchActive::onEvent(const Api::SearchStop::Cwc& cwc, StateMachine& /*dsm*/)
{
    execution.stop();
    cwc.sendResponse(Api::SearchStop::Response::okay(Api::SearchStop::Okay{ .stopped = true }));
    return Idle{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim
