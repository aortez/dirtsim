#include "SearchActive.h"
#include "PlanPlayback.h"
#include "SearchHelpers.h"
#include "SearchIdle.h"
#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/SearchPauseSet.h"
#include "server/api/SearchStop.h"
#include "ui/UiComponentManager.h"
#include "ui/UiServices.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/StateMachine.h"
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {
namespace {

const char* completionReasonText(Api::SearchCompletionReason reason)
{
    switch (reason) {
        case Api::SearchCompletionReason::Completed:
            return "Completed.";
        case Api::SearchCompletionReason::Stopped:
            return "Stopped.";
        case Api::SearchCompletionReason::Error:
            return "Error.";
    }

    DIRTSIM_ASSERT(false, "Unhandled SearchCompletionReason");
    return "Error.";
}

const char* searchProgressEventText(Api::SearchProgressEvent event)
{
    switch (event) {
        case Api::SearchProgressEvent::Unknown:
            return "Unknown";
        case Api::SearchProgressEvent::RootInitialized:
            return "Root";
        case Api::SearchProgressEvent::ExpandedAlive:
            return "Expanded";
        case Api::SearchProgressEvent::Backtracked:
            return "Backtracked";
        case Api::SearchProgressEvent::PrunedDead:
            return "Pruned dead";
        case Api::SearchProgressEvent::PrunedStalled:
            return "Pruned stalled";
        case Api::SearchProgressEvent::PrunedVelocityStuck:
            return "Pruned velocity";
        case Api::SearchProgressEvent::PrunedBelowScreen:
            return "Pruned below";
        case Api::SearchProgressEvent::CompletedBudgetExceeded:
            return "Budget hit";
        case Api::SearchProgressEvent::CompletedExhausted:
            return "Exhausted";
        case Api::SearchProgressEvent::CompletedMilestoneReached:
            return "Milestone";
        case Api::SearchProgressEvent::Stopped:
            return "Stopped";
        case Api::SearchProgressEvent::Error:
            return "Error";
    }

    DIRTSIM_ASSERT(false, "Unhandled SearchProgressEvent");
    return "Unknown";
}

} // namespace

void SearchActive::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering SearchActive state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getSimulationContainer();

    UiServices& uiServices = static_cast<UiServices&>(sm);
    EventSink& eventSink = static_cast<EventSink&>(sm);
    playground_ = std::make_unique<SimPlayground>(
        uiManager, &sm.getWebSocketService(), uiServices, eventSink, &sm.getFractalAnimator());
    DIRTSIM_ASSERT(playground_, "SearchActive playground creation failed");

    statusCard_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(statusCard_, 280, LV_SIZE_CONTENT);
    lv_obj_align(statusCard_, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_set_style_bg_color(statusCard_, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(statusCard_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(statusCard_, 0, 0);
    lv_obj_set_style_radius(statusCard_, 14, 0);
    lv_obj_set_style_pad_all(statusCard_, 14, 0);
    lv_obj_set_style_pad_row(statusCard_, 10, 0);
    lv_obj_set_flex_flow(statusCard_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statusCard_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(statusCard_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(statusCard_);

    titleLabel_ = lv_label_create(statusCard_);
    lv_label_set_text(titleLabel_, "Search Active");
    lv_obj_set_style_text_color(titleLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel_, &lv_font_montserrat_24, 0);

    bodyLabel_ = lv_label_create(statusCard_);
    lv_obj_set_width(bodyLabel_, LV_PCT(100));
    lv_label_set_long_mode(bodyLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(bodyLabel_, lv_color_hex(0xD7E2EC), 0);
    lv_obj_set_style_text_font(bodyLabel_, &lv_font_montserrat_16, 0);

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->clearContent();
        panel->hide();
        panel->resetWidth();
    }

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setAllowMinimize(false);
    iconRail->setVisible(true);
    iconRail->showIcons();
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setMinimizedAffordanceStyle(IconRail::minimizedAffordanceLeftTopSquare());
    iconRail->deselectAll();
    updateVisibleIcons(sm);

    SearchHelpers::subscribeToBasicRender(sm);
    updateBodyText();
}

void SearchActive::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting SearchActive state");
    playground_.reset();
    worldData_.reset();

    if (statusCard_ != nullptr) {
        lv_obj_del(statusCard_);
        statusCard_ = nullptr;
        titleLabel_ = nullptr;
        bodyLabel_ = nullptr;
    }

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
        }
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
        }
    }
}

void SearchActive::updateVisibleIcons(StateMachine& sm)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    std::vector<IconId> visibleIcons;
    if (isCompletedView()) {
        if (savedPlan_.has_value()) {
            visibleIcons.push_back(IconId::PLAY);
        }
        visibleIcons.push_back(IconId::BACK);
    }
    else {
        if (progress_.paused) {
            visibleIcons.push_back(IconId::PLAY);
        }
        else {
            visibleIcons.push_back(IconId::PAUSE);
        }
        visibleIcons.push_back(IconId::STOP);
    }
    iconRail->setVisibleIcons(visibleIcons);
}

void SearchActive::updateBodyText()
{
    DIRTSIM_ASSERT(bodyLabel_, "SearchActive body label must exist");
    DIRTSIM_ASSERT(titleLabel_, "SearchActive title label must exist");

    if (completedSearch_.has_value()
        && completedSearch_->reason == Api::SearchCompletionReason::Error) {
        lv_label_set_text(titleLabel_, "Search Error");
    }
    else if (isCompletedView()) {
        lv_label_set_text(titleLabel_, "Search Complete");
    }
    else {
        lv_label_set_text(titleLabel_, "Search Active");
    }

    std::string text;
    if (completedSearch_.has_value()) {
        text = completionReasonText(completedSearch_->reason);
    }
    else {
        text = progress_.paused ? "Paused." : "Running.";
    }
    text += "\nSearched nodes: ";
    text += std::to_string(progress_.searchedNodeCount);
    text += "\nCurrent frame: ";
    text += std::to_string(progress_.currentGameplayFrame);
    text += "\nBest frontier: ";
    text += std::to_string(progress_.bestFrontier);
    text += "\nLast event: ";
    text += searchProgressEventText(progress_.lastSearchEvent);

    if (completedSearch_.has_value() && !completedSearch_->errorMessage.empty()) {
        text += "\n\nError:\n";
        text += completedSearch_->errorMessage;
    }

    if (lastError_.has_value()) {
        text += "\n\nLast error:\n";
        text += lastError_.value();
    }

    lv_label_set_text(bodyLabel_, text.c_str());
}

State::Any SearchActive::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "SearchActive icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* iconRail = sm.getUiComponentManager()->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    if (isCompletedView()) {
        if (evt.selectedId == IconId::PLAY && savedPlan_.has_value()) {
            const auto playbackResult = SearchHelpers::startPlanPlayback(sm, savedPlan_->id);
            if (playbackResult.isError()) {
                lastError_ = playbackResult.errorValue();
                updateBodyText();
                iconRail->deselectAll();
                return std::move(*this);
            }

            lastError_.reset();
            iconRail->deselectAll();
            return PlanPlayback{ savedPlan_->id };
        }

        if (evt.selectedId == IconId::BACK) {
            iconRail->deselectAll();
            return SearchIdle{
                savedPlan_,
                savedPlan_.has_value() ? std::make_optional(savedPlan_->id) : std::nullopt,
            };
        }

        if (evt.selectedId == IconId::NONE) {
            return std::move(*this);
        }

        LOG_WARN(State, "Ignoring unsupported icon selection during completed SearchActive");
        iconRail->deselectAll();
        return std::move(*this);
    }

    if (evt.selectedId == IconId::PAUSE || evt.selectedId == IconId::PLAY) {
        const bool paused = evt.selectedId == IconId::PAUSE;
        auto nextState = onEvent(
            UiApi::SearchPauseSet::Cwc{
                UiApi::SearchPauseSet::Command{ .paused = paused },
                nullptr,
            },
            sm);
        iconRail->deselectAll();
        return nextState;
    }

    if (evt.selectedId == IconId::STOP) {
        auto nextState =
            onEvent(UiApi::SearchStop::Cwc{ UiApi::SearchStop::Command{}, nullptr }, sm);
        iconRail->deselectAll();
        return nextState;
    }

    if (evt.selectedId == IconId::NONE) {
        return std::move(*this);
    }

    LOG_WARN(State, "Ignoring unsupported icon selection during SearchActive");
    iconRail->deselectAll();
    return std::move(*this);
}

State::Any SearchActive::onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm)
{
    savedPlan_ = evt.saved.summary;
    updateVisibleIcons(sm);
    updateBodyText();
    return std::move(*this);
}

State::Any SearchActive::onEvent(const SearchCompletedReceivedEvent& evt, StateMachine& sm)
{
    completedSearch_ = evt.completed;
    if (evt.completed.summary.has_value()) {
        savedPlan_ = evt.completed.summary;
    }
    updateVisibleIcons(sm);
    updateBodyText();
    return std::move(*this);
}

State::Any SearchActive::onEvent(const RailModeChangedEvent& /*evt*/, StateMachine& /*sm*/)
{
    if (playground_) {
        playground_->sendDisplayResizeUpdate();
    }
    return std::move(*this);
}

State::Any SearchActive::onEvent(const SearchProgressReceivedEvent& evt, StateMachine& sm)
{
    progress_ = evt.progress;
    if (!isCompletedView()) {
        lastError_.reset();
    }
    updateVisibleIcons(sm);
    updateBodyText();
    return std::move(*this);
}

State::Any SearchActive::onEvent(const UiUpdateEvent& evt, StateMachine& /*sm*/)
{
    if (!playground_) {
        return std::move(*this);
    }

    worldData_ = std::make_unique<WorldData>(evt.worldData);
    playground_->updateFromWorldData(*worldData_, evt.scenario_id, evt.scenario_config, 0.0);

    if (evt.scenarioVideoFrame.has_value()) {
        playground_->presentVideoFrame(evt.scenarioVideoFrame.value());
    }
    else {
        playground_->render(*worldData_, false);
    }

    return std::move(*this);
}

State::Any SearchActive::onEvent(const UiApi::SearchPauseSet::Cwc& cwc, StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        lastError_ = "UI is not connected to the server";
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(UiApi::SearchPauseSet::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    Api::SearchPauseSet::Command command{
        .paused = cwc.command.paused,
    };
    const auto result = wsService.sendCommandAndGetResponse<Api::SearchPauseSet::OkayType>(
        command, SearchHelpers::kServerTimeoutMs);
    if (result.isError()) {
        lastError_ = result.errorValue();
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(UiApi::SearchPauseSet::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }
    if (result.value().isError()) {
        lastError_ = result.value().errorValue().message;
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(UiApi::SearchPauseSet::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    progress_.paused = result.value().value().paused;
    lastError_.reset();
    updateVisibleIcons(sm);
    updateBodyText();
    if (cwc.callback) {
        cwc.sendResponse(
            UiApi::SearchPauseSet::Response::okay(
                UiApi::SearchPauseSet::Okay{ .paused = progress_.paused }));
    }
    return std::move(*this);
}

State::Any SearchActive::onEvent(const UiApi::SearchStop::Cwc& cwc, StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        lastError_ = "UI is not connected to the server";
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(UiApi::SearchStop::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    Api::SearchStop::Command command{};
    const auto result = wsService.sendCommandAndGetResponse<Api::SearchStop::OkayType>(
        command, SearchHelpers::kServerTimeoutMs);
    if (result.isError()) {
        lastError_ = result.errorValue();
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(UiApi::SearchStop::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }
    if (result.value().isError()) {
        lastError_ = result.value().errorValue().message;
        updateBodyText();
        if (cwc.callback) {
            cwc.sendResponse(UiApi::SearchStop::Response::error(ApiError(lastError_.value())));
        }
        return std::move(*this);
    }

    lastError_.reset();
    if (cwc.callback) {
        cwc.sendResponse(
            UiApi::SearchStop::Response::okay(UiApi::SearchStop::Okay{ .stopped = true }));
    }
    return std::move(*this);
}

bool SearchActive::isCompletedView() const
{
    return completedSearch_.has_value();
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
