#include "SearchBroadcastHelpers.h"
#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "server/PlanRepository.h"
#include "server/StateMachine.h"
#include "server/api/PlanSaved.h"
#include "server/api/SearchCompleted.h"

namespace DirtSim {
namespace Server {
namespace State {

namespace {

constexpr auto kProgressBroadcastInterval = std::chrono::milliseconds(100);

void broadcastSearchProgress(StateMachine& dsm, const Api::SearchProgress& progress)
{
    dsm.broadcastEventData(Api::SearchProgress::name(), Network::serialize_payload(progress));
}

Api::SearchCompletionReason mapCompletionReason(
    const std::optional<SearchSupport::SmbDfsSearchCompletionReason>& reason)
{
    switch (reason.value_or(SearchSupport::SmbDfsSearchCompletionReason::Error)) {
        case SearchSupport::SmbDfsSearchCompletionReason::Completed:
            return Api::SearchCompletionReason::Completed;
        case SearchSupport::SmbDfsSearchCompletionReason::Stopped:
            return Api::SearchCompletionReason::Stopped;
        case SearchSupport::SmbDfsSearchCompletionReason::Error:
            return Api::SearchCompletionReason::Error;
    }

    DIRTSIM_ASSERT(false, "Unhandled SmbDfsSearchCompletionReason");
    return Api::SearchCompletionReason::Error;
}

} // namespace

void SearchActive::onEnter(StateMachine& dsm)
{
    LOG_INFO(State, "SearchActive: Entered");
    dsm.updateCachedWorldData(search.getWorldData());
    renderBroadcasted_ = false;
    if (search.hasRenderableFrame()) {
        broadcastSearchRender(dsm, search.getWorldData(), search.getScenarioVideoFrame());
        renderBroadcasted_ = true;
    }
    broadcastSearchProgress(dsm, search.getProgress());
    lastProgressBroadcastTime_ = std::chrono::steady_clock::now();
}

void SearchActive::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "SearchActive: Exiting");
}

std::optional<Any> SearchActive::tick(StateMachine& dsm)
{
    const auto tickResult = search.tick();
    if (search.hasRenderableFrame() && (!renderBroadcasted_ || tickResult.frameAdvanced)) {
        dsm.updateCachedWorldData(search.getWorldData());
        broadcastSearchRender(dsm, search.getWorldData(), search.getScenarioVideoFrame());
        renderBroadcasted_ = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (tickResult.frameAdvanced || now - lastProgressBroadcastTime_ >= kProgressBroadcastInterval
        || tickResult.completed) {
        broadcastSearchProgress(dsm, search.getProgress());
        lastProgressBroadcastTime_ = now;
    }

    if (!tickResult.completed) {
        return std::nullopt;
    }

    if (tickResult.error.has_value()) {
        LOG_ERROR(State, "SearchActive: {}", tickResult.error.value());
    }

    auto completionReason = mapCompletionReason(search.getCompletionReason());
    std::string completionErrorMessage = search.getCompletionErrorMessage().value_or(std::string{});
    std::optional<Api::PlanSummary> savedSummary = std::nullopt;

    if (search.hasPersistablePlan()) {
        auto storeResult = dsm.getPlanRepository().store(search.getPlan());
        if (storeResult.isError()) {
            LOG_ERROR(State, "SearchActive: Failed to store plan: {}", storeResult.errorValue());
            completionReason = Api::SearchCompletionReason::Error;
            completionErrorMessage = "Failed to store plan: " + storeResult.errorValue();
        }
        else {
            savedSummary = search.getPlan().summary;
            const Api::PlanSaved saved{
                .summary = search.getPlan().summary,
            };
            dsm.broadcastEventData(Api::PlanSaved::name(), Network::serialize_payload(saved));
        }
    }

    const Api::SearchCompleted completed{
        .reason = completionReason,
        .summary = savedSummary,
        .errorMessage = completionErrorMessage,
    };
    dsm.broadcastEventData(Api::SearchCompleted::name(), Network::serialize_payload(completed));
    return Idle{};
}

Any SearchActive::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

Any SearchActive::onEvent(const Api::SearchPauseSet::Cwc& cwc, StateMachine& dsm)
{
    search.pauseSet(cwc.command.paused);

    Api::SearchPauseSet::Okay response{
        .paused = search.isPaused(),
    };
    cwc.sendResponse(Api::SearchPauseSet::Response::okay(std::move(response)));
    broadcastSearchProgress(dsm, search.getProgress());
    lastProgressBroadcastTime_ = std::chrono::steady_clock::now();
    return std::move(*this);
}

Any SearchActive::onEvent(const Api::SearchProgressGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    Api::SearchProgressGet::Okay response{
        .progress = search.getProgress(),
    };
    cwc.sendResponse(Api::SearchProgressGet::Response::okay(std::move(response)));
    return std::move(*this);
}

Any SearchActive::onEvent(const Api::SearchStop::Cwc& cwc, StateMachine& /*dsm*/)
{
    search.stop();
    cwc.sendResponse(Api::SearchStop::Response::okay(Api::SearchStop::Okay{ .stopped = true }));
    return std::move(*this);
}

} // namespace State
} // namespace Server
} // namespace DirtSim
