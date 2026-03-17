#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace State {

namespace {

UiApi::NetworkDiagnosticsGet::Okay toAutomationOkay(
    const NetworkDiagnosticsPanel::AutomationState& state)
{
    UiApi::NetworkDiagnosticsGet::Okay okay{
        .connect_cancel_enabled = state.connectCancelEnabled,
        .connect_cancel_visible = state.connectCancelVisible,
        .connect_overlay_visible = state.connectOverlayVisible,
        .password_prompt_visible = state.passwordPromptVisible,
        .password_submit_enabled = state.passwordSubmitEnabled,
        .connect_progress = std::nullopt,
        .connected_ssid = state.connectedSsid,
        .connect_target_ssid = state.connectTargetSsid,
        .password_prompt_target_ssid = state.passwordPromptTargetSsid,
        .password_error = state.passwordError,
        .networks = {},
        .view_mode = state.viewMode,
        .wifi_status_message = state.wifiStatusMessage,
    };
    if (state.connectProgress.has_value()) {
        okay.connect_progress = UiApi::NetworkDiagnosticsGet::ConnectProgressInfo{
            .phase = state.connectProgress->phase,
            .ssid = state.connectProgress->ssid,
            .can_cancel = state.connectProgress->canCancel,
        };
    }
    okay.networks.reserve(state.networks.size());
    for (const auto& network : state.networks) {
        okay.networks.push_back(
            UiApi::NetworkDiagnosticsGet::NetworkInfo{
                .ssid = network.ssid,
                .status = network.status,
                .requires_password = network.requiresPassword,
            });
    }
    return okay;
}

} // namespace

void Network::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Network state");

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(contentArea, "Network state requires a menu content area");

    lv_obj_clean(contentArea);

    contentRoot_ = lv_obj_create(contentArea);
    lv_obj_set_size(contentRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(contentRoot_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(contentRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(contentRoot_, 20, 0);
    lv_obj_set_style_pad_right(contentRoot_, 20, 0);
    lv_obj_set_style_pad_bottom(contentRoot_, 20, 0);
    lv_obj_set_style_pad_left(contentRoot_, IconRail::RAIL_WIDTH + 20, 0);
    lv_obj_set_style_border_width(contentRoot_, 0, 0);
    lv_obj_clear_flag(contentRoot_, LV_OBJ_FLAG_SCROLLABLE);

    activeSubviewIcon_ = IconId::NETWORK;
    networkPanel_ =
        std::make_unique<NetworkDiagnosticsPanel>(contentRoot_, sm.getUserSettingsManager());
    if (networkPanel_) {
        networkPanel_->showWifiView();
    }

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->hide();
        panel->clearContent();
        panel->resetWidth();
    }

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setVisible(true);
    iconRail->setAllowMinimize(false);
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setVisibleIcons({ IconId::DUCK, IconId::NETWORK, IconId::SETTINGS });
    iconRail->showIcons();
    iconRail->selectIcon(activeSubviewIcon_);
}

void Network::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Network state");

    networkPanel_.reset();

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
        }
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
            panel->resetWidth();
        }
    }

    if (contentRoot_) {
        lv_obj_del(contentRoot_);
        contentRoot_ = nullptr;
    }
}

State::Any Network::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    if (evt.selectedId == IconId::DUCK) {
        LOG_INFO(State, "Duck icon selected, returning to StartMenu");
        return StartMenu{};
    }

    if (evt.selectedId == IconId::NETWORK) {
        activeSubviewIcon_ = IconId::NETWORK;
        if (networkPanel_) {
            networkPanel_->showWifiView();
        }
        return std::move(*this);
    }

    if (evt.selectedId == IconId::SETTINGS) {
        activeSubviewIcon_ = IconId::SETTINGS;
        if (networkPanel_) {
            networkPanel_->showLanAccessView();
        }
        return std::move(*this);
    }

    if (evt.selectedId == IconId::NONE) {
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in Network state");
    return std::move(*this);
}

State::Any Network::onEvent(const RailModeChangedEvent& evt, StateMachine& sm)
{
    if (evt.newMode != RailMode::Minimized) {
        return std::move(*this);
    }

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->showIcons();
    iconRail->selectIcon(activeSubviewIcon_);

    return std::move(*this);
}

State::Any Network::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked, returning to StartMenu");
    return StartMenu{};
}

State::Any Network::onEvent(const UiApi::NetworkConnectCancelPress::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!networkPanel_) {
        cwc.sendResponse(
            UiApi::NetworkConnectCancelPress::Response::error(
                ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = networkPanel_->pressAutomationConnectCancel();
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkConnectCancelPress::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkConnectCancelPress::Response::okay(
            UiApi::NetworkConnectCancelPress::Okay{ .accepted = true }));
    return std::move(*this);
}

State::Any Network::onEvent(const UiApi::NetworkConnectPress::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!networkPanel_) {
        cwc.sendResponse(
            UiApi::NetworkConnectPress::Response::error(ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = networkPanel_->pressAutomationConnect(cwc.command.ssid);
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkConnectPress::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkConnectPress::Response::okay(
            UiApi::NetworkConnectPress::Okay{ .accepted = true }));
    return std::move(*this);
}

State::Any Network::onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!networkPanel_) {
        cwc.sendResponse(
            UiApi::NetworkDiagnosticsGet::Response::error(ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = networkPanel_->getAutomationState();
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkDiagnosticsGet::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkDiagnosticsGet::Response::okay(toAutomationOkay(result.value())));
    return std::move(*this);
}

State::Any Network::onEvent(const UiApi::NetworkPasswordSubmit::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!networkPanel_) {
        cwc.sendResponse(
            UiApi::NetworkPasswordSubmit::Response::error(ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = networkPanel_->submitAutomationPassword(cwc.command.password);
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkPasswordSubmit::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkPasswordSubmit::Response::okay(
            UiApi::NetworkPasswordSubmit::Okay{ .accepted = true }));
    return std::move(*this);
}

State::Any Network::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "SimStop command received, returning to StartMenu");
    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));
    return StartMenu{};
}

State::Any Network::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "StopButtonPress command received, returning to StartMenu");
    cwc.sendResponse(UiApi::StopButtonPress::Response::okay(std::monostate{}));
    return onEvent(StopButtonClickedEvent{}, sm);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
