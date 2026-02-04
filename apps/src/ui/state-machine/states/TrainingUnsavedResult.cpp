#include "TrainingUnsavedResult.h"
#include "State.h"
#include "TrainingActive.h"
#include "TrainingIdle.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"
#include "ui/TrainingUnsavedResultView.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <utility>

namespace DirtSim {
namespace Ui {
namespace State {
namespace {

Result<Api::TrainingResultSave::OkayType, std::string> saveTrainingResultToServer(
    StateMachine& sm, const std::vector<GenomeId>& ids, bool restart)
{
    if (ids.empty()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error("No ids provided");
    }
    if (!sm.hasWebSocketService()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(
            "No WebSocketService available");
    }

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(
            "Not connected to server");
    }

    Api::TrainingResultSave::Command cmd;
    cmd.ids = ids;
    cmd.restart = restart;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultSave::OkayType>(cmd, 5000);
    if (result.isError()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(result.errorValue());
    }
    if (result.value().isError()) {
        return Result<Api::TrainingResultSave::OkayType, std::string>::error(
            result.value().errorValue().message);
    }

    return Result<Api::TrainingResultSave::OkayType, std::string>::okay(result.value().value());
}

} // namespace

TrainingUnsavedResult::TrainingUnsavedResult(
    TrainingSpec lastTrainingSpec,
    bool hasTrainingSpec,
    Api::TrainingResult::Summary summary,
    std::vector<Api::TrainingResult::Candidate> candidates)
    : lastTrainingSpec_(std::move(lastTrainingSpec)),
      hasTrainingSpec_(hasTrainingSpec),
      summary_(std::move(summary)),
      candidates_(std::move(candidates))
{}

TrainingUnsavedResult::~TrainingUnsavedResult() = default;

void TrainingUnsavedResult::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training unsaved-result state");

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    view_ = std::make_unique<TrainingUnsavedResultView>(uiManager, sm);

    if (auto* iconRail = uiManager->getIconRail()) {
        if (lv_obj_t* railContainer = iconRail->getContainer()) {
            lv_obj_add_flag(railContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(railContainer, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    }
    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->clearContent();
        panel->hide();
        panel->resetWidth();
    }

    if (view_) {
        view_->showTrainingResultModal(summary_, candidates_);
    }
}

void TrainingUnsavedResult::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting Training unsaved-result state");
}

void TrainingUnsavedResult::updateAnimations()
{
    if (view_) {
        view_->updateAnimations();
    }
}

bool TrainingUnsavedResult::isTrainingResultModalVisible() const
{
    return view_ && view_->isTrainingResultModalVisible();
}

State::Any TrainingUnsavedResult::onEvent(
    const TrainingResultSaveClickedEvent& evt, StateMachine& sm)
{
    LOG_INFO(State, "Training result save requested (count={})", evt.ids.size());

    if (evt.ids.empty()) {
        LOG_WARN(State, "Training result save ignored: no ids provided");
        return std::move(*this);
    }

    const auto result = saveTrainingResultToServer(sm, evt.ids, evt.restart);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultSave failed: {}", result.errorValue());
        return std::move(*this);
    }

    if (evt.restart) {
        return TrainingActive{ lastTrainingSpec_, hasTrainingSpec_ };
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(
    const TrainingResultDiscardClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Training result discard requested");

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        return std::move(*this);
    }
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot discard training result");
        return std::move(*this);
    }

    Api::TrainingResultDiscard::Command cmd;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultDiscard::OkayType>(cmd, 5000);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultDiscard failed: {}", result.errorValue());
        return std::move(*this);
    }
    if (result.value().isError()) {
        LOG_ERROR(State, "TrainingResultDiscard error: {}", result.value().errorValue().message);
        return std::move(*this);
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(
    const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& sm)
{
    if (!view_ || !view_->isTrainingResultModalVisible()) {
        cwc.sendResponse(
            UiApi::TrainingResultSave::Response::error(
                ApiError("Training result modal not visible")));
        return std::move(*this);
    }

    std::vector<GenomeId> ids;
    if (cwc.command.count.has_value()) {
        ids = view_->getTrainingResultSaveIdsForCount(cwc.command.count.value());
    }
    else {
        ids = view_->getTrainingResultSaveIds();
    }
    if (ids.empty()) {
        cwc.sendResponse(
            UiApi::TrainingResultSave::Response::error(ApiError("No candidates selected")));
        return std::move(*this);
    }

    const bool restartRequested = cwc.command.restart;
    const auto saveResult = saveTrainingResultToServer(sm, ids, restartRequested);
    if (saveResult.isError()) {
        LOG_ERROR(State, "TrainingResultSave failed: {}", saveResult.errorValue());
        cwc.sendResponse(
            UiApi::TrainingResultSave::Response::error(ApiError(saveResult.errorValue())));
        return std::move(*this);
    }

    UiApi::TrainingResultSave::Okay response{
        .queued = false,
        .savedCount = saveResult.value().savedCount,
        .discardedCount = saveResult.value().discardedCount,
        .savedIds = saveResult.value().savedIds,
    };
    cwc.sendResponse(UiApi::TrainingResultSave::Response::okay(std::move(response)));

    if (restartRequested) {
        return TrainingActive{ lastTrainingSpec_, hasTrainingSpec_ };
    }

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(
    const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& sm)
{
    if (!view_ || !view_->isTrainingResultModalVisible()) {
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(
                ApiError("Training result modal not visible")));
        return std::move(*this);
    }

    if (!sm.hasWebSocketService()) {
        LOG_ERROR(State, "No WebSocketService available");
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(ApiError("No WebSocketService")));
        return std::move(*this);
    }
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, cannot discard training result");
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(ApiError("Not connected to server")));
        return std::move(*this);
    }

    Api::TrainingResultDiscard::Command cmd;
    const auto result =
        wsService.sendCommandAndGetResponse<Api::TrainingResultDiscard::OkayType>(cmd, 5000);
    if (result.isError()) {
        LOG_ERROR(State, "TrainingResultDiscard failed: {}", result.errorValue());
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }
    if (result.value().isError()) {
        LOG_ERROR(State, "TrainingResultDiscard error: {}", result.value().errorValue().message);
        cwc.sendResponse(
            UiApi::TrainingResultDiscard::Response::error(
                ApiError(result.value().errorValue().message)));
        return std::move(*this);
    }

    cwc.sendResponse(UiApi::TrainingResultDiscard::Response::okay({ .queued = true }));

    if (view_) {
        view_->hideTrainingResultModal();
    }

    return TrainingIdle{ lastTrainingSpec_, hasTrainingSpec_ };
}

State::Any TrainingUnsavedResult::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(
    const EvolutionProgressReceivedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any TrainingUnsavedResult::onEvent(
    const TrainingBestSnapshotReceivedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
