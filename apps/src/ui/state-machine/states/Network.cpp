#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/NetworkDiagnosticsPanel.h"
#include "ui/state-machine/StateMachine.h"
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {
namespace Ui {
namespace State {

struct NetworkUiContext {
    lv_obj_t* contentRoot = nullptr;
    std::unique_ptr<NetworkDiagnosticsPanel> panel;
};

namespace {

std::string toAutomationScreenText(const NetworkDiagnosticsPanel::AutomationScreen screen)
{
    switch (screen) {
        case NetworkDiagnosticsPanel::AutomationScreen::LanAccess:
            return "LanAccess";
        case NetworkDiagnosticsPanel::AutomationScreen::Scanner:
            return "Scanner";
        case NetworkDiagnosticsPanel::AutomationScreen::Wifi:
            return "Wifi";
        case NetworkDiagnosticsPanel::AutomationScreen::WifiConnecting:
            return "WifiConnecting";
        case NetworkDiagnosticsPanel::AutomationScreen::WifiDetails:
            return "WifiDetails";
        case NetworkDiagnosticsPanel::AutomationScreen::WifiPassword:
            return "WifiPassword";
    }

    DIRTSIM_ASSERT(false, "Unhandled NetworkDiagnosticsPanel::AutomationScreen");
    return "Wifi";
}

UiApi::NetworkDiagnosticsGet::Okay toAutomationOkay(
    const NetworkDiagnosticsPanel::AutomationState& state)
{
    UiApi::NetworkDiagnosticsGet::Okay okay{
        .connect_cancel_enabled = state.connectCancelEnabled,
        .connect_cancel_visible = state.connectCancelVisible,
        .connect_overlay_visible = state.connectOverlayVisible,
        .password_prompt_visible = state.passwordPromptVisible,
        .password_submit_enabled = state.passwordSubmitEnabled,
        .scanner_enter_enabled = state.scannerEnterEnabled,
        .scanner_exit_enabled = state.scannerExitEnabled,
        .scanner_mode_active = state.scannerModeActive,
        .scanner_mode_available = state.scannerModeAvailable,
        .connect_progress = std::nullopt,
        .connected_ssid = state.connectedSsid,
        .connect_target_ssid = state.connectTargetSsid,
        .password_prompt_target_ssid = state.passwordPromptTargetSsid,
        .password_error = state.passwordError,
        .scanner_status_message = state.scannerStatusMessage,
        .networks = {},
        .screen = toAutomationScreenText(state.screen),
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

std::shared_ptr<NetworkUiContext> ensureNetworkUiContext(
    StateMachine& sm, std::shared_ptr<NetworkUiContext> context)
{
    if (!context) {
        context = std::make_shared<NetworkUiContext>();
    }
    if (context->panel && context->contentRoot) {
        return context;
    }

    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(contentArea, "Network state requires a menu content area");

    lv_obj_clean(contentArea);

    context->contentRoot = lv_obj_create(contentArea);
    lv_obj_set_size(context->contentRoot, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(context->contentRoot, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(context->contentRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(context->contentRoot, 20, 0);
    lv_obj_set_style_pad_right(context->contentRoot, 20, 0);
    lv_obj_set_style_pad_bottom(context->contentRoot, 20, 0);
    lv_obj_set_style_pad_left(context->contentRoot, IconRail::RAIL_WIDTH + 20, 0);
    lv_obj_set_style_border_width(context->contentRoot, 0, 0);
    lv_obj_clear_flag(context->contentRoot, LV_OBJ_FLAG_SCROLLABLE);

    context->panel = std::make_unique<NetworkDiagnosticsPanel>(
        context->contentRoot, sm.getUserSettingsManager());

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->hide();
        panel->clearContent();
        panel->resetWidth();
    }

    return context;
}

void cleanupNetworkUiContext(StateMachine& sm, std::shared_ptr<NetworkUiContext>& context)
{
    if (!context || context.use_count() > 1) {
        return;
    }

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->setAllowMinimize(true);
            iconRail->setVisible(true);
        }
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
            panel->resetWidth();
        }
    }

    context->panel.reset();
    if (context->contentRoot) {
        lv_obj_del(context->contentRoot);
        context->contentRoot = nullptr;
    }
    context.reset();
}

void configureNetworkRail(
    StateMachine& sm, const bool visible, const std::optional<IconId> selectedIcon)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");

    iconRail->setVisible(visible);
    if (!visible) {
        return;
    }

    iconRail->setAllowMinimize(false);
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setVisibleIcons({ IconId::DUCK, IconId::NETWORK, IconId::SETTINGS, IconId::SCANNER });
    iconRail->showIcons();
    if (selectedIcon.has_value()) {
        iconRail->selectIcon(selectedIcon.value());
    }
}

Result<NetworkDiagnosticsPanel::AutomationState, std::string> getNetworkAutomationState(
    const std::shared_ptr<NetworkUiContext>& context)
{
    if (!context || !context->panel) {
        return Result<NetworkDiagnosticsPanel::AutomationState, std::string>::error(
            "Network panel unavailable");
    }

    return context->panel->getAutomationState();
}

Any transitionForScreen(
    const NetworkDiagnosticsPanel::AutomationScreen screen,
    std::shared_ptr<NetworkUiContext> context)
{
    switch (screen) {
        case NetworkDiagnosticsPanel::AutomationScreen::Wifi:
            return NetworkWifi{ std::move(context) };
        case NetworkDiagnosticsPanel::AutomationScreen::WifiDetails:
            return NetworkWifiDetails{ std::move(context) };
        case NetworkDiagnosticsPanel::AutomationScreen::WifiPassword:
            return NetworkWifiPassword{ std::move(context) };
        case NetworkDiagnosticsPanel::AutomationScreen::WifiConnecting:
            return NetworkWifiConnecting{ std::move(context) };
        case NetworkDiagnosticsPanel::AutomationScreen::LanAccess:
            return NetworkSettings{ std::move(context) };
        case NetworkDiagnosticsPanel::AutomationScreen::Scanner:
            return NetworkScanner{ std::move(context) };
    }

    DIRTSIM_ASSERT(false, "Unhandled NetworkDiagnosticsPanel::AutomationScreen");
    return NetworkWifi{ std::move(context) };
}

Result<std::monostate, std::string> sendDiagnosticsResponse(
    const UiApi::NetworkDiagnosticsGet::Cwc& cwc, const std::shared_ptr<NetworkUiContext>& context)
{
    const auto result = getNetworkAutomationState(context);
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkDiagnosticsGet::Response::error(ApiError(result.errorValue())));
        return Result<std::monostate, std::string>::error(result.errorValue());
    }

    cwc.sendResponse(
        UiApi::NetworkDiagnosticsGet::Response::okay(toAutomationOkay(result.value())));
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

std::optional<Any> syncNetworkStateToPanelScreen(
    const std::shared_ptr<NetworkUiContext>& context,
    const NetworkDiagnosticsPanel::AutomationScreen expectedScreen)
{
    const auto result = getNetworkAutomationState(context);
    if (result.isError() || result.value().screen == expectedScreen) {
        return std::nullopt;
    }

    return transitionForScreen(result.value().screen, context);
}

void respondOkay(const UiApi::SimStop::Cwc& cwc)
{
    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));
}

void respondOkay(const UiApi::StopButtonPress::Cwc& cwc)
{
    cwc.sendResponse(UiApi::StopButtonPress::Response::okay(std::monostate{}));
}

void restoreSelectedIcon(StateMachine& sm, const IconId iconId)
{
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    auto* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->selectIcon(iconId);
}

} // namespace

NetworkWifi::NetworkWifi(std::shared_ptr<NetworkUiContext> context) : context_(std::move(context))
{}

void NetworkWifi::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering NetworkWifi state");

    context_ = ensureNetworkUiContext(sm, std::move(context_));
    configureNetworkRail(sm, true, IconId::NETWORK);
    if (context_ && context_->panel) {
        context_->panel->showWifiView();
        context_->panel->refresh();
    }
}

void NetworkWifi::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting NetworkWifi state");
    cleanupNetworkUiContext(sm, context_);
}

Any NetworkWifi::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "NetworkWifi icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
            LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
            return NetworkScanner{ context_ };
        }
        return StartMenu{};
    }

    if (evt.selectedId == IconId::SETTINGS) {
        return NetworkSettings{ context_ };
    }
    if (evt.selectedId == IconId::SCANNER) {
        return NetworkScanner{ context_ };
    }
    if (evt.selectedId == IconId::NETWORK) {
        return std::move(*this);
    }
    if (evt.selectedId == IconId::NONE) {
        restoreSelectedIcon(sm, IconId::NETWORK);
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in NetworkWifi");
    return std::move(*this);
}

Any NetworkWifi::onEvent(const RailModeChangedEvent& evt, StateMachine& sm)
{
    if (evt.newMode == RailMode::Minimized) {
        configureNetworkRail(sm, true, IconId::NETWORK);
    }
    return std::move(*this);
}

Any NetworkWifi::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
        LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
        return NetworkScanner{ context_ };
    }
    return StartMenu{};
}

Any NetworkWifi::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    auto nextState =
        syncNetworkStateToPanelScreen(context_, NetworkDiagnosticsPanel::AutomationScreen::Wifi);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }
    return std::move(*this);
}

Any NetworkWifi::onEvent(const UiApi::NetworkConnectPress::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!context_ || !context_->panel) {
        cwc.sendResponse(
            UiApi::NetworkConnectPress::Response::error(ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = context_->panel->pressAutomationConnect(cwc.command.ssid);
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkConnectPress::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkConnectPress::Response::okay(
            UiApi::NetworkConnectPress::Okay{ .accepted = true }));

    auto nextState =
        syncNetworkStateToPanelScreen(context_, NetworkDiagnosticsPanel::AutomationScreen::Wifi);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }

    return std::move(*this);
}

Any NetworkWifi::onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    static_cast<void>(sendDiagnosticsResponse(cwc, context_));
    return std::move(*this);
}

Any NetworkWifi::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

Any NetworkWifi::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

NetworkWifiDetails::NetworkWifiDetails(std::shared_ptr<NetworkUiContext> context)
    : context_(std::move(context))
{}

void NetworkWifiDetails::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering NetworkWifiDetails state");
    context_ = ensureNetworkUiContext(sm, std::move(context_));
    configureNetworkRail(sm, true, IconId::NETWORK);
}

void NetworkWifiDetails::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting NetworkWifiDetails state");
    cleanupNetworkUiContext(sm, context_);
}

Any NetworkWifiDetails::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "NetworkWifiDetails icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
            LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
            return NetworkScanner{ context_ };
        }
        return StartMenu{};
    }
    if (evt.selectedId == IconId::SETTINGS) {
        return NetworkSettings{ context_ };
    }
    if (evt.selectedId == IconId::SCANNER) {
        return NetworkScanner{ context_ };
    }
    if (evt.selectedId == IconId::NETWORK || evt.selectedId == IconId::NONE) {
        if (evt.selectedId == IconId::NONE) {
            restoreSelectedIcon(sm, IconId::NETWORK);
        }
        if (context_ && context_->panel) {
            context_->panel->showWifiView();
        }
        return NetworkWifi{ context_ };
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in NetworkWifiDetails");
    return std::move(*this);
}

Any NetworkWifiDetails::onEvent(const RailModeChangedEvent& evt, StateMachine& sm)
{
    if (evt.newMode == RailMode::Minimized) {
        configureNetworkRail(sm, true, IconId::NETWORK);
    }
    return std::move(*this);
}

Any NetworkWifiDetails::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
        LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
        return NetworkScanner{ context_ };
    }
    return StartMenu{};
}

Any NetworkWifiDetails::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    auto nextState = syncNetworkStateToPanelScreen(
        context_, NetworkDiagnosticsPanel::AutomationScreen::WifiDetails);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }
    return std::move(*this);
}

Any NetworkWifiDetails::onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    static_cast<void>(sendDiagnosticsResponse(cwc, context_));
    return std::move(*this);
}

Any NetworkWifiDetails::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

Any NetworkWifiDetails::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

NetworkWifiPassword::NetworkWifiPassword(std::shared_ptr<NetworkUiContext> context)
    : context_(std::move(context))
{}

void NetworkWifiPassword::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering NetworkWifiPassword state");
    context_ = ensureNetworkUiContext(sm, std::move(context_));
    configureNetworkRail(sm, false, std::nullopt);
}

void NetworkWifiPassword::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting NetworkWifiPassword state");
    cleanupNetworkUiContext(sm, context_);
}

Any NetworkWifiPassword::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return StartMenu{};
}

Any NetworkWifiPassword::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    auto nextState = syncNetworkStateToPanelScreen(
        context_, NetworkDiagnosticsPanel::AutomationScreen::WifiPassword);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }
    return std::move(*this);
}

Any NetworkWifiPassword::onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    static_cast<void>(sendDiagnosticsResponse(cwc, context_));
    return std::move(*this);
}

Any NetworkWifiPassword::onEvent(const UiApi::NetworkPasswordSubmit::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!context_ || !context_->panel) {
        cwc.sendResponse(
            UiApi::NetworkPasswordSubmit::Response::error(ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = context_->panel->submitAutomationPassword(cwc.command.password);
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkPasswordSubmit::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkPasswordSubmit::Response::okay(
            UiApi::NetworkPasswordSubmit::Okay{ .accepted = true }));

    auto nextState = syncNetworkStateToPanelScreen(
        context_, NetworkDiagnosticsPanel::AutomationScreen::WifiPassword);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }

    return std::move(*this);
}

Any NetworkWifiPassword::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

Any NetworkWifiPassword::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

NetworkWifiConnecting::NetworkWifiConnecting(std::shared_ptr<NetworkUiContext> context)
    : context_(std::move(context))
{}

void NetworkWifiConnecting::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering NetworkWifiConnecting state");
    context_ = ensureNetworkUiContext(sm, std::move(context_));
    configureNetworkRail(sm, false, std::nullopt);
}

void NetworkWifiConnecting::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting NetworkWifiConnecting state");
    cleanupNetworkUiContext(sm, context_);
}

Any NetworkWifiConnecting::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return StartMenu{};
}

Any NetworkWifiConnecting::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    auto nextState = syncNetworkStateToPanelScreen(
        context_, NetworkDiagnosticsPanel::AutomationScreen::WifiConnecting);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }
    return std::move(*this);
}

Any NetworkWifiConnecting::onEvent(
    const UiApi::NetworkConnectCancelPress::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!context_ || !context_->panel) {
        cwc.sendResponse(
            UiApi::NetworkConnectCancelPress::Response::error(
                ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = context_->panel->pressAutomationConnectCancel();
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkConnectCancelPress::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkConnectCancelPress::Response::okay(
            UiApi::NetworkConnectCancelPress::Okay{ .accepted = true }));

    auto nextState = syncNetworkStateToPanelScreen(
        context_, NetworkDiagnosticsPanel::AutomationScreen::WifiConnecting);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }

    return std::move(*this);
}

Any NetworkWifiConnecting::onEvent(
    const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    static_cast<void>(sendDiagnosticsResponse(cwc, context_));
    return std::move(*this);
}

Any NetworkWifiConnecting::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

Any NetworkWifiConnecting::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

NetworkSettings::NetworkSettings(std::shared_ptr<NetworkUiContext> context)
    : context_(std::move(context))
{}

void NetworkSettings::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering NetworkSettings state");
    context_ = ensureNetworkUiContext(sm, std::move(context_));
    configureNetworkRail(sm, true, IconId::SETTINGS);
    if (context_ && context_->panel) {
        context_->panel->showLanAccessView();
        context_->panel->refresh();
    }
}

void NetworkSettings::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting NetworkSettings state");
    cleanupNetworkUiContext(sm, context_);
}

Any NetworkSettings::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "NetworkSettings icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
            LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
            return NetworkScanner{ context_ };
        }
        return StartMenu{};
    }
    if (evt.selectedId == IconId::NETWORK) {
        return NetworkWifi{ context_ };
    }
    if (evt.selectedId == IconId::SCANNER) {
        return NetworkScanner{ context_ };
    }
    if (evt.selectedId == IconId::SETTINGS) {
        return std::move(*this);
    }
    if (evt.selectedId == IconId::NONE) {
        restoreSelectedIcon(sm, IconId::SETTINGS);
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in NetworkSettings");
    return std::move(*this);
}

Any NetworkSettings::onEvent(const RailModeChangedEvent& evt, StateMachine& sm)
{
    if (evt.newMode == RailMode::Minimized) {
        configureNetworkRail(sm, true, IconId::SETTINGS);
    }
    return std::move(*this);
}

Any NetworkSettings::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
        LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
        return NetworkScanner{ context_ };
    }
    return StartMenu{};
}

Any NetworkSettings::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    auto nextState = syncNetworkStateToPanelScreen(
        context_, NetworkDiagnosticsPanel::AutomationScreen::LanAccess);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }
    return std::move(*this);
}

Any NetworkSettings::onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    static_cast<void>(sendDiagnosticsResponse(cwc, context_));
    return std::move(*this);
}

Any NetworkSettings::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

Any NetworkSettings::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

NetworkScanner::NetworkScanner(std::shared_ptr<NetworkUiContext> context)
    : context_(std::move(context))
{}

void NetworkScanner::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering NetworkScanner state");
    context_ = ensureNetworkUiContext(sm, std::move(context_));
    configureNetworkRail(sm, true, IconId::SCANNER);
    if (context_ && context_->panel) {
        context_->panel->showScannerView();
        context_->panel->refresh();
    }
}

void NetworkScanner::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting NetworkScanner state");
    cleanupNetworkUiContext(sm, context_);
}

Any NetworkScanner::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "NetworkScanner icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    if (evt.selectedId == IconId::DUCK) {
        if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
            LOG_INFO(State, "Blocking exit while scanner mode is active or busy");
            restoreSelectedIcon(sm, IconId::SCANNER);
            return std::move(*this);
        }
        return StartMenu{};
    }

    if (evt.selectedId == IconId::NETWORK) {
        if (context_ && context_->panel && context_->panel->isScannerModeBusy()
            && !context_->panel->isScannerModeActive()) {
            LOG_INFO(State, "Blocking exit while scanner mode is entering");
            restoreSelectedIcon(sm, IconId::SCANNER);
            return std::move(*this);
        }
        if (context_ && context_->panel) {
            static_cast<void>(context_->panel->requestScannerExit());
        }
        return NetworkWifi{ context_ };
    }

    if (evt.selectedId == IconId::SETTINGS) {
        if (context_ && context_->panel && context_->panel->isScannerModeBusy()
            && !context_->panel->isScannerModeActive()) {
            LOG_INFO(State, "Blocking exit while scanner mode is entering");
            restoreSelectedIcon(sm, IconId::SCANNER);
            return std::move(*this);
        }
        if (context_ && context_->panel) {
            static_cast<void>(context_->panel->requestScannerExit());
        }
        return NetworkSettings{ context_ };
    }

    if (evt.selectedId == IconId::SCANNER) {
        return std::move(*this);
    }
    if (evt.selectedId == IconId::NONE) {
        restoreSelectedIcon(sm, IconId::SCANNER);
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in NetworkScanner");
    return std::move(*this);
}

Any NetworkScanner::onEvent(const RailModeChangedEvent& evt, StateMachine& sm)
{
    if (evt.newMode == RailMode::Minimized) {
        configureNetworkRail(sm, true, IconId::SCANNER);
    }
    return std::move(*this);
}

Any NetworkScanner::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& sm)
{
    if (context_ && context_->panel && context_->panel->isScannerModeActiveOrBusy()) {
        restoreSelectedIcon(sm, IconId::SCANNER);
        return std::move(*this);
    }
    return StartMenu{};
}

Any NetworkScanner::onEvent(const UiUpdateEvent& /*evt*/, StateMachine& /*sm*/)
{
    auto nextState =
        syncNetworkStateToPanelScreen(context_, NetworkDiagnosticsPanel::AutomationScreen::Scanner);
    if (nextState.has_value()) {
        return std::move(nextState.value());
    }
    return std::move(*this);
}

Any NetworkScanner::onEvent(const UiApi::NetworkDiagnosticsGet::Cwc& cwc, StateMachine& /*sm*/)
{
    static_cast<void>(sendDiagnosticsResponse(cwc, context_));
    return std::move(*this);
}

Any NetworkScanner::onEvent(const UiApi::NetworkScannerEnterPress::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!context_ || !context_->panel) {
        cwc.sendResponse(
            UiApi::NetworkScannerEnterPress::Response::error(
                ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = context_->panel->pressAutomationScannerEnter();
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkScannerEnterPress::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkScannerEnterPress::Response::okay(
            UiApi::NetworkScannerEnterPress::Okay{ .accepted = true }));
    return std::move(*this);
}

Any NetworkScanner::onEvent(const UiApi::NetworkScannerExitPress::Cwc& cwc, StateMachine& /*sm*/)
{
    if (!context_ || !context_->panel) {
        cwc.sendResponse(
            UiApi::NetworkScannerExitPress::Response::error(ApiError("Network panel unavailable")));
        return std::move(*this);
    }

    const auto result = context_->panel->pressAutomationScannerExit();
    if (result.isError()) {
        cwc.sendResponse(
            UiApi::NetworkScannerExitPress::Response::error(ApiError(result.errorValue())));
        return std::move(*this);
    }

    cwc.sendResponse(
        UiApi::NetworkScannerExitPress::Response::okay(
            UiApi::NetworkScannerExitPress::Okay{ .accepted = true }));
    return std::move(*this);
}

Any NetworkScanner::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

Any NetworkScanner::onEvent(const UiApi::StopButtonPress::Cwc& cwc, StateMachine& sm)
{
    respondOkay(cwc);
    return onEvent(StopButtonClickedEvent{}, sm);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
