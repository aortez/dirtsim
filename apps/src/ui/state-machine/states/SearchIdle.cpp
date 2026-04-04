#include "SearchIdle.h"
#include "PlanPlayback.h"
#include "SearchActive.h"
#include "SearchHelpers.h"
#include "SearchPlanBrowser.h"
#include "StartMenu.h"
#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/ExpandablePanel.h"
#include "ui/state-machine/StateMachine.h"
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {
namespace {

std::optional<std::string> playbackStoppedError(const Api::PlanPlaybackStopped& stopped)
{
    if (stopped.reason != Api::PlanPlaybackStopReason::Error) {
        return std::nullopt;
    }
    if (stopped.errorMessage.empty()) {
        return std::string("Plan playback failed");
    }
    return "Plan playback failed: " + stopped.errorMessage;
}

} // namespace

SearchIdle::SearchIdle(
    std::optional<Api::PlanSummary> lastSavedPlan,
    std::optional<UUID> selectedPlanId,
    std::optional<std::string> lastError)
    : lastError_(std::move(lastError)),
      lastSavedPlan_(std::move(lastSavedPlan)),
      selectedPlanId_(std::move(selectedPlanId))
{}

void SearchIdle::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering SearchIdle state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(contentArea, "SearchIdle requires a menu content area");
    lv_obj_clean(contentArea);

    contentRoot_ = lv_obj_create(contentArea);
    lv_obj_set_size(contentRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(contentRoot_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        contentRoot_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(contentRoot_, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(contentRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(contentRoot_, 0, 0);
    lv_obj_set_style_pad_all(contentRoot_, 24, 0);
    lv_obj_set_style_pad_row(contentRoot_, 16, 0);
    lv_obj_clear_flag(contentRoot_, LV_OBJ_FLAG_SCROLLABLE);

    auto* titleLabel = lv_label_create(contentRoot_);
    lv_label_set_text(titleLabel, "Search");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_28, 0);

    bodyLabel_ = lv_label_create(contentRoot_);
    lv_obj_set_width(bodyLabel_, LV_PCT(80));
    lv_label_set_long_mode(bodyLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(bodyLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(bodyLabel_, lv_color_hex(0xC8D2DC), 0);
    lv_obj_set_style_text_font(bodyLabel_, &lv_font_montserrat_18, 0);

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
    iconRail->setMinimizedAffordanceStyle(IconRail::minimizedAffordanceLeftCenter());
    iconRail->deselectAll();
    updateVisibleIcons(sm);

    updateBodyText();
}

void SearchIdle::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting SearchIdle state");

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
        }
    }

    if (contentRoot_ != nullptr) {
        lv_obj_del(contentRoot_);
        contentRoot_ = nullptr;
        bodyLabel_ = nullptr;
    }
}

void SearchIdle::updateVisibleIcons(StateMachine& sm)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    iconRail->setVisibleIcons({ IconId::DUCK, IconId::SCANNER, IconId::PLAN_BROWSER });
}

void SearchIdle::updateBodyText()
{
    DIRTSIM_ASSERT(bodyLabel_, "SearchIdle body label must exist");

    std::string text = "SMB1 phase-1 search.\nUse SCANNER to run search.";
    text += "\n\nUse PLAN_BROWSER to browse saved plans and play them.";

    if (lastError_.has_value()) {
        text += "\n\nLast error:\n";
        text += lastError_.value();
    }

    lv_label_set_text(bodyLabel_, text.c_str());
}

State::Any SearchIdle::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "SearchIdle icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        return StartMenu{};
    }

    if (evt.selectedId == IconId::SCANNER) {
        const auto startResult = SearchHelpers::startSearch(sm);
        if (startResult.isError()) {
            lastError_ = startResult.errorValue();
            updateBodyText();
            return std::move(*this);
        }

        lastError_.reset();
        return SearchActive{};
    }

    if (evt.selectedId == IconId::PLAN_BROWSER) {
        lastError_.reset();
        return SearchPlanBrowser{ lastSavedPlan_, selectedPlanId_ };
    }

    if (evt.selectedId == IconId::NONE) {
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in SearchIdle");
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const PlanPlaybackStoppedReceivedEvent& evt, StateMachine& /*sm*/)
{
    selectedPlanId_ = evt.stopped.planId;
    lastError_ = playbackStoppedError(evt.stopped);
    updateBodyText();
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm)
{
    lastSavedPlan_ = evt.saved.summary;
    selectedPlanId_ = evt.saved.summary.id;
    lastError_.reset();
    updateVisibleIcons(sm);
    updateBodyText();
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const UiApi::PlanBrowserOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    lastError_.reset();
    cwc.sendResponse(
        UiApi::PlanBrowserOpen::Response::okay(UiApi::PlanBrowserOpen::Okay{ .opened = true }));
    return SearchPlanBrowser{ lastSavedPlan_, selectedPlanId_ };
}

State::Any SearchIdle::onEvent(const UiApi::PlanDetailOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    cwc.sendResponse(UiApi::PlanDetailOpen::Response::error(ApiError("Plan browser not open")));
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const UiApi::PlanDetailSelect::Cwc& cwc, StateMachine& /*sm*/)
{
    cwc.sendResponse(UiApi::PlanDetailSelect::Response::error(ApiError("Plan browser not open")));
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const UiApi::PlanPlaybackStart::Cwc& cwc, StateMachine& sm)
{
    const auto startResult = SearchHelpers::startPlanPlayback(sm, cwc.command.planId);
    if (startResult.isError()) {
        lastError_ = startResult.errorValue();
        cwc.sendResponse(UiApi::PlanPlaybackStart::Response::error(ApiError(lastError_.value())));
        updateBodyText();
        return std::move(*this);
    }

    selectedPlanId_ = cwc.command.planId;
    lastError_.reset();
    cwc.sendResponse(
        UiApi::PlanPlaybackStart::Response::okay(UiApi::PlanPlaybackStart::Okay{ .queued = true }));
    return PlanPlayback{ cwc.command.planId };
}

State::Any SearchIdle::onEvent(const UiApi::SearchStart::Cwc& cwc, StateMachine& sm)
{
    const auto startResult = SearchHelpers::startSearch(sm);
    if (startResult.isError()) {
        lastError_ = startResult.errorValue();
        cwc.sendResponse(UiApi::SearchStart::Response::error(ApiError(lastError_.value())));
        updateBodyText();
        return std::move(*this);
    }

    lastError_.reset();
    cwc.sendResponse(
        UiApi::SearchStart::Response::okay(UiApi::SearchStart::Okay{ .queued = true }));
    return SearchActive{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
