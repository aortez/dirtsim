#include "SearchPlanBrowser.h"
#include "PlanPlayback.h"
#include "SearchActive.h"
#include "SearchIdle.h"
#include "StartMenu.h"
#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/PlanPlaybackStart.h"
#include "server/api/SearchStart.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/ExpandablePanel.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace State {
namespace {

constexpr int kServerTimeoutMs = 2000;

} // namespace

SearchPlanBrowser::SearchPlanBrowser(
    std::optional<Api::PlanSummary> lastSavedPlan, std::optional<UUID> selectedPlanId)
    : lastSavedPlan_(std::move(lastSavedPlan)), selectedPlanId_(std::move(selectedPlanId))
{}

void SearchPlanBrowser::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering SearchPlanBrowser state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(contentArea, "SearchPlanBrowser requires a menu content area");
    lv_obj_clean(contentArea);

    contentRoot_ = lv_obj_create(contentArea);
    lv_obj_set_size(contentRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(contentRoot_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        contentRoot_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(contentRoot_, lv_color_hex(0x101214), 0);
    lv_obj_set_style_bg_opa(contentRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(contentRoot_, 0, 0);
    lv_obj_set_style_pad_all(contentRoot_, 16, 0);
    lv_obj_set_style_pad_row(contentRoot_, 12, 0);
    lv_obj_clear_flag(contentRoot_, LV_OBJ_FLAG_SCROLLABLE);

    browserHost_ = lv_obj_create(contentRoot_);
    lv_obj_set_size(browserHost_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(browserHost_, 1);
    lv_obj_set_style_bg_opa(browserHost_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(browserHost_, 0, 0);
    lv_obj_set_style_pad_all(browserHost_, 0, 0);
    lv_obj_clear_flag(browserHost_, LV_OBJ_FLAG_SCROLLABLE);

    errorLabel_ = lv_label_create(contentRoot_);
    lv_obj_set_width(errorLabel_, LV_PCT(100));
    lv_label_set_long_mode(errorLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(errorLabel_, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(errorLabel_, &lv_font_montserrat_14, 0);

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
    updateVisibleIcons(sm);
    if (iconRail->getSelectedIcon() != IconId::PLAN_BROWSER
        && iconRail->isIconSelectable(IconId::PLAN_BROWSER)) {
        iconRail->selectIcon(IconId::PLAN_BROWSER);
    }

    planBrowserPanel_ = std::make_unique<PlanBrowserPanel>(
        browserHost_,
        &sm.getWebSocketService(),
        selectedPlanId_,
        [this, &sm](const PlanBrowserState& state) { applyBrowserState(state, sm); });
    updateErrorText();
}

void SearchPlanBrowser::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting SearchPlanBrowser state");
    planBrowserPanel_.reset();

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
        }
    }

    if (contentRoot_ != nullptr) {
        lv_obj_del(contentRoot_);
        browserHost_ = nullptr;
        contentRoot_ = nullptr;
        errorLabel_ = nullptr;
    }
}

void SearchPlanBrowser::applyBrowserState(const PlanBrowserState& state, StateMachine& sm)
{
    lastSavedPlan_ = state.latestPlan;
    if (state.selectedPlan.has_value()) {
        selectedPlanId_ = state.selectedPlan->id;
    }
    else {
        selectedPlanId_.reset();
    }

    updateVisibleIcons(sm);
    updateErrorText();
}

Result<std::monostate, std::string> SearchPlanBrowser::startPlanPlayback(
    StateMachine& sm, UUID planId)
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

Result<std::monostate, std::string> SearchPlanBrowser::startSearch(StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<std::monostate, std::string>::error("UI is not connected to the server");
    }

    Api::SearchStart::Command command{};
    const auto result =
        wsService.sendCommandAndGetResponse<Api::SearchStart::OkayType>(command, kServerTimeoutMs);
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(result.errorValue());
    }
    if (result.value().isError()) {
        return Result<std::monostate, std::string>::error(result.value().errorValue().message);
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void SearchPlanBrowser::updateErrorText()
{
    DIRTSIM_ASSERT(errorLabel_, "SearchPlanBrowser error label must exist");

    if (!lastError_.has_value()) {
        lv_label_set_text(errorLabel_, "");
        return;
    }

    lv_label_set_text(errorLabel_, lastError_->c_str());
}

void SearchPlanBrowser::updateVisibleIcons(StateMachine& sm)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    std::vector<IconId> visibleIcons{ IconId::DUCK, IconId::SCANNER, IconId::PLAN_BROWSER };
    if (selectedPlanId_.has_value()) {
        visibleIcons.push_back(IconId::PLAY);
    }
    iconRail->setVisibleIcons(visibleIcons);
}

State::Any SearchPlanBrowser::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "SearchPlanBrowser icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        return StartMenu{};
    }

    if (evt.selectedId == IconId::SCANNER) {
        const auto startResult = startSearch(sm);
        if (startResult.isError()) {
            lastError_ = startResult.errorValue();
            updateErrorText();
            return std::move(*this);
        }

        lastError_.reset();
        updateErrorText();
        return SearchActive{};
    }

    if (evt.selectedId == IconId::PLAN_BROWSER) {
        return std::move(*this);
    }

    if (evt.selectedId == IconId::PLAY) {
        DIRTSIM_ASSERT(selectedPlanId_.has_value(), "PLAY requires a selected plan");
        const auto startResult = startPlanPlayback(sm, selectedPlanId_.value());
        if (startResult.isError()) {
            lastError_ = startResult.errorValue();
            updateErrorText();
            return std::move(*this);
        }

        lastError_.reset();
        updateErrorText();
        return PlanPlayback{ selectedPlanId_.value() };
    }

    if (evt.selectedId == IconId::NONE) {
        return SearchIdle{ lastSavedPlan_, selectedPlanId_ };
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in SearchPlanBrowser");
    return std::move(*this);
}

State::Any SearchPlanBrowser::onEvent(const PlanSavedReceivedEvent& evt, StateMachine& sm)
{
    lastSavedPlan_ = evt.saved.summary;
    selectedPlanId_ = evt.saved.summary.id;
    lastError_.reset();
    if (planBrowserPanel_) {
        planBrowserPanel_->refresh();
    }
    updateVisibleIcons(sm);
    updateErrorText();
    return std::move(*this);
}

State::Any SearchPlanBrowser::onEvent(const UiApi::PlanBrowserOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    lastError_.reset();
    updateErrorText();
    cwc.sendResponse(
        UiApi::PlanBrowserOpen::Response::okay(UiApi::PlanBrowserOpen::Okay{ .opened = true }));
    return std::move(*this);
}

State::Any SearchPlanBrowser::onEvent(const UiApi::PlanDetailOpen::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::PlanDetailOpen::Response;

    if (!planBrowserPanel_) {
        cwc.sendResponse(Response::error(ApiError("Plan browser not available")));
        return std::move(*this);
    }

    Result<UUID, std::string> result;
    if (cwc.command.id.has_value()) {
        result = planBrowserPanel_->openDetailById(cwc.command.id.value());
    }
    else {
        result = planBrowserPanel_->openDetailByIndex(cwc.command.index);
    }
    if (result.isError()) {
        cwc.sendResponse(Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        Response::okay(
            UiApi::PlanDetailOpen::Okay{
                .opened = true,
                .id = result.value(),
            }));
    return std::move(*this);
}

State::Any SearchPlanBrowser::onEvent(const UiApi::PlanDetailSelect::Cwc& cwc, StateMachine& /*sm*/)
{
    using Response = UiApi::PlanDetailSelect::Response;

    if (!planBrowserPanel_) {
        cwc.sendResponse(Response::error(ApiError("Plan browser not available")));
        return std::move(*this);
    }

    const auto result = planBrowserPanel_->selectDetailForId(cwc.command.id);
    if (result.isError()) {
        cwc.sendResponse(Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(Response::okay(UiApi::PlanDetailSelect::Okay{ .selected = true }));
    return std::move(*this);
}

State::Any SearchPlanBrowser::onEvent(const UiApi::PlanPlaybackStart::Cwc& cwc, StateMachine& sm)
{
    const auto startResult = startPlanPlayback(sm, cwc.command.planId);
    if (startResult.isError()) {
        lastError_ = startResult.errorValue();
        cwc.sendResponse(UiApi::PlanPlaybackStart::Response::error(ApiError(lastError_.value())));
        updateErrorText();
        return std::move(*this);
    }

    selectedPlanId_ = cwc.command.planId;
    lastError_.reset();
    updateVisibleIcons(sm);
    updateErrorText();
    cwc.sendResponse(
        UiApi::PlanPlaybackStart::Response::okay(UiApi::PlanPlaybackStart::Okay{ .queued = true }));
    return PlanPlayback{ cwc.command.planId };
}

State::Any SearchPlanBrowser::onEvent(const UiApi::SearchStart::Cwc& cwc, StateMachine& sm)
{
    const auto startResult = startSearch(sm);
    if (startResult.isError()) {
        lastError_ = startResult.errorValue();
        cwc.sendResponse(UiApi::SearchStart::Response::error(ApiError(lastError_.value())));
        updateErrorText();
        return std::move(*this);
    }

    lastError_.reset();
    updateErrorText();
    cwc.sendResponse(
        UiApi::SearchStart::Response::okay(UiApi::SearchStart::Okay{ .queued = true }));
    return SearchActive{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
