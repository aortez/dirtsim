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

    view_ = std::make_unique<SearchIdleView>(contentArea, *iconRail);
    view_->setLastError(lastError_);
}

void SearchIdle::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting SearchIdle state");

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
        }
    }

    view_.reset();
}

void SearchIdle::updateAnimations()
{
    if (view_) {
        view_->updateAnimations();
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
            if (view_) {
                view_->setLastError(lastError_);
            }
            return std::move(*this);
        }

        lastError_.reset();
        if (view_) {
            view_->setLastError(lastError_);
        }
        return SearchActive{};
    }

    if (evt.selectedId == IconId::PLAN_BROWSER) {
        lastError_.reset();
        if (view_) {
            view_->setLastError(lastError_);
        }
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
    if (view_) {
        view_->setLastError(lastError_);
    }
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm)
{
    lastSavedPlan_ = evt.saved.summary;
    selectedPlanId_ = evt.saved.summary.id;
    lastError_.reset();
    updateVisibleIcons(sm);
    if (view_) {
        view_->setLastError(lastError_);
    }
    return std::move(*this);
}

State::Any SearchIdle::onEvent(const UiApi::PlanBrowserOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    lastError_.reset();
    if (view_) {
        view_->setLastError(lastError_);
    }
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
        if (view_) {
            view_->setLastError(lastError_);
        }
        return std::move(*this);
    }

    selectedPlanId_ = cwc.command.planId;
    lastError_.reset();
    if (view_) {
        view_->setLastError(lastError_);
    }
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
        if (view_) {
            view_->setLastError(lastError_);
        }
        return std::move(*this);
    }

    lastError_.reset();
    if (view_) {
        view_->setLastError(lastError_);
    }
    cwc.sendResponse(
        UiApi::SearchStart::Response::okay(UiApi::SearchStart::Okay{ .queued = true }));
    return SearchActive{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
