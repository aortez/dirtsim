#include "NetworkDiagnosticsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "os-manager/api/NetworkSnapshotGet.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include "os-manager/api/WifiConnect.h"
#include "os-manager/api/WifiForget.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <exception>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>

namespace DirtSim {
namespace Ui {

namespace {
struct NetworkAccessCache {
    bool webUiEnabled = false;
    bool webSocketEnabled = false;
    std::string webSocketToken;
};

std::mutex accessCacheMutex;
NetworkAccessCache accessCache;

NetworkAccessCache getAccessCache()
{
    std::lock_guard<std::mutex> lock(accessCacheMutex);
    return accessCache;
}

void updateAccessCache(const NetworkAccessCache& status)
{
    std::lock_guard<std::mutex> lock(accessCacheMutex);
    accessCache = status;
}

NetworkInterfaceInfo toUiLocalAddressInfo(const OsApi::NetworkSnapshotGet::LocalAddressInfo& info)
{
    return NetworkInterfaceInfo{ .name = info.name, .address = info.address };
}

Network::WifiStatus toUiWifiStatus(const OsApi::NetworkSnapshotGet::WifiStatusInfo& info)
{
    return Network::WifiStatus{ .connected = info.connected, .ssid = info.ssid };
}

Network::WifiNetworkStatus toUiWifiNetworkStatus(
    const OsApi::NetworkSnapshotGet::WifiNetworkStatus status)
{
    switch (status) {
        case OsApi::NetworkSnapshotGet::WifiNetworkStatus::Connected:
            return Network::WifiNetworkStatus::Connected;
        case OsApi::NetworkSnapshotGet::WifiNetworkStatus::Saved:
            return Network::WifiNetworkStatus::Saved;
        case OsApi::NetworkSnapshotGet::WifiNetworkStatus::Open:
            return Network::WifiNetworkStatus::Open;
        case OsApi::NetworkSnapshotGet::WifiNetworkStatus::Available:
            return Network::WifiNetworkStatus::Available;
    }

    return Network::WifiNetworkStatus::Saved;
}

Network::WifiNetworkInfo toUiWifiNetworkInfo(const OsApi::NetworkSnapshotGet::WifiNetworkInfo& info)
{
    return Network::WifiNetworkInfo{
        .ssid = info.ssid,
        .status = toUiWifiNetworkStatus(info.status),
        .signalDbm = info.signalDbm,
        .security = info.security,
        .autoConnect = info.autoConnect,
        .hasCredentials = info.hasCredentials,
        .lastUsedDate = info.lastUsedDate,
        .lastUsedRelative = info.lastUsedRelative,
        .connectionId = info.connectionId,
    };
}

Network::WifiConnectResult toUiWifiConnectResult(const OsApi::WifiConnect::Okay& result)
{
    return Network::WifiConnectResult{ .success = result.success, .ssid = result.ssid };
}

Network::WifiForgetResult toUiWifiForgetResult(const OsApi::WifiForget::Okay& result)
{
    return Network::WifiForgetResult{
        .success = result.success,
        .ssid = result.ssid,
        .removed = result.removed,
    };
}

constexpr uint32_t ACCESSORY_BG_COLOR = 0x1A1A1A;
constexpr uint32_t ACCESSORY_BORDER_COLOR = 0x404040;
constexpr uint32_t CARD_BG_COLOR = 0x101010;
constexpr uint32_t CARD_BORDER_COLOR = 0x303030;
constexpr uint32_t HEADER_TEXT_COLOR = 0xAAAAAA;
constexpr uint32_t ERROR_TEXT_COLOR = 0xFF7777;
constexpr uint32_t MUTED_TEXT_COLOR = 0xB0B0B0;
constexpr uint32_t NETWORK_ROW_BG_COLOR = 0x1C1C1C;
constexpr uint32_t NETWORK_ROW_BORDER_COLOR = 0x383838;
lv_obj_t* getActionButtonInnerButton(lv_obj_t* container)
{
    if (!container) {
        return nullptr;
    }

    return lv_obj_get_child(container, 0);
}

lv_obj_t* getActionButtonLabel(lv_obj_t* container)
{
    lv_obj_t* button = getActionButtonInnerButton(container);
    if (!button) {
        return nullptr;
    }

    const uint32_t childCount = lv_obj_get_child_cnt(button);
    if (childCount == 0) {
        return nullptr;
    }

    return lv_obj_get_child(button, childCount - 1);
}

bool isOpenSecurity(const std::string& security)
{
    return security.empty() || security == "open";
}

void setActionButtonEnabled(lv_obj_t* container, bool enabled)
{
    lv_obj_t* button = getActionButtonInnerButton(container);
    if (!button) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(button, LV_STATE_DISABLED);
        return;
    }

    lv_obj_add_state(button, LV_STATE_DISABLED);
}

void setActionButtonText(lv_obj_t* container, const std::string& text)
{
    lv_obj_t* label = getActionButtonLabel(container);
    if (!label) {
        return;
    }

    lv_label_set_text(label, text.c_str());
}

lv_obj_t* createSectionCard(lv_obj_t* parent, const char* title)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(CARD_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(CARD_BORDER_COLOR), 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(HEADER_TEXT_COLOR), 0);
    lv_obj_set_width(titleLabel, LV_PCT(100));

    return card;
}

void stylePanelColumn(lv_obj_t* column)
{
    lv_obj_set_size(column, 0, LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(column, 0, 0);
    lv_obj_set_style_pad_row(column, 14, 0);
    lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(column, 0, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);
}

void styleSwitchRow(lv_obj_t* switchObj)
{
    if (!switchObj) {
        return;
    }

    lv_obj_t* container = lv_obj_get_parent(switchObj);
    if (!container) {
        return;
    }

    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 12, 0);
    lv_obj_set_style_pad_column(container, 12, 0);
    lv_obj_set_style_bg_color(container, lv_color_hex(ACCESSORY_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_obj_get_child(container, 1);
    if (label) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
}

std::string joinTextParts(const std::vector<std::string>& parts, const char* separator)
{
    std::string text;
    for (const auto& part : parts) {
        if (part.empty()) {
            continue;
        }

        if (!text.empty()) {
            text += separator;
        }
        text += part;
    }

    return text;
}
} // namespace

NetworkDiagnosticsPanel::NetworkDiagnosticsPanel(lv_obj_t* container)
    : container_(container), asyncState_(std::make_shared<AsyncState>())
{
    createUI();
    LOG_INFO(Controls, "NetworkDiagnosticsPanel created");
}

NetworkDiagnosticsPanel::~NetworkDiagnosticsPanel()
{
    closePasswordPrompt();

    if (refreshTimer_) {
        lv_timer_delete(refreshTimer_);
        refreshTimer_ = nullptr;
    }
    LOG_INFO(Controls, "NetworkDiagnosticsPanel destroyed");
}

void NetworkDiagnosticsPanel::createUI()
{
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(container_, 16, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    pagesContainer_ = lv_obj_create(container_);
    lv_obj_set_size(pagesContainer_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(pagesContainer_, 1);
    lv_obj_set_flex_flow(pagesContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        pagesContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(pagesContainer_, 0, 0);
    lv_obj_set_style_bg_opa(pagesContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pagesContainer_, 0, 0);
    lv_obj_clear_flag(pagesContainer_, LV_OBJ_FLAG_SCROLLABLE);

    wifiView_ = lv_obj_create(pagesContainer_);
    stylePanelColumn(wifiView_);
    lv_obj_set_size(wifiView_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(wifiView_, 1);

    lanAccessView_ = lv_obj_create(pagesContainer_);
    stylePanelColumn(lanAccessView_);
    lv_obj_set_size(lanAccessView_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(lanAccessView_, 1);

    lv_obj_t* wifiCard = lv_obj_create(wifiView_);
    lv_obj_set_size(wifiCard, LV_PCT(100), 0);
    lv_obj_set_flex_grow(wifiCard, 1);
    lv_obj_set_flex_flow(wifiCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wifiCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(wifiCard, 12, 0);
    lv_obj_set_style_pad_row(wifiCard, 10, 0);
    lv_obj_set_style_bg_color(wifiCard, lv_color_hex(CARD_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(wifiCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifiCard, 1, 0);
    lv_obj_set_style_border_color(wifiCard, lv_color_hex(CARD_BORDER_COLOR), 0);
    lv_obj_set_style_radius(wifiCard, 10, 0);
    lv_obj_clear_flag(wifiCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* wifiHeaderRow = lv_obj_create(wifiCard);
    lv_obj_set_size(wifiHeaderRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifiHeaderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        wifiHeaderRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(wifiHeaderRow, 0, 0);
    lv_obj_set_style_pad_column(wifiHeaderRow, 12, 0);
    lv_obj_set_style_bg_opa(wifiHeaderRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifiHeaderRow, 0, 0);
    lv_obj_clear_flag(wifiHeaderRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* wifiTitleLabel = lv_label_create(wifiHeaderRow);
    lv_label_set_text(wifiTitleLabel, "Wi-Fi");
    lv_obj_set_style_text_font(wifiTitleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifiTitleLabel, lv_color_hex(HEADER_TEXT_COLOR), 0);

    refreshButton_ = LVGLBuilder::actionButton(wifiHeaderRow)
                         .text("Refresh")
                         .icon(LV_SYMBOL_REFRESH)
                         .mode(LVGLBuilder::ActionMode::Push)
                         .layoutRow()
                         .width(188)
                         .height(48)
                         .callback(onRefreshClicked, this)
                         .buildOrLog();

    wifiStatusLabel_ = lv_label_create(wifiCard);
    lv_obj_set_style_text_font(wifiStatusLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(MUTED_TEXT_COLOR), 0);
    lv_obj_set_width(wifiStatusLabel_, LV_PCT(100));
    lv_label_set_long_mode(wifiStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(wifiStatusLabel_, LV_OBJ_FLAG_HIDDEN);

    currentNetworkContainer_ = lv_obj_create(wifiCard);
    lv_obj_set_size(currentNetworkContainer_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(currentNetworkContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        currentNetworkContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(currentNetworkContainer_, 12, 0);
    lv_obj_set_style_pad_row(currentNetworkContainer_, 8, 0);
    lv_obj_set_style_bg_color(currentNetworkContainer_, lv_color_hex(ACCESSORY_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(currentNetworkContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(currentNetworkContainer_, 1, 0);
    lv_obj_set_style_border_color(
        currentNetworkContainer_, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
    lv_obj_set_style_radius(currentNetworkContainer_, 10, 0);
    lv_obj_clear_flag(currentNetworkContainer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(currentNetworkContainer_, LV_OBJ_FLAG_HIDDEN);

    networksTitleLabel_ = lv_label_create(wifiCard);
    lv_label_set_text(networksTitleLabel_, "Networks");
    lv_obj_set_style_text_font(networksTitleLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(networksTitleLabel_, lv_color_hex(HEADER_TEXT_COLOR), 0);
    lv_obj_set_width(networksTitleLabel_, LV_PCT(100));

    networksContainer_ = lv_obj_create(wifiCard);
    lv_obj_set_size(networksContainer_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(networksContainer_, 1);
    lv_obj_set_flex_flow(networksContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        networksContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(networksContainer_, 0, 0);
    lv_obj_set_style_pad_row(networksContainer_, 10, 0);
    lv_obj_set_style_bg_opa(networksContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(networksContainer_, 0, 0);
    lv_obj_set_scroll_dir(networksContainer_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(networksContainer_, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* accessCard = createSectionCard(lanAccessView_, "LAN Access");

    lv_obj_t* accessHint = lv_label_create(accessCard);
    lv_label_set_text(
        accessHint, "Control which services are visible to devices on your local network.");
    lv_obj_set_style_text_font(accessHint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(accessHint, lv_color_hex(MUTED_TEXT_COLOR), 0);
    lv_label_set_long_mode(accessHint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(accessHint, LV_PCT(100));

    webUiToggle_ = LVGLBuilder::labeledSwitch(accessCard)
                       .label("LAN Web UI")
                       .initialState(false)
                       .width(LV_PCT(100))
                       .height(72)
                       .callback(onWebUiToggleChanged, this)
                       .buildOrLog();
    styleSwitchRow(webUiToggle_);

    webSocketToggle_ = LVGLBuilder::labeledSwitch(accessCard)
                           .label("Incoming WebSocket Traffic")
                           .initialState(false)
                           .width(LV_PCT(100))
                           .height(72)
                           .callback(onWebSocketToggleChanged, this)
                           .buildOrLog();
    styleSwitchRow(webSocketToggle_);

    lv_obj_t* tokenCard = lv_obj_create(accessCard);
    lv_obj_set_size(tokenCard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tokenCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tokenCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(tokenCard, 12, 0);
    lv_obj_set_style_pad_row(tokenCard, 6, 0);
    lv_obj_set_style_bg_color(tokenCard, lv_color_hex(ACCESSORY_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(tokenCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tokenCard, 1, 0);
    lv_obj_set_style_border_color(tokenCard, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
    lv_obj_set_style_radius(tokenCard, 10, 0);
    lv_obj_clear_flag(tokenCard, LV_OBJ_FLAG_SCROLLABLE);

    webSocketTokenTitleLabel_ = lv_label_create(tokenCard);
    lv_obj_set_style_text_font(webSocketTokenTitleLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(webSocketTokenTitleLabel_, lv_color_hex(HEADER_TEXT_COLOR), 0);
    lv_obj_set_width(webSocketTokenTitleLabel_, LV_PCT(100));
    lv_label_set_long_mode(webSocketTokenTitleLabel_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(webSocketTokenTitleLabel_, "WebSocket token");

    webSocketTokenLabel_ = lv_label_create(tokenCard);
    lv_obj_set_style_text_font(webSocketTokenLabel_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(webSocketTokenLabel_, lv_color_hex(0x00CED1), 0);
    lv_obj_set_width(webSocketTokenLabel_, LV_PCT(100));
    lv_label_set_long_mode(webSocketTokenLabel_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(webSocketTokenLabel_, "--");

    refreshTimer_ = lv_timer_create(onRefreshTimer, 100, this);
    if (refreshTimer_) {
        lv_timer_pause(refreshTimer_);
    }

    const auto cachedAccess = getAccessCache();
    if (cachedAccess.webUiEnabled || cachedAccess.webSocketEnabled) {
        NetworkAccessStatus cachedStatus;
        cachedStatus.webUiEnabled = cachedAccess.webUiEnabled;
        cachedStatus.webSocketEnabled = cachedAccess.webSocketEnabled;
        cachedStatus.webSocketToken = cachedAccess.webSocketToken;
        const auto statusResult =
            Result<NetworkAccessStatus, std::string>::okay(std::move(cachedStatus));
        updateWebUiStatus(statusResult);
        updateWebSocketStatus(statusResult);
    }

    setViewMode(viewMode_);

    // Initial display update.
    refresh();
}

void NetworkDiagnosticsPanel::showLanAccessView()
{
    setViewMode(ViewMode::LanAccess);
}

void NetworkDiagnosticsPanel::showWifiView()
{
    setViewMode(ViewMode::Wifi);
}

void NetworkDiagnosticsPanel::setViewMode(ViewMode mode)
{
    auto setVisibility = [](lv_obj_t* view, bool visible) {
        if (!view) {
            return;
        }

        if (visible) {
            lv_obj_clear_flag(view, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(view, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
        else {
            lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(view, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    };

    setVisibility(wifiView_, mode == ViewMode::Wifi);
    setVisibility(lanAccessView_, mode == ViewMode::LanAccess);
    viewMode_ = mode;
}

void NetworkDiagnosticsPanel::closePasswordPrompt()
{
    if (passwordOverlay_) {
        lv_obj_del(passwordOverlay_);
        passwordOverlay_ = nullptr;
    }

    passwordCancelButton_ = nullptr;
    passwordErrorLabel_ = nullptr;
    passwordJoinButton_ = nullptr;
    passwordKeyboard_ = nullptr;
    passwordTextArea_ = nullptr;
    passwordVisibilityButton_ = nullptr;
    passwordPromptNetwork_.reset();
    passwordVisible_ = false;
}

bool NetworkDiagnosticsPanel::networkRequiresPassword(const Network::WifiNetworkInfo& network) const
{
    if (network.status == Network::WifiNetworkStatus::Connected) {
        return false;
    }

    if (isOpenSecurity(network.security)) {
        return false;
    }

    return !network.hasCredentials;
}

void NetworkDiagnosticsPanel::openPasswordPrompt(const Network::WifiNetworkInfo& network)
{
    closePasswordPrompt();

    passwordPromptNetwork_ = network;
    passwordVisible_ = false;

    passwordOverlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(passwordOverlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(passwordOverlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(passwordOverlay_, LV_OPA_60, 0);
    lv_obj_set_style_border_width(passwordOverlay_, 0, 0);
    lv_obj_set_style_pad_all(passwordOverlay_, 0, 0);
    lv_obj_set_style_radius(passwordOverlay_, 0, 0);
    lv_obj_clear_flag(passwordOverlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(passwordOverlay_);

    lv_obj_t* modal = lv_obj_create(passwordOverlay_);
    lv_obj_set_size(modal, LV_PCT(90), LV_PCT(100));
    lv_obj_align(modal, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_border_width(modal, 1, 0);
    lv_obj_set_style_border_color(modal, lv_color_hex(CARD_BORDER_COLOR), 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 10, 0);
    lv_obj_set_style_pad_row(modal, 8, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* headerRow = lv_obj_create(modal);
    lv_obj_set_size(headerRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(headerRow, 0, 0);
    lv_obj_set_style_pad_column(headerRow, 10, 0);
    lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(headerRow, 0, 0);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        headerRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_SCROLLABLE);

    auto configureHeaderCell = [](lv_obj_t* cell) {
        lv_obj_set_size(cell, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(cell, 1);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    };

    lv_obj_t* titleCell = lv_obj_create(headerRow);
    configureHeaderCell(titleCell);

    lv_obj_t* title = lv_label_create(titleCell);
    const std::string titleText = "Join Wi-Fi";
    lv_label_set_text(title, titleText.c_str());
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    lv_obj_t* ssidCell = lv_obj_create(headerRow);
    configureHeaderCell(ssidCell);

    lv_obj_t* ssidLabel = lv_label_create(ssidCell);
    const std::string ssidText = "SSID: " + network.ssid;
    lv_label_set_text(ssidLabel, ssidText.c_str());
    lv_obj_set_width(ssidLabel, LV_PCT(100));
    lv_label_set_long_mode(ssidLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ssidLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ssidLabel, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_18, 0);

    lv_obj_t* securityCell = lv_obj_create(headerRow);
    configureHeaderCell(securityCell);

    lv_obj_t* securityLabel = lv_label_create(securityCell);
    const std::string securityText =
        "Security: " + (network.security.empty() ? std::string("unknown") : network.security);
    lv_label_set_text(securityLabel, securityText.c_str());
    lv_obj_set_width(securityLabel, LV_PCT(100));
    lv_obj_set_style_text_align(securityLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(securityLabel, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(securityLabel, &lv_font_montserrat_12, 0);

    lv_obj_t* passwordRow = lv_obj_create(modal);
    lv_obj_set_size(passwordRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(passwordRow, 6, 0);
    lv_obj_set_style_pad_column(passwordRow, 6, 0);
    lv_obj_set_style_bg_color(passwordRow, lv_color_hex(ACCESSORY_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(passwordRow, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(passwordRow, 1, 0);
    lv_obj_set_style_border_color(passwordRow, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
    lv_obj_set_style_radius(passwordRow, 10, 0);
    lv_obj_set_flex_flow(passwordRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        passwordRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(passwordRow, LV_OBJ_FLAG_SCROLLABLE);

    passwordTextArea_ = lv_textarea_create(passwordRow);
    lv_obj_set_size(passwordTextArea_, 0, 48);
    lv_obj_set_flex_grow(passwordTextArea_, 1);
    lv_textarea_set_one_line(passwordTextArea_, true);
    lv_textarea_set_max_length(passwordTextArea_, 64);
    lv_textarea_set_password_mode(passwordTextArea_, true);
    lv_textarea_set_password_show_time(passwordTextArea_, 0);
    lv_textarea_set_placeholder_text(passwordTextArea_, "Password");
    lv_obj_set_style_bg_opa(passwordTextArea_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passwordTextArea_, 0, 0);
    lv_obj_set_style_outline_width(passwordTextArea_, 0, 0);
    lv_obj_set_style_pad_hor(passwordTextArea_, 6, 0);
    lv_obj_set_style_pad_ver(passwordTextArea_, 8, 0);
    lv_obj_set_style_text_color(passwordTextArea_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(passwordTextArea_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(
        passwordTextArea_, lv_color_hex(MUTED_TEXT_COLOR), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(passwordTextArea_, lv_color_hex(0x00CED1), LV_PART_CURSOR);
    lv_obj_add_event_cb(passwordTextArea_, onPasswordTextAreaEvent, LV_EVENT_ALL, this);

    passwordVisibilityButton_ = LVGLBuilder::actionButton(passwordRow)
                                    .text("Show")
                                    .mode(LVGLBuilder::ActionMode::Push)
                                    .width(88)
                                    .height(48)
                                    .callback(onPasswordVisibilityClicked, this)
                                    .buildOrLog();

    passwordJoinButton_ = LVGLBuilder::actionButton(passwordRow)
                              .text("Join")
                              .mode(LVGLBuilder::ActionMode::Push)
                              .width(92)
                              .height(48)
                              .callback(onPasswordJoinClicked, this)
                              .buildOrLog();

    passwordCancelButton_ = LVGLBuilder::actionButton(passwordRow)
                                .text("Cancel")
                                .mode(LVGLBuilder::ActionMode::Push)
                                .width(92)
                                .height(48)
                                .callback(onPasswordCancelClicked, this)
                                .buildOrLog();

    passwordErrorLabel_ = lv_label_create(modal);
    lv_obj_set_width(passwordErrorLabel_, LV_PCT(100));
    lv_label_set_long_mode(passwordErrorLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(passwordErrorLabel_, lv_color_hex(ERROR_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(passwordErrorLabel_, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(passwordErrorLabel_, LV_OBJ_FLAG_HIDDEN);

    passwordKeyboard_ = lv_keyboard_create(modal);
    lv_obj_set_size(passwordKeyboard_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(passwordKeyboard_, 1);
    lv_obj_set_style_bg_color(passwordKeyboard_, lv_color_hex(CARD_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(passwordKeyboard_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(passwordKeyboard_, 0, 0);
    lv_obj_set_style_radius(passwordKeyboard_, 10, 0);
    lv_obj_set_style_bg_color(passwordKeyboard_, lv_color_hex(NETWORK_ROW_BG_COLOR), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(passwordKeyboard_, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(passwordKeyboard_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(
        passwordKeyboard_, lv_color_hex(ACCESSORY_BORDER_COLOR), LV_PART_ITEMS);
    lv_obj_set_style_text_color(passwordKeyboard_, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_text_font(passwordKeyboard_, &lv_font_montserrat_18, LV_PART_ITEMS);
    lv_obj_add_event_cb(passwordKeyboard_, onPasswordKeyboardEvent, LV_EVENT_ALL, this);
    lv_keyboard_set_textarea(passwordKeyboard_, passwordTextArea_);

    updatePasswordVisibilityButton();
    updatePasswordJoinButton();
    setPasswordPromptError("");
}

void NetworkDiagnosticsPanel::refresh()
{
    setLoadingState();
    startAsyncRefresh();
}

void NetworkDiagnosticsPanel::setWifiStatusMessage(const std::string& text, uint32_t color)
{
    if (!wifiStatusLabel_) {
        return;
    }

    if (text.empty()) {
        lv_label_set_text(wifiStatusLabel_, "");
        lv_obj_add_flag(wifiStatusLabel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(wifiStatusLabel_, text.c_str());
    lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(color), 0);
    lv_obj_clear_flag(wifiStatusLabel_, LV_OBJ_FLAG_HIDDEN);
}

void NetworkDiagnosticsPanel::updateCurrentConnectionSummary()
{
    if (!currentNetworkContainer_ || !networksTitleLabel_) {
        return;
    }

    lv_obj_clean(currentNetworkContainer_);

    size_t connectedIndex = 0;
    const Network::WifiNetworkInfo* connectedNetwork = nullptr;
    for (size_t i = 0; i < networks_.size(); ++i) {
        if (networks_[i].status == Network::WifiNetworkStatus::Connected) {
            connectedNetwork = &networks_[i];
            connectedIndex = i;
            break;
        }
    }

    if (!connectedNetwork) {
        lv_label_set_text(networksTitleLabel_, "Networks");
        lv_obj_add_flag(currentNetworkContainer_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(networksTitleLabel_, "Available networks");
    lv_obj_clear_flag(currentNetworkContainer_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* summaryHeader = lv_obj_create(currentNetworkContainer_);
    lv_obj_set_size(summaryHeader, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(summaryHeader, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        summaryHeader, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(summaryHeader, 0, 0);
    lv_obj_set_style_pad_column(summaryHeader, 10, 0);
    lv_obj_set_style_bg_opa(summaryHeader, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summaryHeader, 0, 0);
    lv_obj_clear_flag(summaryHeader, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(summaryHeader);
    const std::string titleText = "Connected to " + connectedNetwork->ssid;
    lv_label_set_text(titleLabel, titleText.c_str());
    lv_obj_set_width(titleLabel, 0);
    lv_obj_set_flex_grow(titleLabel, 1);
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x00FF7F), 0);

    const bool isForgetting =
        actionState_.kind == AsyncActionKind::Forget && connectedNetwork->ssid == actionState_.ssid;
    const bool actionsDisabled = isActionInProgress();
    if (!connectedNetwork->connectionId.empty()) {
        auto forgetContext = std::make_unique<ForgetContext>();
        forgetContext->panel = this;
        forgetContext->index = connectedIndex;
        forgetContexts_.push_back(std::move(forgetContext));
        ForgetContext* forgetContextPtr = forgetContexts_.back().get();

        lv_obj_t* forgetButton = LVGLBuilder::actionButton(summaryHeader)
                                     .text(isForgetting ? "Forgetting" : "Forget")
                                     .mode(LVGLBuilder::ActionMode::Push)
                                     .width(96)
                                     .height(40)
                                     .callback(onForgetClicked, forgetContextPtr)
                                     .buildOrLog();
        setActionButtonEnabled(forgetButton, !actionsDisabled);
    }

    lv_obj_t* detailsLabel = lv_label_create(currentNetworkContainer_);
    const std::string detailsText = formatCurrentConnectionDetails(*connectedNetwork);
    lv_label_set_text(detailsLabel, detailsText.c_str());
    lv_obj_set_width(detailsLabel, LV_PCT(100));
    lv_label_set_long_mode(detailsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(detailsLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detailsLabel, lv_color_hex(MUTED_TEXT_COLOR), 0);
}

void NetworkDiagnosticsPanel::setLoadingState()
{
    setWifiStatusMessage("", MUTED_TEXT_COLOR);

    if (networksContainer_) {
        lv_obj_clean(networksContainer_);
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, "Scanning nearby networks...");
        lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }

    setRefreshButtonEnabled(false);
}

void NetworkDiagnosticsPanel::setPasswordPromptBusy(bool busy)
{
    if (passwordTextArea_) {
        if (busy) {
            lv_obj_add_state(passwordTextArea_, LV_STATE_DISABLED);
        }
        else {
            lv_obj_clear_state(passwordTextArea_, LV_STATE_DISABLED);
        }
    }

    if (passwordKeyboard_) {
        if (busy) {
            lv_obj_add_state(passwordKeyboard_, LV_STATE_DISABLED);
        }
        else {
            lv_obj_clear_state(passwordKeyboard_, LV_STATE_DISABLED);
        }
    }

    setActionButtonEnabled(passwordCancelButton_, !busy);
    setActionButtonEnabled(passwordVisibilityButton_, !busy);
    if (busy) {
        setActionButtonEnabled(passwordJoinButton_, false);
    }
    else {
        updatePasswordJoinButton();
    }
}

void NetworkDiagnosticsPanel::setPasswordPromptError(const std::string& message)
{
    if (!passwordErrorLabel_) {
        return;
    }

    if (message.empty()) {
        lv_label_set_text(passwordErrorLabel_, "");
        lv_obj_add_flag(passwordErrorLabel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(passwordErrorLabel_, message.c_str());
    lv_obj_clear_flag(passwordErrorLabel_, LV_OBJ_FLAG_HIDDEN);
}

void NetworkDiagnosticsPanel::setRefreshButtonEnabled(bool enabled)
{
    if (!refreshButton_) {
        return;
    }

    lv_obj_t* button = lv_obj_get_child(refreshButton_, 0);
    if (!button) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(button, LV_STATE_DISABLED);
    }
    else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

void NetworkDiagnosticsPanel::setWebUiToggleEnabled(bool enabled)
{
    if (!webUiToggle_) {
        return;
    }

    lv_obj_t* container = lv_obj_get_parent(webUiToggle_);
    if (enabled) {
        lv_obj_clear_state(webUiToggle_, LV_STATE_DISABLED);
        if (container) {
            lv_obj_clear_state(container, LV_STATE_DISABLED);
        }
    }
    else {
        lv_obj_add_state(webUiToggle_, LV_STATE_DISABLED);
        if (container) {
            lv_obj_add_state(container, LV_STATE_DISABLED);
        }
    }
}

void NetworkDiagnosticsPanel::setWebSocketToggleEnabled(bool enabled)
{
    if (!webSocketToggle_) {
        return;
    }

    lv_obj_t* container = lv_obj_get_parent(webSocketToggle_);
    if (enabled) {
        lv_obj_clear_state(webSocketToggle_, LV_STATE_DISABLED);
        if (container) {
            lv_obj_clear_state(container, LV_STATE_DISABLED);
        }
    }
    else {
        lv_obj_add_state(webSocketToggle_, LV_STATE_DISABLED);
        if (container) {
            lv_obj_add_state(container, LV_STATE_DISABLED);
        }
    }
}

bool NetworkDiagnosticsPanel::startAsyncRefresh()
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->refreshInProgress) {
            return false;
        }
        asyncState_->refreshInProgress = true;
    }

    if (refreshTimer_) {
        lv_timer_resume(refreshTimer_);
    }

    auto state = asyncState_;
    std::thread([state]() {
        PendingRefreshData data;
        try {
            Network::WebSocketService client;
            const std::string address = "ws://localhost:9090";
            const auto connectResult = client.connect(address, 2000);
            if (connectResult.isError()) {
                const std::string errorMessage =
                    "Failed to connect to os-manager: " + connectResult.errorValue();
                data.statusResult = Result<Network::WifiStatus, std::string>::error(errorMessage);
                data.listResult =
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::error(errorMessage);
                data.accessStatusResult =
                    Result<NetworkAccessStatus, std::string>::error(errorMessage);
            }
            else {
                OsApi::NetworkSnapshotGet::Command snapshotCmd{};
                const auto snapshotResponse =
                    client.sendCommandAndGetResponse<OsApi::NetworkSnapshotGet::Okay>(
                        snapshotCmd, 4000);

                OsApi::SystemStatus::Command statusCmd{};
                const auto accessResponse =
                    client.sendCommandAndGetResponse<OsApi::SystemStatus::Okay>(statusCmd, 2000);
                client.disconnect();

                if (snapshotResponse.isError()) {
                    data.statusResult = Result<Network::WifiStatus, std::string>::error(
                        "NetworkSnapshotGet failed: " + snapshotResponse.errorValue());
                    data.listResult =
                        Result<std::vector<Network::WifiNetworkInfo>, std::string>::error(
                            "NetworkSnapshotGet failed: " + snapshotResponse.errorValue());
                }
                else {
                    const auto snapshotInner = snapshotResponse.value();
                    if (snapshotInner.isError()) {
                        data.statusResult = Result<Network::WifiStatus, std::string>::error(
                            "NetworkSnapshotGet failed: " + snapshotInner.errorValue().message);
                        data.listResult =
                            Result<std::vector<Network::WifiNetworkInfo>, std::string>::error(
                                "NetworkSnapshotGet failed: " + snapshotInner.errorValue().message);
                    }
                    else {
                        std::vector<Network::WifiNetworkInfo> networks;
                        networks.reserve(snapshotInner.value().networks.size());
                        for (const auto& network : snapshotInner.value().networks) {
                            networks.push_back(toUiWifiNetworkInfo(network));
                        }

                        std::vector<NetworkInterfaceInfo> localAddresses;
                        localAddresses.reserve(snapshotInner.value().localAddresses.size());
                        for (const auto& addressInfo : snapshotInner.value().localAddresses) {
                            localAddresses.push_back(toUiLocalAddressInfo(addressInfo));
                        }

                        data.statusResult = Result<Network::WifiStatus, std::string>::okay(
                            toUiWifiStatus(snapshotInner.value().status));
                        data.listResult =
                            Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(
                                std::move(networks));
                        data.localAddresses = std::move(localAddresses);
                    }
                }

                if (accessResponse.isError()) {
                    data.accessStatusResult = Result<NetworkAccessStatus, std::string>::error(
                        "SystemStatus failed: " + accessResponse.errorValue());
                }
                else {
                    const auto accessInner = accessResponse.value();
                    if (accessInner.isError()) {
                        data.accessStatusResult = Result<NetworkAccessStatus, std::string>::error(
                            "SystemStatus failed: " + accessInner.errorValue().message);
                    }
                    else {
                        NetworkAccessStatus status;
                        status.webUiEnabled = accessInner.value().lan_web_ui_enabled;
                        status.webSocketEnabled = accessInner.value().lan_websocket_enabled;
                        status.webSocketToken = accessInner.value().lan_websocket_token;
                        data.accessStatusResult =
                            Result<NetworkAccessStatus, std::string>::okay(std::move(status));
                    }
                }
            }

            if (!data.accessStatusResult.isError()) {
                NetworkAccessCache cache;
                cache.webUiEnabled = data.accessStatusResult.value().webUiEnabled;
                cache.webSocketEnabled = data.accessStatusResult.value().webSocketEnabled;
                cache.webSocketToken = data.accessStatusResult.value().webSocketToken;
                updateAccessCache(cache);
            }
        }
        catch (const std::exception& e) {
            LOG_WARN(Controls, "WiFi refresh exception: {}", e.what());
            data.statusResult = Result<Network::WifiStatus, std::string>::error(e.what());
            data.listResult =
                Result<std::vector<Network::WifiNetworkInfo>, std::string>::error(e.what());
            data.accessStatusResult = Result<NetworkAccessStatus, std::string>::error(e.what());
        }
        catch (...) {
            LOG_WARN(Controls, "WiFi refresh exception: unknown");
            data.statusResult =
                Result<Network::WifiStatus, std::string>::error("WiFi refresh failed");
            data.listResult = Result<std::vector<Network::WifiNetworkInfo>, std::string>::error(
                "WiFi refresh failed");
            data.accessStatusResult =
                Result<NetworkAccessStatus, std::string>::error("WiFi refresh failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingRefresh = data;
        state->refreshInProgress = false;
    }).detach();

    return true;
}

void NetworkDiagnosticsPanel::startAsyncConnect(
    const Network::WifiNetworkInfo& network, const std::optional<std::string>& password)
{
    Network::WifiNetworkInfo networkCopy = network;
    if (!beginAsyncAction(AsyncActionKind::Connect, networkCopy, "Connecting to")) {
        return;
    }

    auto state = asyncState_;
    std::thread([state, networkCopy, password]() {
        Result<Network::WifiConnectResult, std::string> result =
            Result<Network::WifiConnectResult, std::string>::error("WiFi connect failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect("ws://localhost:9090", 2000);
            if (connectResult.isError()) {
                result = Result<Network::WifiConnectResult, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WifiConnect::Command cmd{ .ssid = networkCopy.ssid, .password = password };
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::WifiConnect::Okay>(cmd, 25000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<Network::WifiConnectResult, std::string>::error(
                        "WifiConnect failed: " + response.errorValue());
                }
                else {
                    const auto inner = response.value();
                    if (inner.isError()) {
                        result = Result<Network::WifiConnectResult, std::string>::error(
                            "WifiConnect failed: " + inner.errorValue().message);
                    }
                    else {
                        result = Result<Network::WifiConnectResult, std::string>::okay(
                            toUiWifiConnectResult(inner.value()));
                    }
                }
            }
        }
        catch (const std::exception& e) {
            LOG_WARN(Controls, "WiFi connect exception: {}", e.what());
            result = Result<Network::WifiConnectResult, std::string>::error(e.what());
        }
        catch (...) {
            LOG_WARN(Controls, "WiFi connect exception: unknown");
            result = Result<Network::WifiConnectResult, std::string>::error("WiFi connect failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingConnect = result;
    }).detach();
}

void NetworkDiagnosticsPanel::startAsyncForget(const Network::WifiNetworkInfo& network)
{
    Network::WifiNetworkInfo networkCopy = network;
    if (!beginAsyncAction(AsyncActionKind::Forget, networkCopy, "Forgetting")) {
        return;
    }

    auto state = asyncState_;
    std::thread([state, networkCopy]() {
        Result<Network::WifiForgetResult, std::string> result =
            Result<Network::WifiForgetResult, std::string>::error("WiFi forget failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect("ws://localhost:9090", 2000);
            if (connectResult.isError()) {
                result = Result<Network::WifiForgetResult, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WifiForget::Command cmd{ .ssid = networkCopy.ssid };
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::WifiForget::Okay>(cmd, 25000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<Network::WifiForgetResult, std::string>::error(
                        "WifiForget failed: " + response.errorValue());
                }
                else {
                    const auto inner = response.value();
                    if (inner.isError()) {
                        result = Result<Network::WifiForgetResult, std::string>::error(
                            "WifiForget failed: " + inner.errorValue().message);
                    }
                    else {
                        result = Result<Network::WifiForgetResult, std::string>::okay(
                            toUiWifiForgetResult(inner.value()));
                    }
                }
            }
        }
        catch (const std::exception& e) {
            LOG_WARN(Controls, "WiFi forget exception: {}", e.what());
            result = Result<Network::WifiForgetResult, std::string>::error(e.what());
        }
        catch (...) {
            LOG_WARN(Controls, "WiFi forget exception: unknown");
            result = Result<Network::WifiForgetResult, std::string>::error("WiFi forget failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingForget = result;
    }).detach();
}

bool NetworkDiagnosticsPanel::startAsyncWebUiAccessSet(bool enabled)
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->webUiUpdateInProgress) {
            return false;
        }
        asyncState_->webUiUpdateInProgress = true;
    }

    if (refreshTimer_) {
        lv_timer_resume(refreshTimer_);
    }

    auto state = asyncState_;
    std::thread([state, enabled]() {
        auto fetchAccessStatus = []() -> Result<NetworkAccessStatus, std::string> {
            Network::WebSocketService client;
            const std::string address = "ws://localhost:9090";
            auto connectResult = client.connect(address, 2000);
            if (connectResult.isError()) {
                return Result<NetworkAccessStatus, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }

            OsApi::SystemStatus::Command statusCmd{};
            auto statusResponse =
                client.sendCommandAndGetResponse<OsApi::SystemStatus::Okay>(statusCmd, 2000);
            client.disconnect();

            if (statusResponse.isError()) {
                return Result<NetworkAccessStatus, std::string>::error(
                    "SystemStatus failed: " + statusResponse.errorValue());
            }

            const auto inner = statusResponse.value();
            if (inner.isError()) {
                return Result<NetworkAccessStatus, std::string>::error(
                    "SystemStatus failed: " + inner.errorValue().message);
            }

            NetworkAccessStatus status;
            status.webUiEnabled = inner.value().lan_web_ui_enabled;
            status.webSocketEnabled = inner.value().lan_websocket_enabled;
            status.webSocketToken = inner.value().lan_websocket_token;
            return Result<NetworkAccessStatus, std::string>::okay(std::move(status));
        };

        Result<NetworkAccessStatus, std::string> result =
            Result<NetworkAccessStatus, std::string>::error("Unknown error");

        try {
            Network::WebSocketService client;
            const std::string address = "ws://localhost:9090";
            auto connectResult = client.connect(address, 2000);
            if (connectResult.isError()) {
                result = Result<NetworkAccessStatus, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WebUiAccessSet::Command cmd{ .enabled = enabled };
                auto response =
                    client.sendCommandAndGetResponse<OsApi::WebUiAccessSet::Okay>(cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<NetworkAccessStatus, std::string>::error(
                        "WebUiAccessSet failed: " + response.errorValue());
                }
                else {
                    const auto inner = response.value();
                    if (inner.isError()) {
                        result = Result<NetworkAccessStatus, std::string>::error(
                            "WebUiAccessSet failed: " + inner.errorValue().message);
                    }
                    else {
                        result = fetchAccessStatus();
                    }
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<NetworkAccessStatus, std::string>::error(e.what());
        }
        catch (...) {
            result = Result<NetworkAccessStatus, std::string>::error("Web UI update failed");
        }

        if (!result.isError()) {
            NetworkAccessCache cache;
            cache.webUiEnabled = result.value().webUiEnabled;
            cache.webSocketEnabled = result.value().webSocketEnabled;
            cache.webSocketToken = result.value().webSocketToken;
            updateAccessCache(cache);
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingWebUiUpdate = result;
        state->webUiUpdateInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncWebSocketAccessSet(bool enabled)
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->webSocketUpdateInProgress) {
            return false;
        }
        asyncState_->webSocketUpdateInProgress = true;
    }

    if (refreshTimer_) {
        lv_timer_resume(refreshTimer_);
    }

    auto state = asyncState_;
    std::thread([state, enabled]() {
        auto fetchAccessStatus = []() -> Result<NetworkAccessStatus, std::string> {
            Network::WebSocketService client;
            const std::string address = "ws://localhost:9090";
            auto connectResult = client.connect(address, 2000);
            if (connectResult.isError()) {
                return Result<NetworkAccessStatus, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }

            OsApi::SystemStatus::Command statusCmd{};
            auto statusResponse =
                client.sendCommandAndGetResponse<OsApi::SystemStatus::Okay>(statusCmd, 2000);
            client.disconnect();

            if (statusResponse.isError()) {
                return Result<NetworkAccessStatus, std::string>::error(
                    "SystemStatus failed: " + statusResponse.errorValue());
            }

            const auto inner = statusResponse.value();
            if (inner.isError()) {
                return Result<NetworkAccessStatus, std::string>::error(
                    "SystemStatus failed: " + inner.errorValue().message);
            }

            NetworkAccessStatus status;
            status.webUiEnabled = inner.value().lan_web_ui_enabled;
            status.webSocketEnabled = inner.value().lan_websocket_enabled;
            status.webSocketToken = inner.value().lan_websocket_token;
            return Result<NetworkAccessStatus, std::string>::okay(std::move(status));
        };

        Result<NetworkAccessStatus, std::string> result =
            Result<NetworkAccessStatus, std::string>::error("Unknown error");

        try {
            Network::WebSocketService client;
            const std::string address = "ws://localhost:9090";
            auto connectResult = client.connect(address, 2000);
            if (connectResult.isError()) {
                result = Result<NetworkAccessStatus, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WebSocketAccessSet::Command cmd{ .enabled = enabled };
                auto response =
                    client.sendCommandAndGetResponse<OsApi::WebSocketAccessSet::Okay>(cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<NetworkAccessStatus, std::string>::error(
                        "WebSocketAccessSet failed: " + response.errorValue());
                }
                else {
                    const auto inner = response.value();
                    if (inner.isError()) {
                        result = Result<NetworkAccessStatus, std::string>::error(
                            "WebSocketAccessSet failed: " + inner.errorValue().message);
                    }
                    else {
                        result = fetchAccessStatus();
                    }
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<NetworkAccessStatus, std::string>::error(e.what());
        }
        catch (...) {
            result = Result<NetworkAccessStatus, std::string>::error("WebSocket update failed");
        }

        if (!result.isError()) {
            NetworkAccessCache cache;
            cache.webUiEnabled = result.value().webUiEnabled;
            cache.webSocketEnabled = result.value().webSocketEnabled;
            cache.webSocketToken = result.value().webSocketToken;
            updateAccessCache(cache);
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingWebSocketUpdate = result;
        state->webSocketUpdateInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::beginAsyncAction(
    AsyncActionKind kind, const Network::WifiNetworkInfo& network, const std::string& verb)
{
    if (isActionInProgress()) {
        return false;
    }

    actionState_.kind = kind;
    actionState_.ssid = network.ssid;

    std::string text = verb;
    if (!network.ssid.empty()) {
        text += " " + network.ssid;
    }
    text += "...";
    setWifiStatusMessage(text, 0x00CED1);

    setRefreshButtonEnabled(false);
    if (!networks_.empty()) {
        updateNetworkDisplay(
            Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
    }

    if (refreshTimer_) {
        lv_timer_resume(refreshTimer_);
    }

    return true;
}

void NetworkDiagnosticsPanel::endAsyncAction(AsyncActionKind kind)
{
    if (actionState_.kind != kind) {
        return;
    }

    actionState_.kind = AsyncActionKind::None;
    actionState_.ssid.clear();
}

bool NetworkDiagnosticsPanel::isActionInProgress() const
{
    return actionState_.kind != AsyncActionKind::None;
}

void NetworkDiagnosticsPanel::submitPasswordPrompt()
{
    if (!passwordPromptNetwork_.has_value() || !passwordTextArea_) {
        return;
    }

    const char* passwordText = lv_textarea_get_text(passwordTextArea_);
    if (!passwordText || *passwordText == '\0') {
        setPasswordPromptError("Password is required.");
        updatePasswordJoinButton();
        return;
    }

    setPasswordPromptError("");
    setPasswordPromptBusy(true);
    startAsyncConnect(passwordPromptNetwork_.value(), std::string(passwordText));
}

void NetworkDiagnosticsPanel::updatePasswordJoinButton()
{
    if (!passwordJoinButton_) {
        return;
    }

    const char* passwordText = passwordTextArea_ ? lv_textarea_get_text(passwordTextArea_) : "";
    const bool hasPassword = passwordText && *passwordText != '\0';
    setActionButtonEnabled(passwordJoinButton_, hasPassword && !isActionInProgress());
}

void NetworkDiagnosticsPanel::updatePasswordVisibilityButton()
{
    if (!passwordVisibilityButton_ || !passwordTextArea_) {
        return;
    }

    lv_textarea_set_password_mode(passwordTextArea_, !passwordVisible_);
    setActionButtonText(passwordVisibilityButton_, passwordVisible_ ? "Hide" : "Show");
}

void NetworkDiagnosticsPanel::updateWifiStatus(
    const Result<Network::WifiStatus, std::string>& statusResult)
{
    if (statusResult.isError()) {
        setWifiStatusMessage("Wi-Fi unavailable", ERROR_TEXT_COLOR);
        LOG_WARN(Controls, "WiFi status failed: {}", statusResult.errorValue());
        return;
    }

    const auto& status = statusResult.value();
    if (!status.connected || status.ssid.empty()) {
        setWifiStatusMessage("Wi-Fi disconnected", MUTED_TEXT_COLOR);
        return;
    }

    if (!isActionInProgress()) {
        setWifiStatusMessage("", MUTED_TEXT_COLOR);
    }
}

void NetworkDiagnosticsPanel::updateWebUiStatus(
    const Result<NetworkAccessStatus, std::string>& statusResult)
{
    if (statusResult.isError()) {
        LOG_WARN(Controls, "LAN Web UI status failed: {}", statusResult.errorValue());
        return;
    }

    const auto& status = statusResult.value();
    webUiEnabled_ = status.webUiEnabled;

    if (webUiToggle_) {
        webUiToggleLocked_ = true;
        if (status.webUiEnabled) {
            lv_obj_add_state(webUiToggle_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(webUiToggle_, LV_STATE_CHECKED);
        }
        webUiToggleLocked_ = false;
    }
}

void NetworkDiagnosticsPanel::updateWebSocketStatus(
    const Result<NetworkAccessStatus, std::string>& statusResult)
{
    if (statusResult.isError()) {
        LOG_WARN(Controls, "WebSocket status failed: {}", statusResult.errorValue());
        if (webSocketTokenTitleLabel_) {
            lv_label_set_text(webSocketTokenTitleLabel_, "WebSocket token");
        }
        if (webSocketTokenLabel_) {
            lv_label_set_text(webSocketTokenLabel_, "unavailable");
        }
        return;
    }

    const auto& status = statusResult.value();
    webSocketEnabled_ = status.webSocketEnabled;
    webSocketToken_ = status.webSocketToken;

    if (webSocketToggle_) {
        webSocketToggleLocked_ = true;
        if (status.webSocketEnabled) {
            lv_obj_add_state(webSocketToggle_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(webSocketToggle_, LV_STATE_CHECKED);
        }
        webSocketToggleLocked_ = false;
    }

    updateWebSocketTokenLabel();
}

void NetworkDiagnosticsPanel::updateWebSocketTokenLabel()
{
    if (!webSocketTokenTitleLabel_ || !webSocketTokenLabel_) {
        return;
    }

    lv_label_set_text(webSocketTokenTitleLabel_, "WebSocket token");
    if (!webSocketEnabled_) {
        lv_label_set_text(webSocketTokenLabel_, "--");
        return;
    }

    const std::string labelText = webSocketToken_.empty() ? "--" : webSocketToken_;
    lv_label_set_text(webSocketTokenLabel_, labelText.c_str());
}

std::string NetworkDiagnosticsPanel::formatAddressSummary() const
{
    std::vector<std::string> parts;
    parts.reserve(localAddresses_.size());
    for (const auto& info : localAddresses_) {
        parts.push_back(info.name + " " + info.address);
    }

    return joinTextParts(parts, " • ");
}

std::string NetworkDiagnosticsPanel::formatCurrentConnectionDetails(
    const Network::WifiNetworkInfo& info) const
{
    std::vector<std::string> parts;
    if (info.signalDbm.has_value()) {
        parts.push_back(std::to_string(info.signalDbm.value()) + " dBm");
    }

    parts.push_back(info.security.empty() ? "unknown" : info.security);

    const std::string addressSummary = formatAddressSummary();
    if (!addressSummary.empty()) {
        parts.push_back(addressSummary);
    }

    if (!info.lastUsedRelative.empty() && info.lastUsedRelative != "not saved") {
        parts.push_back("used " + info.lastUsedRelative);
    }

    return joinTextParts(parts, " • ");
}

std::string NetworkDiagnosticsPanel::formatNetworkDetails(
    const Network::WifiNetworkInfo& info) const
{
    std::vector<std::string> parts;
    if (info.signalDbm.has_value()) {
        parts.push_back(std::to_string(info.signalDbm.value()) + " dBm");
    }

    parts.push_back(info.security.empty() ? "unknown" : info.security);

    if (!info.connectionId.empty()) {
        if (!info.lastUsedRelative.empty() && info.lastUsedRelative != "not saved") {
            parts.push_back("used " + info.lastUsedRelative);
        }
        else {
            parts.push_back("saved");
        }
    }
    else if (!info.lastUsedRelative.empty()) {
        parts.push_back(info.lastUsedRelative);
    }

    return joinTextParts(parts, " • ");
}

void NetworkDiagnosticsPanel::updateNetworkDisplay(
    const Result<std::vector<Network::WifiNetworkInfo>, std::string>& listResult)
{
    if (!networksContainer_) {
        return;
    }

    lv_obj_clean(networksContainer_);
    networks_.clear();
    connectContexts_.clear();
    forgetContexts_.clear();

    if (listResult.isError()) {
        updateCurrentConnectionSummary();
        std::string text = "WiFi unavailable: " + listResult.errorValue();
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF6666), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        return;
    }

    networks_ = listResult.value();
    updateCurrentConnectionSummary();

    bool hasConnectedNetwork = false;
    bool hasVisibleNetworks = false;

    for (size_t i = 0; i < networks_.size(); ++i) {
        const auto& network = networks_[i];
        if (network.status == Network::WifiNetworkStatus::Connected) {
            hasConnectedNetwork = true;
            continue;
        }

        hasVisibleNetworks = true;

        lv_obj_t* row = lv_obj_create(networksContainer_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(NETWORK_ROW_BG_COLOR), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(NETWORK_ROW_BORDER_COLOR), 0);
        lv_obj_set_style_radius(row, 10, 0);

        lv_obj_t* textColumn = lv_obj_create(row);
        lv_obj_set_size(textColumn, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textColumn, 1);
        lv_obj_set_flex_flow(textColumn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(textColumn, 0, 0);
        lv_obj_set_style_pad_row(textColumn, 2, 0);
        lv_obj_set_style_bg_opa(textColumn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textColumn, 0, 0);

        lv_obj_t* ssidLabel = lv_label_create(textColumn);
        lv_label_set_text(ssidLabel, network.ssid.c_str());
        lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(
            ssidLabel,
            network.status == Network::WifiNetworkStatus::Connected
                ? lv_color_hex(0x00FF7F)
                : (network.status == Network::WifiNetworkStatus::Open
                       ? lv_color_hex(0x00CED1)
                       : (network.status == Network::WifiNetworkStatus::Available
                              ? lv_color_hex(0xFFD166)
                              : lv_color_hex(0xFFFFFF))),
            0);
        lv_label_set_long_mode(ssidLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssidLabel, LV_PCT(100));

        const std::string details = formatNetworkDetails(network);
        lv_obj_t* detailsLabel = lv_label_create(textColumn);
        lv_label_set_text(detailsLabel, details.c_str());
        lv_obj_set_style_text_font(detailsLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(detailsLabel, lv_color_hex(MUTED_TEXT_COLOR), 0);
        lv_label_set_long_mode(detailsLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(detailsLabel, LV_PCT(100));

        const bool isConnecting =
            actionState_.kind == AsyncActionKind::Connect && network.ssid == actionState_.ssid;
        const bool actionsDisabled = isActionInProgress();
        const bool requiresPassword = networkRequiresPassword(network);

        std::string buttonText =
            (network.status == Network::WifiNetworkStatus::Open || requiresPassword) ? "Join"
                                                                                     : "Connect";
        if (isConnecting) {
            buttonText = (network.status == Network::WifiNetworkStatus::Open || requiresPassword)
                ? "Joining"
                : "Connecting";
        }

        auto context = std::make_unique<ConnectContext>();
        context->panel = this;
        context->index = i;
        connectContexts_.push_back(std::move(context));
        ConnectContext* contextPtr = connectContexts_.back().get();

        lv_obj_t* buttonContainer = LVGLBuilder::actionButton(row)
                                        .text(buttonText.c_str())
                                        .mode(LVGLBuilder::ActionMode::Push)
                                        .width(92)
                                        .height(40)
                                        .callback(onConnectClicked, contextPtr)
                                        .buildOrLog();
        setActionButtonEnabled(buttonContainer, !actionsDisabled);
    }

    if (!hasVisibleNetworks) {
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(
            label,
            hasConnectedNetwork ? "No other Wi-Fi networks found" : "No Wi-Fi networks found");
        lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }
}

void NetworkDiagnosticsPanel::onRefreshClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    NetworkDiagnosticsPanel* self =
        static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->refresh();
        LOG_INFO(Controls, "Network info refreshed by user");
    }
}

void NetworkDiagnosticsPanel::applyPendingUpdates()
{
    if (!asyncState_) {
        return;
    }

    std::optional<Result<Network::WifiConnectResult, std::string>> connectResult;
    std::optional<Result<Network::WifiForgetResult, std::string>> forgetResult;
    std::optional<PendingRefreshData> refreshData;
    std::optional<Result<NetworkAccessStatus, std::string>> webSocketUpdateResult;
    std::optional<Result<NetworkAccessStatus, std::string>> webUiUpdateResult;

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        connectResult = asyncState_->pendingConnect;
        asyncState_->pendingConnect.reset();

        forgetResult = asyncState_->pendingForget;
        asyncState_->pendingForget.reset();

        refreshData = asyncState_->pendingRefresh;
        asyncState_->pendingRefresh.reset();

        webSocketUpdateResult = asyncState_->pendingWebSocketUpdate;
        asyncState_->pendingWebSocketUpdate.reset();

        webUiUpdateResult = asyncState_->pendingWebUiUpdate;
        asyncState_->pendingWebUiUpdate.reset();
    }

    if (connectResult.has_value()) {
        const bool passwordPromptConnect = passwordPromptNetwork_.has_value()
            && actionState_.kind == AsyncActionKind::Connect
            && actionState_.ssid == passwordPromptNetwork_->ssid;
        endAsyncAction(AsyncActionKind::Connect);
        if (connectResult->isError()) {
            LOG_WARN(Controls, "WiFi connect failed: {}", connectResult->errorValue());
            setWifiStatusMessage("Wi-Fi connect failed", ERROR_TEXT_COLOR);
            if (passwordPromptConnect) {
                setPasswordPromptBusy(false);
                setPasswordPromptError(connectResult->errorValue());
            }
            if (!networks_.empty()) {
                updateNetworkDisplay(
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
            }
        }
        else {
            LOG_INFO(Controls, "WiFi connect requested for {}", connectResult->value().ssid);
            if (passwordPromptConnect) {
                closePasswordPrompt();
            }
            refresh();
        }
    }

    if (forgetResult.has_value()) {
        endAsyncAction(AsyncActionKind::Forget);
        if (forgetResult->isError()) {
            LOG_WARN(Controls, "WiFi forget failed: {}", forgetResult->errorValue());
            setWifiStatusMessage("Wi-Fi forget failed", ERROR_TEXT_COLOR);
            if (!networks_.empty()) {
                updateNetworkDisplay(
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
            }
        }
        else {
            LOG_INFO(Controls, "WiFi forget completed for {}", forgetResult->value().ssid);
            refresh();
        }
    }

    if (refreshData.has_value()) {
        if (refreshData->localAddresses.has_value()) {
            localAddresses_ = std::move(refreshData->localAddresses.value());

            const std::string addressSummary = formatAddressSummary();
            if (addressSummary.empty()) {
                LOG_DEBUG(Controls, "No non-loopback IPv4 addresses found.");
            }
            else {
                LOG_DEBUG(Controls, "Network addresses updated: {}", addressSummary);
            }
        }
        updateWifiStatus(refreshData->statusResult);
        updateNetworkDisplay(refreshData->listResult);
        updateWebUiStatus(refreshData->accessStatusResult);
        updateWebSocketStatus(refreshData->accessStatusResult);
    }

    if (webUiUpdateResult.has_value()) {
        setWebUiToggleEnabled(true);
        if (webUiUpdateResult->isError()) {
            LOG_WARN(Controls, "LAN Web UI update failed: {}", webUiUpdateResult->errorValue());
            if (webUiToggle_) {
                webUiToggleLocked_ = true;
                if (webUiEnabled_) {
                    lv_obj_add_state(webUiToggle_, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_clear_state(webUiToggle_, LV_STATE_CHECKED);
                }
                webUiToggleLocked_ = false;
            }
        }
        else {
            updateWebUiStatus(*webUiUpdateResult);
            updateWebSocketStatus(*webUiUpdateResult);
        }
    }

    if (webSocketUpdateResult.has_value()) {
        setWebSocketToggleEnabled(true);
        if (webSocketUpdateResult->isError()) {
            LOG_WARN(Controls, "WebSocket update failed: {}", webSocketUpdateResult->errorValue());
            if (webSocketToggle_) {
                webSocketToggleLocked_ = true;
                if (webSocketEnabled_) {
                    lv_obj_add_state(webSocketToggle_, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_clear_state(webSocketToggle_, LV_STATE_CHECKED);
                }
                webSocketToggleLocked_ = false;
            }
            updateWebSocketTokenLabel();
        }
        else {
            updateWebUiStatus(*webSocketUpdateResult);
            updateWebSocketStatus(*webSocketUpdateResult);
        }
    }

    bool refreshInProgress = false;
    bool hasPending = false;
    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        refreshInProgress = asyncState_->refreshInProgress;
        hasPending = asyncState_->pendingRefresh.has_value()
            || asyncState_->pendingConnect.has_value() || asyncState_->pendingForget.has_value()
            || asyncState_->pendingWebSocketUpdate.has_value()
            || asyncState_->pendingWebUiUpdate.has_value() || asyncState_->webSocketUpdateInProgress
            || asyncState_->webUiUpdateInProgress;
    }

    if (!refreshInProgress && !isActionInProgress() && !hasPending && refreshTimer_) {
        lv_timer_pause(refreshTimer_);
        setRefreshButtonEnabled(true);
    }
}

void NetworkDiagnosticsPanel::onRefreshTimer(lv_timer_t* timer)
{
    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    self->applyPendingUpdates();
}

void NetworkDiagnosticsPanel::onConnectClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ConnectContext* ctx = static_cast<ConnectContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->panel) {
        return;
    }

    if (ctx->index >= ctx->panel->networks_.size()) {
        return;
    }

    const auto& network = ctx->panel->networks_[ctx->index];
    if (ctx->panel->networkRequiresPassword(network)) {
        ctx->panel->openPasswordPrompt(network);
        return;
    }

    ctx->panel->startAsyncConnect(network);
}

void NetworkDiagnosticsPanel::onForgetClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ForgetContext* ctx = static_cast<ForgetContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->panel) {
        return;
    }

    if (ctx->index >= ctx->panel->networks_.size()) {
        return;
    }

    ctx->panel->startAsyncForget(ctx->panel->networks_[ctx->index]);
}

void NetworkDiagnosticsPanel::onPasswordCancelClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || self->isActionInProgress()) {
        return;
    }

    self->closePasswordPrompt();
}

void NetworkDiagnosticsPanel::onPasswordJoinClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->submitPasswordPrompt();
}

void NetworkDiagnosticsPanel::onPasswordKeyboardEvent(lv_event_t* e)
{
    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL && !self->isActionInProgress()) {
        self->closePasswordPrompt();
        return;
    }

    if (code == LV_EVENT_READY) {
        self->submitPasswordPrompt();
    }
}

void NetworkDiagnosticsPanel::onPasswordTextAreaEvent(lv_event_t* e)
{
    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        self->setPasswordPromptError("");
        self->updatePasswordJoinButton();
        return;
    }

    if (code == LV_EVENT_READY) {
        self->submitPasswordPrompt();
    }
}

void NetworkDiagnosticsPanel::onPasswordVisibilityClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->passwordTextArea_ || self->isActionInProgress()) {
        return;
    }

    self->passwordVisible_ = !self->passwordVisible_;
    self->updatePasswordVisibilityButton();
}

void NetworkDiagnosticsPanel::onWebSocketToggleChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    NetworkDiagnosticsPanel* self =
        static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || self->webSocketToggleLocked_) {
        return;
    }

    if (!self->webSocketToggle_) {
        return;
    }

    const bool enabled = lv_obj_has_state(self->webSocketToggle_, LV_STATE_CHECKED);
    self->setWebSocketToggleEnabled(false);
    if (!self->startAsyncWebSocketAccessSet(enabled)) {
        self->setWebSocketToggleEnabled(true);
        self->webSocketToggleLocked_ = true;
        if (self->webSocketEnabled_) {
            lv_obj_add_state(self->webSocketToggle_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(self->webSocketToggle_, LV_STATE_CHECKED);
        }
        self->webSocketToggleLocked_ = false;
    }
}

void NetworkDiagnosticsPanel::onWebUiToggleChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    NetworkDiagnosticsPanel* self =
        static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || self->webUiToggleLocked_) {
        return;
    }

    if (!self->webUiToggle_) {
        return;
    }

    const bool enabled = lv_obj_has_state(self->webUiToggle_, LV_STATE_CHECKED);
    self->setWebUiToggleEnabled(false);
    if (!self->startAsyncWebUiAccessSet(enabled)) {
        self->setWebUiToggleEnabled(true);
        self->webUiToggleLocked_ = true;
        if (self->webUiEnabled_) {
            lv_obj_add_state(self->webUiToggle_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(self->webUiToggle_, LV_STATE_CHECKED);
        }
        self->webUiToggleLocked_ = false;
    }
}

} // namespace Ui
} // namespace DirtSim
