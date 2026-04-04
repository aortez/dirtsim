#include "SearchActive.h"
#include "PlanPlayback.h"
#include "SearchIdle.h"
#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/PlanPlaybackStart.h"
#include "server/api/RenderFormatSet.h"
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

constexpr int kServerTimeoutMs = 2000;

const char* completionReasonText(Api::SearchCompletionReason reason)
{
    switch (reason) {
        case Api::SearchCompletionReason::StoppedByUser:
            return "Stopped by user";
        case Api::SearchCompletionReason::ReachedSegmentLimit:
            return "Reached segment limit";
        case Api::SearchCompletionReason::NoFurtherProgress:
            return "Could not find further progress";
        case Api::SearchCompletionReason::SearchError:
            return "Search error";
    }

    DIRTSIM_ASSERT(false, "Unhandled SearchCompletionReason");
    return "Search error";
}

Result<std::monostate, std::string> startPlanPlayback(StateMachine& sm, UUID planId)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<std::monostate, std::string>::error("UI is not connected to the server");
    }

    Api::PlanPlaybackStart::Command command{
        .planId = planId,
    };
    const auto result = wsService.sendCommandAndGetResponse<Api::PlanPlaybackStart::OkayType>(
        command, kServerTimeoutMs);
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(result.errorValue());
    }
    if (result.value().isError()) {
        return Result<std::monostate, std::string>::error(result.value().errorValue().message);
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void subscribeToBasicRender(StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "SearchActive: UI is not connected to the server");
        return;
    }

    Api::RenderFormatSet::Command command{
        .format = RenderFormat::EnumType::Basic,
        .connectionId = "",
    };
    const auto result =
        wsService.sendCommandAndGetResponse<Api::RenderFormatSet::OkayType>(command, 250);
    if (result.isError()) {
        LOG_WARN(
            State, "SearchActive: Failed to subscribe to render stream: {}", result.errorValue());
        return;
    }
    if (result.value().isError()) {
        LOG_WARN(
            State,
            "SearchActive: RenderFormatSet rejected: {}",
            result.value().errorValue().message);
    }
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

    subscribeToBasicRender(sm);
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

    if (isCompletedView()) {
        lv_label_set_text(titleLabel_, "Search Complete");
    }
    else {
        lv_label_set_text(titleLabel_, "Search Active");
    }

    std::string text;
    if (completedSearch_.has_value()) {
        text = completionReasonText(completedSearch_->reason);
        text += "\n";
    }
    else {
        text = progress_.paused ? "Paused." : "Running.";
    }
    text += "\nElapsed frames: ";
    text += std::to_string(progress_.elapsedFrames);
    text += "\nBest frontier: ";
    text += std::to_string(progress_.bestFrontier);
    text += "\nDepth: ";
    text += std::to_string(progress_.beamWidth);
    text += "\nExpanded: ";
    text += std::to_string(progress_.expandedNodeCount);
    text += "\nAttempt: ";
    text += std::to_string(progress_.attemptIndex);
    text += "\nBacktracks: ";
    text += std::to_string(progress_.backtrackCount);
    text += "\nSegment: ";
    text += std::to_string(progress_.segmentIndex);
    text += "/";
    if (progress_.maxSegments == 0u) {
        text += "unlimited";
    }
    else {
        text += std::to_string(progress_.maxSegments);
    }
    text += "\nCandidate: ";
    text += std::to_string(progress_.candidateIndex);
    text += "/";
    text += std::to_string(progress_.candidateCount);
    text += "\nFrames / segment: ";
    text += std::to_string(progress_.maxSteps);

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
            const auto startResult = startPlanPlayback(sm, savedPlan_->id);
            if (startResult.isError()) {
                lastError_ = startResult.errorValue();
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
            return SearchIdle{ savedPlan_,
                               savedPlan_.has_value() ? std::make_optional(savedPlan_->id)
                                                      : std::nullopt };
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

State::Any SearchActive::onEvent(const PlanSavedReceivedEvent& evt, StateMachine& /*sm*/)
{
    savedPlan_ = evt.saved.summary;
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
    if (!completedSearch_.has_value()) {
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
        command, kServerTimeoutMs);
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
    const auto result =
        wsService.sendCommandAndGetResponse<Api::SearchStop::OkayType>(command, kServerTimeoutMs);
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
