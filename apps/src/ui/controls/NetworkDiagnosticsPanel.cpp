#include "NetworkDiagnosticsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/ClientHello.h"
#include "core/network/WebSocketService.h"
#include "os-manager/api/NetworkDiagnosticsModeSet.h"
#include "os-manager/api/NetworkSnapshotChanged.h"
#include "os-manager/api/NetworkSnapshotGet.h"
#include "os-manager/api/ScannerModeEnter.h"
#include "os-manager/api/ScannerModeExit.h"
#include "os-manager/api/ScannerSnapshotGet.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include "os-manager/api/WifiConnect.h"
#include "os-manager/api/WifiConnectCancel.h"
#include "os-manager/api/WifiDisconnect.h"
#include "os-manager/api/WifiForget.h"
#include "os-manager/api/WifiScanRequest.h"
#include "server/api/UserSettingsPatch.h"
#include "ui/UserSettingsManager.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include "ui/widgets/TimeSeriesPlotWidget.h"
#include <algorithm>
#include <exception>
#include <optional>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_set>

namespace DirtSim {
namespace Ui {

namespace {
struct NetworkAccessCache {
    bool webUiEnabled = false;
    bool webSocketEnabled = false;
    std::string webSocketToken;
    bool scannerModeAvailable = false;
    bool scannerModeActive = false;
    std::string scannerModeDetail;
};

std::mutex accessCacheMutex;
NetworkAccessCache accessCache;
constexpr int OS_MANAGER_CONNECT_TIMEOUT_MS = 35000;

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

bool isConnectFinalizing(const std::optional<Network::WifiConnectProgress>& progress)
{
    return progress.has_value() && !progress->canCancel
        && progress->phase != Network::WifiConnectPhase::Canceling;
}

std::string toAutomationPhaseText(const Network::WifiConnectPhase phase)
{
    switch (phase) {
        case Network::WifiConnectPhase::Starting:
            return "Starting";
        case Network::WifiConnectPhase::Associating:
            return "Associating";
        case Network::WifiConnectPhase::Authenticating:
            return "Authenticating";
        case Network::WifiConnectPhase::GettingAddress:
            return "GettingAddress";
        case Network::WifiConnectPhase::Canceling:
            return "Canceling";
    }

    return "Starting";
}

std::string toAutomationStatusText(const Network::WifiNetworkStatus status)
{
    switch (status) {
        case Network::WifiNetworkStatus::Connected:
            return "Connected";
        case Network::WifiNetworkStatus::Saved:
            return "Saved";
        case Network::WifiNetworkStatus::Open:
            return "Open";
        case Network::WifiNetworkStatus::Available:
            return "Available";
    }

    return "Saved";
}

std::string labelText(lv_obj_t* label)
{
    if (!label) {
        return "";
    }

    const char* text = lv_label_get_text(label);
    return text ? std::string(text) : "";
}

void setNetworkDiagnosticsModeAsync(const bool active)
{
    std::thread([active]() {
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect("ws://localhost:9090", 2000);
            if (connectResult.isError()) {
                LOG_WARN(
                    Controls,
                    "Failed to connect to os-manager for diagnostics mode update: {}",
                    connectResult.errorValue());
                return;
            }

            OsApi::NetworkDiagnosticsModeSet::Command cmd{ .active = active };
            const auto response =
                client.sendCommandAndGetResponse<OsApi::NetworkDiagnosticsModeSet::Okay>(cmd, 2000);
            client.disconnect();

            if (response.isError()) {
                LOG_WARN(
                    Controls,
                    "NetworkDiagnosticsModeSet transport failed: {}",
                    response.errorValue());
                return;
            }
            if (response.value().isError()) {
                LOG_WARN(
                    Controls,
                    "NetworkDiagnosticsModeSet failed: {}",
                    response.value().errorValue().message);
                return;
            }

            LOG_INFO(Controls, "Network diagnostics mode {}.", active ? "enabled" : "disabled");
        }
        catch (const std::exception& e) {
            LOG_WARN(Controls, "Network diagnostics mode update failed: {}", e.what());
        }
    }).detach();
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

Network::WifiAccessPointInfo toUiWifiAccessPointInfo(
    const OsApi::NetworkSnapshotGet::WifiAccessPointInfo& info)
{
    return Network::WifiAccessPointInfo{
        .ssid = info.ssid,
        .bssid = info.bssid,
        .signalDbm = info.signalDbm,
        .frequencyMhz = info.frequencyMhz,
        .channel = info.channel,
        .security = info.security,
        .active = info.active,
    };
}

Network::WifiConnectResult toUiWifiConnectResult(const OsApi::WifiConnect::Okay& result)
{
    return Network::WifiConnectResult{ .success = result.success, .ssid = result.ssid };
}

Network::WifiDisconnectResult toUiWifiDisconnectResult(const OsApi::WifiDisconnect::Okay& result)
{
    return Network::WifiDisconnectResult{ .success = result.success, .ssid = result.ssid };
}

Network::WifiConnectPhase toUiWifiConnectPhase(
    const OsApi::NetworkSnapshotGet::WifiConnectPhase phase)
{
    switch (phase) {
        case OsApi::NetworkSnapshotGet::WifiConnectPhase::Starting:
            return Network::WifiConnectPhase::Starting;
        case OsApi::NetworkSnapshotGet::WifiConnectPhase::Associating:
            return Network::WifiConnectPhase::Associating;
        case OsApi::NetworkSnapshotGet::WifiConnectPhase::Authenticating:
            return Network::WifiConnectPhase::Authenticating;
        case OsApi::NetworkSnapshotGet::WifiConnectPhase::GettingAddress:
            return Network::WifiConnectPhase::GettingAddress;
        case OsApi::NetworkSnapshotGet::WifiConnectPhase::Canceling:
            return Network::WifiConnectPhase::Canceling;
    }

    return Network::WifiConnectPhase::Starting;
}

Network::WifiConnectProgress toUiWifiConnectProgress(
    const OsApi::NetworkSnapshotGet::WifiConnectProgressInfo& info)
{
    return Network::WifiConnectProgress{
        .ssid = info.ssid,
        .phase = toUiWifiConnectPhase(info.phase),
        .canCancel = info.canCancel,
    };
}

Network::WifiConnectOutcome toUiWifiConnectOutcome(
    const OsApi::NetworkSnapshotGet::WifiConnectOutcomeInfo& info)
{
    return Network::WifiConnectOutcome{
        .ssid = info.ssid,
        .message = info.message,
        .canceled = info.canceled,
    };
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
constexpr uint32_t CONNECT_STAGE_ACTIVE_BG_COLOR = 0xFFDD66;
constexpr uint32_t CONNECT_STAGE_ACTIVE_BORDER_COLOR = 0xFFDD66;
constexpr uint32_t CONNECT_STAGE_ACTIVE_TEXT_COLOR = 0x101010;
constexpr uint32_t CONNECT_STAGE_COMPLETE_BG_COLOR = 0x21484F;
constexpr uint32_t CONNECT_STAGE_COMPLETE_BORDER_COLOR = 0x3C7E88;
constexpr uint32_t CONNECT_STAGE_COMPLETE_TEXT_COLOR = 0xE4F9FF;
constexpr uint32_t CONNECT_STAGE_PENDING_BG_COLOR = 0x202020;
constexpr uint32_t CONNECT_STAGE_PENDING_BORDER_COLOR = 0x454545;
constexpr uint32_t CONNECT_STAGE_PENDING_TEXT_COLOR = 0x808080;
constexpr int NETWORK_SNAPSHOT_TIMEOUT_MS = 12000;
constexpr int NETWORK_ACTION_BUTTON_HEIGHT = 72;
constexpr int NETWORK_CARD_PADDING = 16;
constexpr int NETWORK_CARD_ROW_PADDING = 12;
constexpr int NETWORK_ROW_BUTTON_WIDTH = 116;
constexpr int NETWORK_ROW_PADDING = 12;
constexpr int NETWORK_SUMMARY_BUTTON_WIDTH = 90;
constexpr int NETWORK_TEXT_INPUT_HEIGHT = 72;
constexpr int NETWORK_TEXT_INPUT_PAD_HOR = 8;
constexpr int NETWORK_TEXT_INPUT_PAD_VER = 12;
constexpr int NETWORK_OVERLAY_BUTTON_WIDTH = 108;
constexpr int NETWORK_PROGRESS_CANCEL_BUTTON_WIDTH = 164;
constexpr int NETWORK_REFRESH_BUTTON_WIDTH = 224;
constexpr int NETWORK_DETAILS_SUMMARY_WIDTH = 280;
constexpr int NETWORK_RADIO_PLOT_HEIGHT = 64;
constexpr int NETWORK_RADIO_CARD_PADDING = 8;
constexpr int NETWORK_RADIO_CARD_ROW_PADDING = 4;
constexpr int NETWORK_RADIOS_CONTENT_ROW_PADDING = 8;
constexpr int NETWORK_STAGE_BADGE_HEIGHT = 34;
constexpr int NETWORK_SIGNAL_HISTORY_MAX_SAMPLES = 60;
constexpr int WIFI_LIST_COLUMN_GROW = 7;
constexpr int WIFI_SUMMARY_COLUMN_GROW = 4;
constexpr int WIFI_SUMMARY_MIN_WIDTH = 220;
constexpr const char* OS_MANAGER_ADDRESS = "ws://localhost:9090";

const std::array<const char*, 4> kConnectStageTitles = {
    "1 Join", "2 Associate", "3 Auth", "4 Address"
};

size_t connectPhaseStageIndex(const Network::WifiConnectPhase phase)
{
    switch (phase) {
        case Network::WifiConnectPhase::Starting:
            return 0;
        case Network::WifiConnectPhase::Associating:
            return 1;
        case Network::WifiConnectPhase::Authenticating:
            return 2;
        case Network::WifiConnectPhase::GettingAddress:
            return 3;
        case Network::WifiConnectPhase::Canceling:
            return 3;
    }

    return 0;
}
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

bool isSavedNetwork(const Network::WifiNetworkInfo& network)
{
    return !network.connectionId.empty();
}

bool shouldUseJoinSemantics(const Network::WifiNetworkInfo& network, const bool requiresPassword)
{
    return !isSavedNetwork(network)
        && (network.status == Network::WifiNetworkStatus::Open || requiresPassword);
}

const char* passwordPromptSubmitText(const Network::WifiNetworkInfo& network)
{
    return isSavedNetwork(network) && !isOpenSecurity(network.security) ? "Reconnect" : "Join";
}

const char* passwordPromptTitleText(const Network::WifiNetworkInfo& network)
{
    return isSavedNetwork(network) && !isOpenSecurity(network.security) ? "Update Password"
                                                                        : "Join Wi-Fi";
}

std::string wifiBandLabel(const std::optional<int> frequencyMhz)
{
    if (!frequencyMhz.has_value()) {
        return {};
    }

    if (frequencyMhz.value() >= 5925) {
        return "6 GHz";
    }
    if (frequencyMhz.value() >= 5000) {
        return "5 GHz";
    }
    if (frequencyMhz.value() >= 2400) {
        return "2.4 GHz";
    }

    return std::to_string(frequencyMhz.value()) + " MHz";
}

bool connectFailureNeedsPasswordPrompt(const std::string& message)
{
    return message.find("no-secrets") != std::string::npos
        || message.find("Password required") != std::string::npos;
}

std::string formatDurationAgo(const uint64_t ageMs)
{
    const uint64_t ageSeconds = ageMs / 1000;
    if (ageSeconds < 2) {
        return "just now";
    }
    if (ageSeconds < 60) {
        return std::to_string(ageSeconds) + "s ago";
    }

    const uint64_t ageMinutes = ageSeconds / 60;
    if (ageMinutes < 60) {
        return std::to_string(ageMinutes) + "m ago";
    }

    const uint64_t ageHours = ageMinutes / 60;
    return std::to_string(ageHours) + "h ago";
}

std::optional<Network::WifiAccessPointInfo> selectPreferredAccessPointForSsid(
    const std::vector<Network::WifiAccessPointInfo>& accessPoints, const std::string& ssid)
{
    std::optional<Network::WifiAccessPointInfo> bestAccessPoint;
    for (const auto& accessPoint : accessPoints) {
        if (accessPoint.ssid != ssid) {
            continue;
        }

        if (!bestAccessPoint.has_value()) {
            bestAccessPoint = accessPoint;
            continue;
        }
        if (accessPoint.active != bestAccessPoint->active) {
            if (accessPoint.active) {
                bestAccessPoint = accessPoint;
            }
            continue;
        }

        const int accessPointSignal =
            accessPoint.signalDbm.has_value() ? accessPoint.signalDbm.value() : -200;
        const int bestSignal =
            bestAccessPoint->signalDbm.has_value() ? bestAccessPoint->signalDbm.value() : -200;
        if (accessPointSignal > bestSignal
            || (accessPointSignal == bestSignal && accessPoint.bssid < bestAccessPoint->bssid)) {
            bestAccessPoint = accessPoint;
        }
    }

    return bestAccessPoint;
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

NetworkDiagnosticsPanel::NetworkDiagnosticsPanel(
    lv_obj_t* container, UserSettingsManager& userSettingsManager)
    : container_(container),
      userSettingsManager_(&userSettingsManager),
      asyncState_(std::make_shared<AsyncState>()),
      liveScanEnabled_(userSettingsManager.get().networkLiveScanPreferred)
{
    createUI();
    setNetworkDiagnosticsModeAsync(liveScanEnabled_);
    LOG_INFO(Controls, "NetworkDiagnosticsPanel created");
}

NetworkDiagnosticsPanel::~NetworkDiagnosticsPanel()
{
    setNetworkDiagnosticsModeAsync(false);
    closeNetworkDetailsOverlay();
    closePasswordPrompt();
    if (eventClient_) {
        eventClient_->disconnect();
        eventClient_.reset();
    }

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

    scannerView_ = lv_obj_create(pagesContainer_);
    stylePanelColumn(scannerView_);
    lv_obj_set_size(scannerView_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(scannerView_, 1);

    lv_obj_t* wifiCard = lv_obj_create(wifiView_);
    lv_obj_set_size(wifiCard, LV_PCT(100), 0);
    lv_obj_set_flex_grow(wifiCard, 1);
    lv_obj_set_flex_flow(wifiCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifiCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(wifiCard, 0, 0);
    lv_obj_set_style_pad_column(wifiCard, NETWORK_CARD_PADDING, 0);
    lv_obj_set_style_bg_opa(wifiCard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifiCard, 0, 0);
    lv_obj_set_style_radius(wifiCard, 0, 0);
    lv_obj_clear_flag(wifiCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* wifiSummaryColumn = lv_obj_create(wifiCard);
    lv_obj_set_size(wifiSummaryColumn, 0, LV_PCT(100));
    lv_obj_set_flex_grow(wifiSummaryColumn, WIFI_SUMMARY_COLUMN_GROW);
    lv_obj_set_style_min_width(wifiSummaryColumn, WIFI_SUMMARY_MIN_WIDTH, 0);
    lv_obj_set_flex_flow(wifiSummaryColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        wifiSummaryColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(wifiSummaryColumn, 0, 0);
    lv_obj_set_style_pad_row(wifiSummaryColumn, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_bg_opa(wifiSummaryColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifiSummaryColumn, 0, 0);
    lv_obj_clear_flag(wifiSummaryColumn, LV_OBJ_FLAG_SCROLLABLE);

    refreshButton_ = LVGLBuilder::actionButton(wifiSummaryColumn)
                         .text("Refresh")
                         .icon(LV_SYMBOL_REFRESH)
                         .mode(LVGLBuilder::ActionMode::Push)
                         .layoutRow()
                         .width(NETWORK_REFRESH_BUTTON_WIDTH)
                         .height(NETWORK_ACTION_BUTTON_HEIGHT)
                         .callback(onRefreshClicked, this)
                         .buildOrLog();

    wifiStatusLabel_ = lv_label_create(wifiSummaryColumn);
    lv_obj_set_style_text_font(wifiStatusLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(MUTED_TEXT_COLOR), 0);
    lv_obj_set_width(wifiStatusLabel_, LV_PCT(100));
    lv_label_set_long_mode(wifiStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(wifiStatusLabel_, LV_OBJ_FLAG_HIDDEN);

    currentNetworkContainer_ = lv_obj_create(wifiSummaryColumn);
    lv_obj_set_size(currentNetworkContainer_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(currentNetworkContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        currentNetworkContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(currentNetworkContainer_, NETWORK_CARD_PADDING, 0);
    lv_obj_set_style_pad_row(currentNetworkContainer_, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_bg_color(currentNetworkContainer_, lv_color_hex(ACCESSORY_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(currentNetworkContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(currentNetworkContainer_, 1, 0);
    lv_obj_set_style_border_color(
        currentNetworkContainer_, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
    lv_obj_set_style_radius(currentNetworkContainer_, 10, 0);
    lv_obj_clear_flag(currentNetworkContainer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(currentNetworkContainer_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* wifiListColumn = lv_obj_create(wifiCard);
    lv_obj_set_size(wifiListColumn, 0, LV_PCT(100));
    lv_obj_set_flex_grow(wifiListColumn, WIFI_LIST_COLUMN_GROW);
    lv_obj_set_flex_flow(wifiListColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        wifiListColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(wifiListColumn, 0, 0);
    lv_obj_set_style_pad_row(wifiListColumn, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_bg_opa(wifiListColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifiListColumn, 0, 0);
    lv_obj_clear_flag(wifiListColumn, LV_OBJ_FLAG_SCROLLABLE);

    networksTitleLabel_ = lv_label_create(wifiListColumn);
    lv_label_set_text(networksTitleLabel_, "Networks");
    lv_obj_set_style_text_font(networksTitleLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(networksTitleLabel_, lv_color_hex(HEADER_TEXT_COLOR), 0);
    lv_obj_set_width(networksTitleLabel_, LV_PCT(100));

    networksContainer_ = lv_obj_create(wifiListColumn);
    lv_obj_set_size(networksContainer_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(networksContainer_, 1);
    lv_obj_set_flex_flow(networksContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        networksContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(networksContainer_, 0, 0);
    lv_obj_set_style_pad_row(networksContainer_, NETWORK_CARD_ROW_PADDING, 0);
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

    lv_obj_t* diagnosticsCard = createSectionCard(lanAccessView_, "Diagnostics");

    lv_obj_t* diagnosticsHint = lv_label_create(diagnosticsCard);
    lv_label_set_text(
        diagnosticsHint, "Automatically enable live Wi-Fi rescans while this menu is open.");
    lv_obj_set_style_text_font(diagnosticsHint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diagnosticsHint, lv_color_hex(MUTED_TEXT_COLOR), 0);
    lv_label_set_long_mode(diagnosticsHint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(diagnosticsHint, LV_PCT(100));

    liveScanToggle_ = LVGLBuilder::labeledSwitch(diagnosticsCard)
                          .label("Live scan in Network menu")
                          .initialState(liveScanEnabled_)
                          .width(LV_PCT(100))
                          .height(72)
                          .callback(onLiveScanToggleChanged, this)
                          .buildOrLog();
    styleSwitchRow(liveScanToggle_);

    lv_obj_t* scannerCard = createSectionCard(scannerView_, "Scanner");
    lv_obj_set_height(scannerCard, 0);
    lv_obj_set_flex_grow(scannerCard, 1);
    lv_obj_add_flag(scannerCard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scannerCard, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scannerCard, LV_SCROLLBAR_MODE_AUTO);

    scannerStatusLabel_ = lv_label_create(scannerCard);
    lv_label_set_text(scannerStatusLabel_, "Scanner status unavailable.");
    lv_obj_set_style_text_font(scannerStatusLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scannerStatusLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(scannerStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(scannerStatusLabel_, LV_PCT(100));

    scannerHintLabel_ = lv_label_create(scannerCard);
    lv_label_set_text(
        scannerHintLabel_,
        "Scanner mode is exclusive. While active, wlan0 leaves NetworkManager and normal Wi-Fi "
        "connections are unavailable.");
    lv_obj_set_style_text_font(scannerHintLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scannerHintLabel_, lv_color_hex(MUTED_TEXT_COLOR), 0);
    lv_label_set_long_mode(scannerHintLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(scannerHintLabel_, LV_PCT(100));

    lv_obj_t* scannerButtonRow = lv_obj_create(scannerCard);
    lv_obj_set_size(scannerButtonRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scannerButtonRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(
        scannerButtonRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scannerButtonRow, 0, 0);
    lv_obj_set_style_pad_row(scannerButtonRow, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_pad_column(scannerButtonRow, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_bg_opa(scannerButtonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scannerButtonRow, 0, 0);
    lv_obj_clear_flag(scannerButtonRow, LV_OBJ_FLAG_SCROLLABLE);

    scannerRefreshButton_ = LVGLBuilder::actionButton(scannerButtonRow)
                                .text("Refresh Status")
                                .icon(LV_SYMBOL_REFRESH)
                                .mode(LVGLBuilder::ActionMode::Push)
                                .layoutRow()
                                .width(NETWORK_REFRESH_BUTTON_WIDTH)
                                .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                .callback(onScannerRefreshClicked, this)
                                .buildOrLog();

    scannerEnterButton_ = LVGLBuilder::actionButton(scannerButtonRow)
                              .text("Enter Scanner Mode")
                              .mode(LVGLBuilder::ActionMode::Push)
                              .width(260)
                              .height(NETWORK_ACTION_BUTTON_HEIGHT)
                              .callback(onScannerEnterClicked, this)
                              .buildOrLog();

    scannerExitButton_ = LVGLBuilder::actionButton(scannerButtonRow)
                             .text("Return to Wi-Fi")
                             .mode(LVGLBuilder::ActionMode::Push)
                             .width(220)
                             .height(NETWORK_ACTION_BUTTON_HEIGHT)
                             .callback(onScannerExitClicked, this)
                             .buildOrLog();

    lv_obj_t* channelMapTitleLabel = lv_label_create(scannerCard);
    lv_label_set_text(channelMapTitleLabel, "Channel map");
    lv_obj_set_style_text_font(channelMapTitleLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(channelMapTitleLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(channelMapTitleLabel, LV_PCT(100));

    lv_obj_t* channelMapRow = lv_obj_create(scannerCard);
    lv_obj_set_size(channelMapRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(channelMapRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        channelMapRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(channelMapRow, 0, 0);
    lv_obj_set_style_pad_column(channelMapRow, 10, 0);
    lv_obj_set_style_bg_opa(channelMapRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(channelMapRow, 0, 0);
    lv_obj_clear_flag(channelMapRow, LV_OBJ_FLAG_SCROLLABLE);

    scannerChannelPlot24_ = std::make_unique<TimeSeriesPlotWidget>(
        channelMapRow,
        TimeSeriesPlotWidget::Config{
            .title = "2.4 GHz",
            .lineColor = lv_color_hex(0x00CED1),
            .defaultMinY = -100.0f,
            .defaultMaxY = -20.0f,
            .valueScale = 1.0f,
            .autoScaleY = false,
            .hideZeroValuePoints = true,
            .showYAxisRangeLabels = false,
            .chartType = LV_CHART_TYPE_BAR,
            .barGroupGapPx = 1,
            .barSeriesGapPx = 1,
            .minPointCount = 1,
        });
    scannerChannelPlot24_->setBottomLabels("ch 1", "ch 11");

    lv_obj_t* plot24Container = scannerChannelPlot24_->getContainer();
    lv_obj_set_size(plot24Container, 0, 110);
    lv_obj_set_flex_grow(plot24Container, 1);

    scannerChannelPlot5_ = std::make_unique<TimeSeriesPlotWidget>(
        channelMapRow,
        TimeSeriesPlotWidget::Config{
            .title = "5 GHz",
            .lineColor = lv_color_hex(0xFFDD66),
            .defaultMinY = -100.0f,
            .defaultMaxY = -20.0f,
            .valueScale = 1.0f,
            .autoScaleY = false,
            .hideZeroValuePoints = true,
            .showYAxisRangeLabels = false,
            .chartType = LV_CHART_TYPE_BAR,
            .barGroupGapPx = 1,
            .barSeriesGapPx = 1,
            .minPointCount = 1,
        });
    scannerChannelPlot5_->setBottomLabels("ch 36", "ch 165");

    lv_obj_t* plot5Container = scannerChannelPlot5_->getContainer();
    lv_obj_set_size(plot5Container, 0, 110);
    lv_obj_set_flex_grow(plot5Container, 1);

    scannerDataLabel_ = lv_label_create(scannerCard);
    lv_label_set_text(scannerDataLabel_, "Scanner data will appear here.");
    lv_obj_set_style_text_font(scannerDataLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scannerDataLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(scannerDataLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(scannerDataLabel_, LV_PCT(100));

    refreshTimer_ = lv_timer_create(onRefreshTimer, 100, this);

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
    updateScannerControls();
    startEventStream();

    // Initial display update.
    refresh();
}

Result<NetworkDiagnosticsPanel::AutomationState, std::string> NetworkDiagnosticsPanel::
    getAutomationState()
{
    applyPendingUpdates();

    AutomationState state;
    switch (viewMode_) {
        case ViewMode::Wifi:
            state.viewMode = "Wifi";
            break;
        case ViewMode::LanAccess:
            state.viewMode = "LanAccess";
            break;
        case ViewMode::Scanner:
            state.viewMode = "Scanner";
            break;
    }
    state.wifiStatusMessage = labelText(wifiStatusLabel_);
    state.connectedSsid = latestWifiStatus_.has_value() && latestWifiStatus_->connected
        ? std::optional<std::string>(latestWifiStatus_->ssid)
        : std::nullopt;
    state.connectTargetSsid = connectAwaitingConfirmationSsid_.has_value()
        ? connectAwaitingConfirmationSsid_
        : (!actionState_.ssid.empty() ? std::optional<std::string>(actionState_.ssid)
                                      : std::nullopt);
    state.passwordPromptTargetSsid = passwordPromptNetwork_.has_value()
        ? std::optional<std::string>(passwordPromptNetwork_->ssid)
        : std::nullopt;
    state.passwordError = labelText(passwordErrorLabel_);
    state.scannerStatusMessage = labelText(scannerStatusLabel_);
    state.passwordPromptVisible =
        passwordOverlay_ && !lv_obj_has_flag(passwordOverlay_, LV_OBJ_FLAG_HIDDEN);
    state.connectOverlayVisible =
        state.passwordPromptVisible && connectOverlayMode_ == ConnectOverlayMode::Connecting;
    state.passwordSubmitEnabled = passwordJoinButton_
        && !lv_obj_has_state(getActionButtonInnerButton(passwordJoinButton_), LV_STATE_DISABLED);
    state.scannerModeActive = scannerModeActive_;
    state.scannerModeAvailable = scannerModeAvailable_;

    if (scannerEnterButton_) {
        const lv_obj_t* innerButton = getActionButtonInnerButton(scannerEnterButton_);
        state.scannerEnterEnabled =
            innerButton && !lv_obj_has_state(const_cast<lv_obj_t*>(innerButton), LV_STATE_DISABLED);
    }
    if (scannerExitButton_) {
        const lv_obj_t* innerButton = getActionButtonInnerButton(scannerExitButton_);
        state.scannerExitEnabled =
            innerButton && !lv_obj_has_state(const_cast<lv_obj_t*>(innerButton), LV_STATE_DISABLED);
    }

    if (connectProgress_.has_value()) {
        state.connectProgress = AutomationConnectProgress{
            .phase = toAutomationPhaseText(connectProgress_->phase),
            .ssid = connectProgress_->ssid,
            .canCancel = connectProgress_->canCancel,
        };
    }

    if (connectProgressCancelButton_) {
        const lv_obj_t* innerButton = getActionButtonInnerButton(connectProgressCancelButton_);
        state.connectCancelVisible =
            !lv_obj_has_flag(connectProgressCancelButton_, LV_OBJ_FLAG_HIDDEN);
        state.connectCancelEnabled =
            innerButton && !lv_obj_has_state(const_cast<lv_obj_t*>(innerButton), LV_STATE_DISABLED);
    }

    state.networks.reserve(networks_.size());
    for (const auto& network : networks_) {
        state.networks.push_back(
            AutomationNetworkInfo{
                .ssid = network.ssid,
                .status = toAutomationStatusText(network.status),
                .requiresPassword = networkRequiresPassword(network),
            });
    }

    return Result<AutomationState, std::string>::okay(state);
}

Result<std::monostate, std::string> NetworkDiagnosticsPanel::pressAutomationConnect(
    const std::string& ssid)
{
    applyPendingUpdates();
    if (viewMode_ != ViewMode::Wifi) {
        return Result<std::monostate, std::string>::error("WiFi view is not active");
    }
    if (isActionInProgress()) {
        return Result<std::monostate, std::string>::error("Another network action is in progress");
    }

    const auto index = findNetworkIndexBySsid(ssid);
    if (!index.has_value()) {
        return Result<std::monostate, std::string>::error("Network not found: " + ssid);
    }

    const auto& network = networks_[index.value()];
    if (networkRequiresPassword(network)) {
        openPasswordPrompt(network);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    openConnectingOverlay(network);
    if (!startAsyncConnect(network)) {
        closePasswordPrompt();
        return Result<std::monostate, std::string>::error("Failed to start WiFi connect");
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NetworkDiagnosticsPanel::pressAutomationConnectCancel()
{
    applyPendingUpdates();
    if (!connectProgressCancelButton_
        || lv_obj_has_flag(connectProgressCancelButton_, LV_OBJ_FLAG_HIDDEN)) {
        return Result<std::monostate, std::string>::error("Connect cancel is not available");
    }

    lv_obj_t* innerButton = getActionButtonInnerButton(connectProgressCancelButton_);
    if (!innerButton || lv_obj_has_state(innerButton, LV_STATE_DISABLED)) {
        return Result<std::monostate, std::string>::error("Connect cancel is disabled");
    }

    if (!startAsyncConnectCancel()) {
        return Result<std::monostate, std::string>::error("Failed to start WiFi connect cancel");
    }

    setActionButtonEnabled(connectProgressCancelButton_, false);
    setActionButtonText(connectProgressCancelButton_, "Canceling");
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NetworkDiagnosticsPanel::submitAutomationPassword(
    const std::string& password)
{
    applyPendingUpdates();
    if (!passwordPromptNetwork_.has_value() || !passwordTextArea_) {
        return Result<std::monostate, std::string>::error("Password prompt is not open");
    }
    if (isActionInProgress()) {
        return Result<std::monostate, std::string>::error("Another network action is in progress");
    }

    lv_textarea_set_text(passwordTextArea_, password.c_str());
    updatePasswordJoinButton();
    submitPasswordPrompt();
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NetworkDiagnosticsPanel::pressAutomationScannerEnter()
{
    applyPendingUpdates();
    if (viewMode_ != ViewMode::Scanner) {
        return Result<std::monostate, std::string>::error("Scanner view is not active");
    }
    if (!scannerModeAvailable_) {
        return Result<std::monostate, std::string>::error("Scanner mode is unavailable");
    }
    if (scannerModeActive_) {
        return Result<std::monostate, std::string>::error("Scanner mode is already active");
    }
    if (!scannerEnterButton_) {
        return Result<std::monostate, std::string>::error("Scanner enter is unavailable");
    }

    lv_obj_t* innerButton = getActionButtonInnerButton(scannerEnterButton_);
    if (!innerButton || lv_obj_has_state(innerButton, LV_STATE_DISABLED)) {
        return Result<std::monostate, std::string>::error("Scanner enter is disabled");
    }
    if (!startAsyncScannerEnter()) {
        return Result<std::monostate, std::string>::error("Failed to start scanner mode enter");
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> NetworkDiagnosticsPanel::pressAutomationScannerExit()
{
    applyPendingUpdates();
    if (viewMode_ != ViewMode::Scanner) {
        return Result<std::monostate, std::string>::error("Scanner view is not active");
    }
    if (!scannerModeActive_) {
        return Result<std::monostate, std::string>::error("Scanner mode is not active");
    }
    if (!scannerExitButton_) {
        return Result<std::monostate, std::string>::error("Scanner exit is unavailable");
    }

    lv_obj_t* innerButton = getActionButtonInnerButton(scannerExitButton_);
    if (!innerButton || lv_obj_has_state(innerButton, LV_STATE_DISABLED)) {
        return Result<std::monostate, std::string>::error("Scanner exit is disabled");
    }
    if (!startAsyncScannerExit()) {
        return Result<std::monostate, std::string>::error("Failed to start scanner mode exit");
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

bool NetworkDiagnosticsPanel::isScannerModeActive() const
{
    return scannerModeActive_;
}

bool NetworkDiagnosticsPanel::isScannerModeBusy() const
{
    return scannerActionInProgress_;
}

bool NetworkDiagnosticsPanel::isScannerModeActiveOrBusy()
{
    applyPendingUpdates();
    return scannerModeActive_ || scannerActionInProgress_;
}

Result<std::monostate, std::string> NetworkDiagnosticsPanel::requestScannerExit()
{
    applyPendingUpdates();
    if (!scannerModeActive_) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (scannerActionInProgress_) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (!startAsyncScannerExit()) {
        return Result<std::monostate, std::string>::error("Failed to start scanner mode exit");
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void NetworkDiagnosticsPanel::showLanAccessView()
{
    setViewMode(ViewMode::LanAccess);
}

void NetworkDiagnosticsPanel::showScannerView()
{
    setViewMode(ViewMode::Scanner);
}

void NetworkDiagnosticsPanel::showWifiView()
{
    setViewMode(ViewMode::Wifi);
}

std::optional<size_t> NetworkDiagnosticsPanel::findNetworkIndexBySsid(const std::string& ssid) const
{
    const auto it = std::find_if(
        networks_.begin(), networks_.end(), [&](const Network::WifiNetworkInfo& network) {
            return network.ssid == ssid;
        });
    if (it == networks_.end()) {
        return std::nullopt;
    }

    return static_cast<size_t>(std::distance(networks_.begin(), it));
}

void NetworkDiagnosticsPanel::setViewMode(ViewMode mode)
{
    const ViewMode previousMode = viewMode_;
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
    setVisibility(scannerView_, mode == ViewMode::Scanner);
    viewMode_ = mode;
    if (mode != ViewMode::Wifi) {
        signalHistoryByBssid_.clear();
        signalHistoryLastSampleAt_.reset();
        return;
    }

    if (previousMode != ViewMode::Wifi) {
        signalHistoryLastSampleAt_.reset();
        updateSignalHistory(true);
    }
}

bool NetworkDiagnosticsPanel::hasEventStreamConnection() const
{
    if (!asyncState_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(asyncState_->mutex);
    return asyncState_->eventStreamConnected;
}

void NetworkDiagnosticsPanel::startEventStream()
{
    if (!asyncState_ || eventClient_) {
        return;
    }

    eventClient_ = std::make_shared<Network::WebSocketService>();
    eventClient_->setClientHello(
        Network::ClientHello{
            .protocolVersion = Network::kClientHelloProtocolVersion,
            .wantsRender = false,
            .wantsEvents = true,
        });

    const auto state = asyncState_;
    eventClient_->onConnected([state]() {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->eventStreamConnected = true;
    });
    eventClient_->onDisconnected([state]() {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->eventStreamConnected = false;
        if (state->scanRequestInProgress) {
            state->pendingScanRequest =
                Result<std::monostate, std::string>::error("Network event stream disconnected");
            state->scanRequestInProgress = false;
        }
    });
    eventClient_->onError([state](const std::string& error) {
        LOG_WARN(Controls, "Network event stream error: {}", error);
        std::lock_guard<std::mutex> lock(state->mutex);
        state->eventStreamConnected = false;
        if (state->scanRequestInProgress) {
            state->pendingScanRequest =
                Result<std::monostate, std::string>::error("Network event stream error: " + error);
            state->scanRequestInProgress = false;
        }
    });
    eventClient_->onServerCommand(
        [state](const std::string& messageType, const std::vector<std::byte>& payload) {
            if (messageType != OsApi::NetworkSnapshotChanged::name()) {
                LOG_DEBUG(Controls, "Ignoring os-manager push '{}'", messageType);
                return;
            }

            try {
                const auto changed =
                    Network::deserialize_payload<OsApi::NetworkSnapshotChanged>(payload);
                PendingRefreshData data;
                data.statusResult = Result<Network::WifiStatus, std::string>::okay(
                    toUiWifiStatus(changed.snapshot.status));

                std::vector<Network::WifiNetworkInfo> networks;
                networks.reserve(changed.snapshot.networks.size());
                for (const auto& network : changed.snapshot.networks) {
                    networks.push_back(toUiWifiNetworkInfo(network));
                }
                data.listResult = Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(
                    std::move(networks));

                std::vector<Network::WifiAccessPointInfo> accessPoints;
                accessPoints.reserve(changed.snapshot.accessPoints.size());
                for (const auto& accessPoint : changed.snapshot.accessPoints) {
                    accessPoints.push_back(toUiWifiAccessPointInfo(accessPoint));
                }
                data.accessPoints = std::move(accessPoints);
                data.activeBssid = changed.snapshot.activeBssid;

                std::vector<NetworkInterfaceInfo> localAddresses;
                localAddresses.reserve(changed.snapshot.localAddresses.size());
                for (const auto& info : changed.snapshot.localAddresses) {
                    localAddresses.push_back(toUiLocalAddressInfo(info));
                }
                data.localAddresses = std::move(localAddresses);
                if (changed.snapshot.connectOutcome.has_value()) {
                    data.connectOutcome =
                        toUiWifiConnectOutcome(changed.snapshot.connectOutcome.value());
                }
                if (changed.snapshot.connectProgress.has_value()) {
                    data.connectProgress =
                        toUiWifiConnectProgress(changed.snapshot.connectProgress.value());
                }
                data.lastScanAgeMs = changed.snapshot.lastScanAgeMs;
                data.scanInProgress = changed.snapshot.scanInProgress;

                const auto accessCache = getAccessCache();
                data.accessStatusResult = Result<NetworkAccessStatus, std::string>::okay(
                    NetworkAccessStatus{
                        .webUiEnabled = accessCache.webUiEnabled,
                        .webSocketEnabled = accessCache.webSocketEnabled,
                        .webSocketToken = accessCache.webSocketToken,
                        .scannerModeAvailable = accessCache.scannerModeAvailable,
                        .scannerModeActive = accessCache.scannerModeActive,
                        .scannerModeDetail = accessCache.scannerModeDetail,
                    });

                std::lock_guard<std::mutex> lock(state->mutex);
                state->pendingRefresh = std::move(data);
                state->scanRequestInProgress = false;
            }
            catch (const std::exception& e) {
                LOG_ERROR(Controls, "Failed to deserialize NetworkSnapshotChanged: {}", e.what());
            }
        });

    const auto client = eventClient_;
    std::thread([client]() {
        const auto connectResult = client->connect(OS_MANAGER_ADDRESS, 2000);
        if (connectResult.isError()) {
            LOG_WARN(
                Controls, "Failed to connect network event stream: {}", connectResult.errorValue());
        }
    }).detach();
}

void NetworkDiagnosticsPanel::closePasswordPrompt()
{
    if (passwordOverlay_) {
        lv_obj_del(passwordOverlay_);
        passwordOverlay_ = nullptr;
    }

    passwordCancelButton_ = nullptr;
    connectProgressCancelButton_ = nullptr;
    connectProgressContainer_ = nullptr;
    connectProgressDetailLabel_ = nullptr;
    connectProgressPhaseLabel_ = nullptr;
    connectProgressStagesRow_ = nullptr;
    connectProgressTitleLabel_ = nullptr;
    passwordErrorLabel_ = nullptr;
    passwordFormContainer_ = nullptr;
    passwordJoinButton_ = nullptr;
    passwordKeyboard_ = nullptr;
    passwordTextArea_ = nullptr;
    passwordStatusLabel_ = nullptr;
    passwordVisibilityButton_ = nullptr;
    passwordPromptNetwork_.reset();
    connectOverlayHasPasswordEntry_ = false;
    connectOverlayMode_ = ConnectOverlayMode::PasswordEntry;
    passwordVisible_ = false;
    connectProgressStageBadges_.fill(nullptr);
}

void NetworkDiagnosticsPanel::closeNetworkDetailsOverlay()
{
    if (networkDetailsOverlay_) {
        lv_obj_del(networkDetailsOverlay_);
        networkDetailsOverlay_ = nullptr;
    }

    networkDetailsContent_ = nullptr;
    networkDetailsLastScanValueLabel_ = nullptr;
    networkDetailsNetwork_.reset();
    networkDetailsPlotBindings_.clear();
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
    closeNetworkDetailsOverlay();
    closePasswordPrompt();

    passwordPromptNetwork_ = network;
    connectOverlayHasPasswordEntry_ = true;
    connectOverlayMode_ = ConnectOverlayMode::PasswordEntry;
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
    lv_obj_set_style_pad_all(modal, NETWORK_CARD_PADDING, 0);
    lv_obj_set_style_pad_row(modal, NETWORK_CARD_ROW_PADDING, 0);
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
    const std::string titleText = passwordPromptTitleText(network);
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

    passwordFormContainer_ = lv_obj_create(modal);
    lv_obj_set_size(passwordFormContainer_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(passwordFormContainer_, 1);
    lv_obj_set_flex_flow(passwordFormContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        passwordFormContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(passwordFormContainer_, 0, 0);
    lv_obj_set_style_pad_row(passwordFormContainer_, 8, 0);
    lv_obj_set_style_bg_opa(passwordFormContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passwordFormContainer_, 0, 0);
    lv_obj_clear_flag(passwordFormContainer_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* passwordRow = lv_obj_create(passwordFormContainer_);
    lv_obj_set_size(passwordRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(passwordRow, 8, 0);
    lv_obj_set_style_pad_column(passwordRow, 8, 0);
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
    lv_obj_set_size(passwordTextArea_, 0, NETWORK_TEXT_INPUT_HEIGHT);
    lv_obj_set_flex_grow(passwordTextArea_, 1);
    lv_textarea_set_one_line(passwordTextArea_, true);
    lv_textarea_set_max_length(passwordTextArea_, 64);
    lv_textarea_set_password_mode(passwordTextArea_, true);
    lv_textarea_set_password_show_time(passwordTextArea_, 0);
    lv_textarea_set_placeholder_text(passwordTextArea_, "Password");
    lv_obj_set_style_bg_opa(passwordTextArea_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(passwordTextArea_, 0, 0);
    lv_obj_set_style_outline_width(passwordTextArea_, 0, 0);
    lv_obj_set_style_pad_hor(passwordTextArea_, NETWORK_TEXT_INPUT_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(passwordTextArea_, NETWORK_TEXT_INPUT_PAD_VER, 0);
    lv_obj_set_style_text_color(passwordTextArea_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(passwordTextArea_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(
        passwordTextArea_, lv_color_hex(MUTED_TEXT_COLOR), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(passwordTextArea_, lv_color_hex(0x00CED1), LV_PART_CURSOR);
    lv_obj_add_event_cb(passwordTextArea_, onPasswordTextAreaEvent, LV_EVENT_ALL, this);

    passwordVisibilityButton_ = LVGLBuilder::actionButton(passwordRow)
                                    .text("Show")
                                    .mode(LVGLBuilder::ActionMode::Push)
                                    .width(96)
                                    .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                    .callback(onPasswordVisibilityClicked, this)
                                    .buildOrLog();

    passwordJoinButton_ = LVGLBuilder::actionButton(passwordRow)
                              .text(passwordPromptSubmitText(network))
                              .mode(LVGLBuilder::ActionMode::Push)
                              .width(NETWORK_OVERLAY_BUTTON_WIDTH)
                              .height(NETWORK_ACTION_BUTTON_HEIGHT)
                              .callback(onPasswordJoinClicked, this)
                              .buildOrLog();

    passwordCancelButton_ = LVGLBuilder::actionButton(passwordRow)
                                .text("Cancel")
                                .mode(LVGLBuilder::ActionMode::Push)
                                .width(NETWORK_OVERLAY_BUTTON_WIDTH)
                                .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                .callback(onPasswordCancelClicked, this)
                                .buildOrLog();

    passwordErrorLabel_ = lv_label_create(passwordFormContainer_);
    lv_obj_set_width(passwordErrorLabel_, LV_PCT(100));
    lv_label_set_long_mode(passwordErrorLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(passwordErrorLabel_, lv_color_hex(ERROR_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(passwordErrorLabel_, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(passwordErrorLabel_, LV_OBJ_FLAG_HIDDEN);

    passwordStatusLabel_ = lv_label_create(passwordFormContainer_);
    lv_obj_set_width(passwordStatusLabel_, LV_PCT(100));
    lv_label_set_long_mode(passwordStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(passwordStatusLabel_, lv_color_hex(0x00CED1), 0);
    lv_obj_set_style_text_font(passwordStatusLabel_, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(passwordStatusLabel_, LV_OBJ_FLAG_HIDDEN);

    passwordKeyboard_ = lv_keyboard_create(passwordFormContainer_);
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
    const auto keyboardPressedSelector = static_cast<lv_style_selector_t>(LV_PART_ITEMS)
        | static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(
        passwordKeyboard_, lv_color_hex(CONNECT_STAGE_ACTIVE_BG_COLOR), keyboardPressedSelector);
    lv_obj_set_style_border_color(
        passwordKeyboard_, lv_color_hex(CONNECT_STAGE_ACTIVE_BG_COLOR), keyboardPressedSelector);
    lv_obj_set_style_text_color(
        passwordKeyboard_, lv_color_hex(CONNECT_STAGE_ACTIVE_TEXT_COLOR), keyboardPressedSelector);
    lv_obj_set_style_shadow_width(passwordKeyboard_, 8, keyboardPressedSelector);
    lv_obj_set_style_shadow_color(
        passwordKeyboard_, lv_color_hex(CONNECT_STAGE_ACTIVE_BG_COLOR), keyboardPressedSelector);
    lv_obj_set_style_shadow_opa(passwordKeyboard_, LV_OPA_30, keyboardPressedSelector);
    lv_obj_add_event_cb(passwordKeyboard_, onPasswordKeyboardEvent, LV_EVENT_ALL, this);
    lv_keyboard_set_textarea(passwordKeyboard_, passwordTextArea_);
    lv_keyboard_set_popovers(passwordKeyboard_, true);

    connectProgressContainer_ = lv_obj_create(modal);
    lv_obj_set_size(connectProgressContainer_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(connectProgressContainer_, 1);
    lv_obj_set_flex_flow(connectProgressContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        connectProgressContainer_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(connectProgressContainer_, 20, 0);
    lv_obj_set_style_pad_row(connectProgressContainer_, 16, 0);
    lv_obj_set_style_bg_opa(connectProgressContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(connectProgressContainer_, 0, 0);
    lv_obj_clear_flag(connectProgressContainer_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* connectProgressIconLabel = lv_label_create(connectProgressContainer_);
    lv_label_set_text(connectProgressIconLabel, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(connectProgressIconLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(connectProgressIconLabel, lv_color_hex(0xFFDD66), 0);

    connectProgressTitleLabel_ = lv_label_create(connectProgressContainer_);
    lv_obj_set_width(connectProgressTitleLabel_, LV_PCT(100));
    lv_label_set_long_mode(connectProgressTitleLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(connectProgressTitleLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(connectProgressTitleLabel_, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(connectProgressTitleLabel_, lv_color_hex(0xFFFFFF), 0);

    connectProgressPhaseLabel_ = lv_label_create(connectProgressContainer_);
    lv_obj_set_width(connectProgressPhaseLabel_, LV_PCT(100));
    lv_label_set_long_mode(connectProgressPhaseLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(connectProgressPhaseLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(connectProgressPhaseLabel_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(connectProgressPhaseLabel_, lv_color_hex(0x00CED1), 0);

    connectProgressStagesRow_ = lv_obj_create(connectProgressContainer_);
    lv_obj_set_size(connectProgressStagesRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(connectProgressStagesRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        connectProgressStagesRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(connectProgressStagesRow_, 0, 0);
    lv_obj_set_style_pad_column(connectProgressStagesRow_, 8, 0);
    lv_obj_set_style_bg_opa(connectProgressStagesRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(connectProgressStagesRow_, 0, 0);
    lv_obj_clear_flag(connectProgressStagesRow_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < connectProgressStageBadges_.size(); ++i) {
        lv_obj_t* badge = lv_obj_create(connectProgressStagesRow_);
        lv_obj_set_size(badge, 0, NETWORK_STAGE_BADGE_HEIGHT);
        lv_obj_set_flex_grow(badge, 1);
        lv_obj_set_style_pad_hor(badge, 8, 0);
        lv_obj_set_style_pad_ver(badge, 6, 0);
        lv_obj_set_style_radius(badge, 999, 0);
        lv_obj_set_style_border_width(badge, 1, 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(CONNECT_STAGE_PENDING_BG_COLOR), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* badgeLabel = lv_label_create(badge);
        lv_label_set_text(badgeLabel, kConnectStageTitles[i]);
        lv_obj_center(badgeLabel);
        lv_obj_set_style_text_font(badgeLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(CONNECT_STAGE_PENDING_TEXT_COLOR), 0);

        connectProgressStageBadges_[i] = badge;
    }

    connectProgressDetailLabel_ = lv_label_create(connectProgressContainer_);
    lv_obj_set_width(connectProgressDetailLabel_, LV_PCT(100));
    lv_label_set_long_mode(connectProgressDetailLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(connectProgressDetailLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(connectProgressDetailLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(connectProgressDetailLabel_, lv_color_hex(MUTED_TEXT_COLOR), 0);

    connectProgressCancelButton_ = LVGLBuilder::actionButton(connectProgressContainer_)
                                       .text("Cancel")
                                       .mode(LVGLBuilder::ActionMode::Push)
                                       .width(NETWORK_PROGRESS_CANCEL_BUTTON_WIDTH)
                                       .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                       .callback(onConnectProgressCancelClicked, this)
                                       .buildOrLog();

    updatePasswordVisibilityButton();
    updatePasswordJoinButton();
    setPasswordPromptError("");
    setPasswordPromptStatus("", MUTED_TEXT_COLOR);
    setConnectOverlayMode(ConnectOverlayMode::PasswordEntry);
}

void NetworkDiagnosticsPanel::openNetworkDetailsOverlay(const Network::WifiNetworkInfo& network)
{
    closeNetworkDetailsOverlay();

    networkDetailsNetwork_ = network;
    networkDetailsPlotBindings_.clear();

    std::vector<Network::WifiAccessPointInfo> matchingAccessPoints;
    for (const auto& accessPoint : accessPoints_) {
        if (accessPoint.ssid == network.ssid) {
            matchingAccessPoints.push_back(accessPoint);
        }
    }
    std::optional<Network::WifiAccessPointInfo> strongestObservedAccessPoint;
    for (const auto& accessPoint : matchingAccessPoints) {
        if (!strongestObservedAccessPoint.has_value()) {
            strongestObservedAccessPoint = accessPoint;
            continue;
        }

        const int accessPointSignal =
            accessPoint.signalDbm.has_value() ? accessPoint.signalDbm.value() : -200;
        const int strongestSignal = strongestObservedAccessPoint->signalDbm.has_value()
            ? strongestObservedAccessPoint->signalDbm.value()
            : -200;
        if (accessPointSignal > strongestSignal
            || (accessPointSignal == strongestSignal
                && accessPoint.bssid < strongestObservedAccessPoint->bssid)) {
            strongestObservedAccessPoint = accessPoint;
        }
    }
    std::stable_sort(
        matchingAccessPoints.begin(),
        matchingAccessPoints.end(),
        [](const Network::WifiAccessPointInfo& lhs, const Network::WifiAccessPointInfo& rhs) {
            if (lhs.active != rhs.active) {
                return lhs.active;
            }

            const int lhsSignal = lhs.signalDbm.has_value() ? lhs.signalDbm.value() : -200;
            const int rhsSignal = rhs.signalDbm.has_value() ? rhs.signalDbm.value() : -200;
            if (lhsSignal != rhsSignal) {
                return lhsSignal > rhsSignal;
            }

            return lhs.bssid < rhs.bssid;
        });

    networkDetailsOverlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(networkDetailsOverlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(networkDetailsOverlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(networkDetailsOverlay_, LV_OPA_60, 0);
    lv_obj_set_style_border_width(networkDetailsOverlay_, 0, 0);
    lv_obj_set_style_pad_all(networkDetailsOverlay_, 0, 0);
    lv_obj_set_style_radius(networkDetailsOverlay_, 0, 0);
    lv_obj_clear_flag(networkDetailsOverlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(networkDetailsOverlay_);

    lv_obj_t* modal = lv_obj_create(networkDetailsOverlay_);
    lv_obj_set_size(modal, LV_PCT(100), LV_PCT(100));
    lv_obj_align(modal, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, NETWORK_CARD_PADDING, 0);
    lv_obj_set_style_pad_row(modal, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* headerRow = lv_obj_create(modal);
    lv_obj_set_size(headerRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        headerRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(headerRow, 0, 0);
    lv_obj_set_style_pad_column(headerRow, 12, 0);
    lv_obj_set_style_bg_opa(headerRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(headerRow, 0, 0);
    lv_obj_clear_flag(headerRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(headerRow);
    lv_label_set_text(titleLabel, network.ssid.c_str());
    lv_obj_set_flex_grow(titleLabel, 1);
    lv_obj_set_width(titleLabel, 0);
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFDD66), 0);

    lv_obj_t* statusBadge = lv_label_create(headerRow);
    std::string badgeText;
    if (network.status == Network::WifiNetworkStatus::Connected) {
        badgeText = "Connected";
    }
    else if (isSavedNetwork(network)) {
        badgeText = "Saved";
    }
    else if (network.status == Network::WifiNetworkStatus::Open) {
        badgeText = "Open";
    }
    else {
        badgeText = "Available";
    }
    lv_label_set_text(statusBadge, badgeText.c_str());
    lv_obj_set_style_text_font(statusBadge, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(statusBadge, lv_color_hex(MUTED_TEXT_COLOR), 0);

    lv_obj_t* bodyRow = lv_obj_create(modal);
    lv_obj_set_size(bodyRow, LV_PCT(100), 0);
    lv_obj_set_flex_grow(bodyRow, 1);
    lv_obj_set_flex_flow(bodyRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bodyRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(bodyRow, 0, 0);
    lv_obj_set_style_pad_column(bodyRow, NETWORK_CARD_PADDING, 0);
    lv_obj_set_style_bg_opa(bodyRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bodyRow, 0, 0);
    lv_obj_clear_flag(bodyRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* summaryColumn = lv_obj_create(bodyRow);
    lv_obj_set_size(summaryColumn, NETWORK_DETAILS_SUMMARY_WIDTH, LV_PCT(100));
    lv_obj_set_flex_flow(summaryColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        summaryColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(summaryColumn, 0, 0);
    lv_obj_set_style_pad_row(summaryColumn, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_bg_opa(summaryColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summaryColumn, 0, 0);
    lv_obj_clear_flag(summaryColumn, LV_OBJ_FLAG_SCROLLABLE);

    networkDetailsContent_ = lv_obj_create(bodyRow);
    lv_obj_set_size(networkDetailsContent_, 0, LV_PCT(100));
    lv_obj_set_flex_grow(networkDetailsContent_, 1);
    lv_obj_set_flex_flow(networkDetailsContent_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        networkDetailsContent_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(networkDetailsContent_, 0, 0);
    lv_obj_set_style_pad_row(networkDetailsContent_, NETWORK_RADIOS_CONTENT_ROW_PADDING, 0);
    lv_obj_set_style_bg_opa(networkDetailsContent_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(networkDetailsContent_, 0, 0);
    lv_obj_set_scroll_dir(networkDetailsContent_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(networkDetailsContent_, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* summaryCard = lv_obj_create(summaryColumn);
    lv_obj_set_size(summaryCard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(summaryCard, 10, 0);
    lv_obj_set_style_pad_row(summaryCard, 8, 0);
    lv_obj_set_style_bg_color(summaryCard, lv_color_hex(ACCESSORY_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(summaryCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(summaryCard, 1, 0);
    lv_obj_set_style_border_color(summaryCard, lv_color_hex(ACCESSORY_BORDER_COLOR), 0);
    lv_obj_set_style_radius(summaryCard, 10, 0);
    lv_obj_set_flex_flow(summaryCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        summaryCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(summaryCard, LV_OBJ_FLAG_SCROLLABLE);

    auto addDetailRow =
        [&](lv_obj_t* parent, const char* labelText, const std::string& valueText) -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text(label, labelText);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(HEADER_TEXT_COLOR), 0);
        lv_obj_set_style_min_width(label, 88, 0);

        lv_obj_t* value = lv_label_create(row);
        lv_label_set_text(value, valueText.c_str());
        lv_obj_set_flex_grow(value, 1);
        lv_obj_set_width(value, 0);
        lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(0xFFFFFF), 0);

        return value;
    };

    auto addBssidRow =
        [&](lv_obj_t* parent, const char* labelText, const std::string& valueText) -> lv_obj_t* {
        lv_obj_t* value = addDetailRow(parent, labelText, valueText);
        if (!value) {
            return nullptr;
        }

#if LV_FONT_UNSCII_16
        lv_obj_set_style_text_font(value, &lv_font_unscii_16, 0);
#endif
        return value;
    };

    const auto preferredAccessPoint = preferredAccessPointForSsid(network.ssid);
    std::optional<std::string> detailActiveBssid;
    if (preferredAccessPoint.has_value() && preferredAccessPoint->active
        && !preferredAccessPoint->bssid.empty()) {
        detailActiveBssid = preferredAccessPoint->bssid;
    }
    else if (
        latestWifiStatus_.has_value() && latestWifiStatus_->connected
        && latestWifiStatus_->ssid == network.ssid && activeBssid_.has_value()) {
        detailActiveBssid = activeBssid_;
    }

    std::optional<Network::WifiAccessPointInfo> detailActiveAccessPoint;
    if (detailActiveBssid.has_value()) {
        const auto activeIt = std::find_if(
            matchingAccessPoints.begin(),
            matchingAccessPoints.end(),
            [&](const Network::WifiAccessPointInfo& accessPoint) {
                return accessPoint.bssid == detailActiveBssid.value();
            });
        if (activeIt != matchingAccessPoints.end()) {
            detailActiveAccessPoint = *activeIt;
        }
    }
    if (!detailActiveAccessPoint.has_value()) {
        const auto activeIt = std::find_if(
            matchingAccessPoints.begin(),
            matchingAccessPoints.end(),
            [](const Network::WifiAccessPointInfo& accessPoint) { return accessPoint.active; });
        if (activeIt != matchingAccessPoints.end()) {
            detailActiveAccessPoint = *activeIt;
        }
    }

    std::optional<int> detailSignal;
    if (network.status == Network::WifiNetworkStatus::Connected
        && detailActiveAccessPoint.has_value() && detailActiveAccessPoint->signalDbm.has_value()) {
        detailSignal = detailActiveAccessPoint->signalDbm;
    }
    else if (network.signalDbm.has_value()) {
        detailSignal = network.signalDbm;
    }
    else if (!matchingAccessPoints.empty()) {
        detailSignal = matchingAccessPoints.front().signalDbm;
    }
    addDetailRow(
        summaryCard,
        "Signal",
        detailSignal.has_value() ? std::to_string(detailSignal.value()) + " dBm" : "unknown");
    addDetailRow(summaryCard, "Security", network.security.empty() ? "unknown" : network.security);
    networkDetailsLastScanValueLabel_ =
        addDetailRow(summaryCard, "Last scan", formatLastScanAgeText());
    if (network.status == Network::WifiNetworkStatus::Connected) {
        addDetailRow(summaryCard, "Connected", "now");
    }
    else if (isSavedNetwork(network)) {
        addDetailRow(
            summaryCard,
            "Last used",
            network.lastUsedRelative.empty() ? std::string("saved") : network.lastUsedRelative);
    }

    if (detailActiveBssid.has_value()) {
        addBssidRow(summaryCard, "Active radio", detailActiveBssid.value());
    }
    if (strongestObservedAccessPoint.has_value() && !strongestObservedAccessPoint->bssid.empty()) {
        const bool strongestDiffers = !detailActiveBssid.has_value()
            || strongestObservedAccessPoint->bssid != detailActiveBssid.value();
        if (strongestDiffers) {
            addBssidRow(summaryCard, "Strongest seen", strongestObservedAccessPoint->bssid);
        }
    }

    if (network.status == Network::WifiNetworkStatus::Connected && !localAddresses_.empty()) {
        addDetailRow(summaryCard, "Addresses", formatAddressSummaryMultiline());
    }

    lv_obj_t* summarySpacer = lv_obj_create(summaryColumn);
    lv_obj_set_size(summarySpacer, LV_PCT(100), 0);
    lv_obj_set_flex_grow(summarySpacer, 1);
    lv_obj_set_style_pad_all(summarySpacer, 0, 0);
    lv_obj_set_style_bg_opa(summarySpacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summarySpacer, 0, 0);
    lv_obj_clear_flag(summarySpacer, LV_OBJ_FLAG_SCROLLABLE);

    const bool actionsEnabled = !isActionInProgress();
    const int summaryButtonWidth = NETWORK_DETAILS_SUMMARY_WIDTH;
    const int summaryHalfButtonWidth = (NETWORK_DETAILS_SUMMARY_WIDTH - 8) / 2;

    lv_obj_t* summaryActionsColumn = lv_obj_create(summaryColumn);
    lv_obj_set_size(summaryActionsColumn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(summaryActionsColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        summaryActionsColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(summaryActionsColumn, 0, 0);
    lv_obj_set_style_pad_row(summaryActionsColumn, 8, 0);
    lv_obj_set_style_bg_opa(summaryActionsColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summaryActionsColumn, 0, 0);
    lv_obj_clear_flag(summaryActionsColumn, LV_OBJ_FLAG_SCROLLABLE);

    if (isSavedNetwork(network) && !isOpenSecurity(network.security)) {
        lv_obj_t* updatePasswordButton = LVGLBuilder::actionButton(summaryActionsColumn)
                                             .text("Update Password")
                                             .mode(LVGLBuilder::ActionMode::Push)
                                             .width(summaryButtonWidth)
                                             .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                             .callback(onNetworkDetailsUpdatePasswordClicked, this)
                                             .buildOrLog();
        setActionButtonEnabled(updatePasswordButton, actionsEnabled);
    }

    lv_obj_t* summaryActionRow = lv_obj_create(summaryActionsColumn);
    lv_obj_set_size(summaryActionRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(summaryActionRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        summaryActionRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(summaryActionRow, 0, 0);
    lv_obj_set_style_pad_column(summaryActionRow, 8, 0);
    lv_obj_set_style_bg_opa(summaryActionRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summaryActionRow, 0, 0);
    lv_obj_clear_flag(summaryActionRow, LV_OBJ_FLAG_SCROLLABLE);

    if (isSavedNetwork(network)) {
        lv_obj_t* forgetButton = LVGLBuilder::actionButton(summaryActionRow)
                                     .text("Forget")
                                     .mode(LVGLBuilder::ActionMode::Push)
                                     .width(summaryHalfButtonWidth)
                                     .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                     .callback(onNetworkDetailsForgetClicked, this)
                                     .buildOrLog();
        setActionButtonEnabled(forgetButton, actionsEnabled);
    }

    LVGLBuilder::actionButton(summaryActionRow)
        .text("Close")
        .mode(LVGLBuilder::ActionMode::Push)
        .width(isSavedNetwork(network) ? summaryHalfButtonWidth : summaryButtonWidth)
        .height(NETWORK_ACTION_BUTTON_HEIGHT)
        .callback(onNetworkDetailsCloseClicked, this)
        .buildOrLog();

    lv_obj_t* radiosTitleLabel = lv_label_create(networkDetailsContent_);
    lv_label_set_text(radiosTitleLabel, "Observed radios");
    lv_obj_set_width(radiosTitleLabel, LV_PCT(100));
    lv_obj_set_style_text_font(radiosTitleLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(radiosTitleLabel, lv_color_hex(HEADER_TEXT_COLOR), 0);

    if (matchingAccessPoints.empty()) {
        lv_obj_t* emptyLabel = lv_label_create(networkDetailsContent_);
        lv_label_set_text(emptyLabel, "No radios in the current scan.");
        lv_obj_set_width(emptyLabel, LV_PCT(100));
        lv_label_set_long_mode(emptyLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(MUTED_TEXT_COLOR), 0);
    }
    else {
        for (const auto& accessPoint : matchingAccessPoints) {
            lv_obj_t* accessPointCard = lv_obj_create(networkDetailsContent_);
            lv_obj_set_size(accessPointCard, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(accessPointCard, NETWORK_RADIO_CARD_PADDING, 0);
            lv_obj_set_style_pad_row(accessPointCard, NETWORK_RADIO_CARD_ROW_PADDING, 0);
            lv_obj_set_style_bg_color(
                accessPointCard,
                lv_color_hex(accessPoint.active ? 0x202632 : ACCESSORY_BG_COLOR),
                0);
            lv_obj_set_style_bg_opa(accessPointCard, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(accessPointCard, accessPoint.active ? 2 : 1, 0);
            lv_obj_set_style_border_color(
                accessPointCard,
                lv_color_hex(
                    accessPoint.active ? CONNECT_STAGE_ACTIVE_BORDER_COLOR
                                       : ACCESSORY_BORDER_COLOR),
                0);
            lv_obj_set_style_radius(accessPointCard, 10, 0);
            lv_obj_set_flex_flow(accessPointCard, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(
                accessPointCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_clear_flag(accessPointCard, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* apHeaderRow = lv_obj_create(accessPointCard);
            lv_obj_set_size(apHeaderRow, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(apHeaderRow, 0, 0);
            lv_obj_set_style_pad_column(apHeaderRow, 8, 0);
            lv_obj_set_style_bg_opa(apHeaderRow, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(apHeaderRow, 0, 0);
            lv_obj_set_flex_flow(apHeaderRow, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                apHeaderRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(apHeaderRow, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* bssidLabel = lv_label_create(apHeaderRow);
            const std::string bssidText =
                accessPoint.bssid.empty() ? std::string("BSSID unavailable") : accessPoint.bssid;
            lv_label_set_text(bssidLabel, bssidText.c_str());
            lv_obj_set_flex_grow(bssidLabel, 1);
            lv_obj_set_width(bssidLabel, 0);
            lv_label_set_long_mode(bssidLabel, LV_LABEL_LONG_DOT);
#if LV_FONT_UNSCII_16
            lv_obj_set_style_text_font(bssidLabel, &lv_font_unscii_16, 0);
#else
            lv_obj_set_style_text_font(bssidLabel, &lv_font_montserrat_14, 0);
#endif
            lv_obj_set_style_text_color(bssidLabel, lv_color_hex(0xFFFFFF), 0);

            if (accessPoint.active) {
                lv_obj_t* activeLabel = lv_label_create(apHeaderRow);
                lv_label_set_text(activeLabel, "Active");
                lv_obj_set_style_text_font(activeLabel, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(activeLabel, lv_color_hex(0x00FF7F), 0);
            }

            std::vector<std::string> accessPointParts;
            if (accessPoint.signalDbm.has_value()) {
                accessPointParts.push_back(std::to_string(accessPoint.signalDbm.value()) + " dBm");
            }
            if (accessPoint.channel.has_value()) {
                accessPointParts.push_back("ch " + std::to_string(accessPoint.channel.value()));
            }
            const std::string bandText = wifiBandLabel(accessPoint.frequencyMhz);
            if (!bandText.empty()) {
                accessPointParts.push_back(bandText);
            }
            else if (accessPoint.frequencyMhz.has_value()) {
                accessPointParts.push_back(
                    std::to_string(accessPoint.frequencyMhz.value()) + " MHz");
            }
            if (!accessPoint.security.empty()) {
                accessPointParts.push_back(accessPoint.security);
            }

            lv_obj_t* apDetailsLabel = lv_label_create(accessPointCard);
            const std::string accessPointDetails = joinTextParts(accessPointParts, " • ");
            lv_label_set_text(apDetailsLabel, accessPointDetails.c_str());
            lv_obj_set_width(apDetailsLabel, LV_PCT(100));
            lv_label_set_long_mode(apDetailsLabel, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(apDetailsLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(apDetailsLabel, lv_color_hex(MUTED_TEXT_COLOR), 0);

            auto historyIt = signalHistoryByBssid_.find(accessPoint.bssid);
            if (historyIt != signalHistoryByBssid_.end() && !historyIt->second.samples.empty()) {
                auto plot = std::make_unique<TimeSeriesPlotWidget>(
                    accessPointCard,
                    TimeSeriesPlotWidget::Config{
                        .title = "",
                        .lineColor =
                            accessPoint.active ? lv_color_hex(0xFFDD66) : lv_color_hex(0x00CED1),
                        .defaultMinY = -90.0f,
                        .defaultMaxY = -20.0f,
                        .valueScale = 1.0f,
                        .autoScaleY = false,
                        .showTitle = false,
                        .showYAxisRangeLabels = false,
                    });
                plot->setSamples(historyIt->second.samples);
                plot->clearBottomLabels();
                lv_obj_t* plotContainer = plot->getContainer();
                lv_obj_set_size(plotContainer, LV_PCT(100), NETWORK_RADIO_PLOT_HEIGHT);
                lv_obj_set_flex_grow(plotContainer, 0);
                networkDetailsPlotBindings_.push_back(
                    NetworkDetailsPlotBinding{
                        .bssid = accessPoint.bssid,
                        .plot = std::move(plot),
                    });
            }
        }
    }
}

void NetworkDiagnosticsPanel::openConnectingOverlay(const Network::WifiNetworkInfo& network)
{
    openPasswordPrompt(network);
    connectOverlayHasPasswordEntry_ = false;
    setConnectOverlayMode(ConnectOverlayMode::Connecting);
    updateConnectOverlay();
}

void NetworkDiagnosticsPanel::refresh(bool forceRefresh)
{
    if (viewMode_ == ViewMode::Scanner) {
        setLoadingState();
        startAsyncRefresh(false);
        return;
    }

    if (forceRefresh && hasEventStreamConnection()) {
        scanInProgress_ = true;
        setWifiStatusMessage("Scanning nearby networks...", MUTED_TEXT_COLOR);
        setRefreshButtonEnabled(false);
        startAsyncScanRequest();
        return;
    }

    setLoadingState();
    startAsyncRefresh(forceRefresh);
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

    const bool isDisconnecting = actionState_.kind == AsyncActionKind::Disconnect
        && connectedNetwork->ssid == actionState_.ssid;
    const bool isForgetting =
        actionState_.kind == AsyncActionKind::Forget && connectedNetwork->ssid == actionState_.ssid;
    const bool actionsDisabled = isActionInProgress();

    auto detailsContext = std::make_unique<NetworkDetailsContext>();
    detailsContext->panel = this;
    detailsContext->index = connectedIndex;
    networkDetailsContexts_.push_back(std::move(detailsContext));
    NetworkDetailsContext* detailsContextPtr = networkDetailsContexts_.back().get();

    lv_obj_t* summaryTapArea = lv_obj_create(currentNetworkContainer_);
    lv_obj_set_size(summaryTapArea, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(summaryTapArea, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        summaryTapArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(summaryTapArea, 6, 0);
    lv_obj_set_style_pad_row(summaryTapArea, 4, 0);
    lv_obj_set_style_bg_opa(summaryTapArea, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summaryTapArea, 0, 0);
    lv_obj_set_style_radius(summaryTapArea, 8, 0);
    lv_obj_set_style_bg_color(summaryTapArea, lv_color_hex(0x262626), LV_STATE_PRESSED);
    lv_obj_clear_flag(summaryTapArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(summaryTapArea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        summaryTapArea, onNetworkDetailsClicked, LV_EVENT_CLICKED, detailsContextPtr);

    lv_obj_t* summaryHeaderRow = lv_obj_create(summaryTapArea);
    lv_obj_set_size(summaryHeaderRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(summaryHeaderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        summaryHeaderRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(summaryHeaderRow, 0, 0);
    lv_obj_set_style_pad_column(summaryHeaderRow, 8, 0);
    lv_obj_set_style_bg_opa(summaryHeaderRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(summaryHeaderRow, 0, 0);
    lv_obj_clear_flag(summaryHeaderRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(summaryHeaderRow);
    const std::string titleText = "Connected to " + connectedNetwork->ssid;
    lv_label_set_text(titleLabel, titleText.c_str());
    lv_obj_set_flex_grow(titleLabel, 1);
    lv_obj_set_width(titleLabel, 0);
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x00FF7F), 0);

    lv_obj_t* chevronLabel = lv_label_create(summaryHeaderRow);
    lv_label_set_text(chevronLabel, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chevronLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(chevronLabel, lv_color_hex(HEADER_TEXT_COLOR), 0);
    lv_obj_set_style_text_color(
        chevronLabel, lv_color_hex(CONNECT_STAGE_ACTIVE_BG_COLOR), LV_STATE_PRESSED);

    lv_obj_t* detailsLabel = lv_label_create(summaryTapArea);
    const std::string detailsText = formatCurrentConnectionDetails(*connectedNetwork);
    lv_label_set_text(detailsLabel, detailsText.c_str());
    lv_obj_set_width(detailsLabel, LV_PCT(100));
    lv_label_set_long_mode(detailsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(detailsLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(detailsLabel, lv_color_hex(MUTED_TEXT_COLOR), 0);

    if (!connectedNetwork->connectionId.empty()) {
        auto disconnectContext = std::make_unique<DisconnectContext>();
        disconnectContext->panel = this;
        disconnectContext->index = connectedIndex;
        disconnectContexts_.push_back(std::move(disconnectContext));
        DisconnectContext* disconnectContextPtr = disconnectContexts_.back().get();

        auto forgetContext = std::make_unique<ForgetContext>();
        forgetContext->panel = this;
        forgetContext->index = connectedIndex;
        forgetContexts_.push_back(std::move(forgetContext));
        ForgetContext* forgetContextPtr = forgetContexts_.back().get();

        lv_obj_t* actionRow = lv_obj_create(currentNetworkContainer_);
        lv_obj_set_size(actionRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(actionRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            actionRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(actionRow, 0, 0);
        lv_obj_set_style_pad_column(actionRow, 8, 0);
        lv_obj_set_style_bg_opa(actionRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(actionRow, 0, 0);
        lv_obj_clear_flag(actionRow, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* disconnectButton = LVGLBuilder::actionButton(actionRow)
                                         .text(isDisconnecting ? "Disconnecting" : "Disconnect")
                                         .mode(LVGLBuilder::ActionMode::Push)
                                         .width(NETWORK_SUMMARY_BUTTON_WIDTH)
                                         .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                         .callback(onDisconnectClicked, disconnectContextPtr)
                                         .buildOrLog();
        setActionButtonEnabled(disconnectButton, !actionsDisabled);

        lv_obj_t* forgetButton = LVGLBuilder::actionButton(actionRow)
                                     .text(isForgetting ? "Forgetting" : "Forget")
                                     .mode(LVGLBuilder::ActionMode::Push)
                                     .width(NETWORK_SUMMARY_BUTTON_WIDTH)
                                     .height(NETWORK_ACTION_BUTTON_HEIGHT)
                                     .callback(onForgetClicked, forgetContextPtr)
                                     .buildOrLog();
        setActionButtonEnabled(forgetButton, !actionsDisabled);
    }
}

void NetworkDiagnosticsPanel::setLoadingState()
{
    scanInProgress_ = true;
    setWifiStatusMessage("Scanning nearby networks...", MUTED_TEXT_COLOR);
    if (scannerStatusLabel_) {
        lv_label_set_text(scannerStatusLabel_, "Refreshing scanner status...");
    }

    if (networksContainer_) {
        lv_obj_clean(networksContainer_);
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, "Scanning nearby networks...");
        lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }

    setRefreshButtonEnabled(false);
    setScannerRefreshButtonEnabled(false);
}

std::string NetworkDiagnosticsPanel::formatConnectPhaseText() const
{
    if ((connectAwaitingConfirmationSsid_.has_value() && !connectProgress_.has_value())
        || isConnectFinalizing(connectProgress_)) {
        return "Finalizing connection";
    }

    const auto phase = connectProgress_.has_value() ? connectProgress_->phase
                                                    : Network::WifiConnectPhase::Starting;
    switch (phase) {
        case Network::WifiConnectPhase::Starting:
            return "Joining network";
        case Network::WifiConnectPhase::Associating:
            return "Associating with network";
        case Network::WifiConnectPhase::Authenticating:
            return "Checking password";
        case Network::WifiConnectPhase::GettingAddress:
            return "Getting address";
        case Network::WifiConnectPhase::Canceling:
            return "Canceling connection";
    }

    return "Joining network";
}

std::string NetworkDiagnosticsPanel::formatAnimatedConnectPhaseText() const
{
    const std::string baseText = formatConnectPhaseText();
    if (!connectStartedAt_.has_value()) {
        return baseText + "...";
    }

    const auto elapsed = std::chrono::steady_clock::now() - connectStartedAt_.value();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const int dotCount = static_cast<int>((elapsedMs / 450) % 3) + 1;
    return baseText + std::string(static_cast<size_t>(dotCount), '.');
}

std::string NetworkDiagnosticsPanel::formatConnectElapsedText() const
{
    if (!connectStartedAt_.has_value()) {
        return "";
    }

    const auto elapsed = std::chrono::steady_clock::now() - connectStartedAt_.value();
    const auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    return "Elapsed " + std::to_string(elapsedSeconds) + "s";
}

std::string NetworkDiagnosticsPanel::formatConnectStatusMessage() const
{
    const std::string ssid = connectAwaitingConfirmationSsid_.has_value()
        ? connectAwaitingConfirmationSsid_.value()
        : (connectProgress_.has_value()
               ? connectProgress_->ssid
               : (actionState_.ssid.empty() ? std::string{} : actionState_.ssid));
    if (ssid.empty()) {
        return formatAnimatedConnectPhaseText();
    }

    return "Connecting to " + ssid + " - " + formatAnimatedConnectPhaseText();
}

bool NetworkDiagnosticsPanel::isConnectedToSsid(const std::string& ssid) const
{
    return latestWifiStatus_.has_value() && latestWifiStatus_->connected
        && latestWifiStatus_->ssid == ssid;
}

void NetworkDiagnosticsPanel::setConnectOverlayMode(const ConnectOverlayMode mode)
{
    connectOverlayMode_ = mode;

    if (passwordFormContainer_) {
        if (mode == ConnectOverlayMode::PasswordEntry && connectOverlayHasPasswordEntry_) {
            lv_obj_clear_flag(passwordFormContainer_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(passwordFormContainer_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (connectProgressContainer_) {
        if (mode == ConnectOverlayMode::Connecting) {
            lv_obj_clear_flag(connectProgressContainer_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(connectProgressContainer_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NetworkDiagnosticsPanel::setConnectProgress(
    const std::optional<Network::WifiConnectProgress>& progress)
{
    if (progress.has_value()) {
        if (!connectStartedAt_.has_value()) {
            connectStartedAt_ = std::chrono::steady_clock::now();
        }
        if (progress->phase != Network::WifiConnectPhase::Canceling) {
            lastConnectPhase_ = progress->phase;
        }
    }
    else {
        if (!connectAwaitingConfirmationSsid_.has_value()) {
            connectStartedAt_.reset();
            lastConnectPhase_ = Network::WifiConnectPhase::Starting;
        }
    }

    connectProgress_ = progress;
    updateConnectOverlay();
}

void NetworkDiagnosticsPanel::updateConnectPhaseBadges()
{
    const auto phase = connectProgress_.has_value()
            && connectProgress_->phase != Network::WifiConnectPhase::Canceling
        ? connectProgress_->phase
        : lastConnectPhase_;
    const size_t activeStageIndex = connectPhaseStageIndex(phase);

    for (size_t i = 0; i < connectProgressStageBadges_.size(); ++i) {
        lv_obj_t* badge = connectProgressStageBadges_[i];
        if (!badge) {
            continue;
        }

        lv_obj_t* badgeLabel = lv_obj_get_child(badge, 0);
        if (!badgeLabel) {
            continue;
        }

        uint32_t backgroundColor = CONNECT_STAGE_PENDING_BG_COLOR;
        uint32_t borderColor = CONNECT_STAGE_PENDING_BORDER_COLOR;
        uint32_t textColor = CONNECT_STAGE_PENDING_TEXT_COLOR;

        if (i < activeStageIndex) {
            backgroundColor = CONNECT_STAGE_COMPLETE_BG_COLOR;
            borderColor = CONNECT_STAGE_COMPLETE_BORDER_COLOR;
            textColor = CONNECT_STAGE_COMPLETE_TEXT_COLOR;
        }
        else if (i == activeStageIndex) {
            backgroundColor = CONNECT_STAGE_ACTIVE_BG_COLOR;
            borderColor = CONNECT_STAGE_ACTIVE_BORDER_COLOR;
            textColor = CONNECT_STAGE_ACTIVE_TEXT_COLOR;
        }

        lv_obj_set_style_bg_color(badge, lv_color_hex(backgroundColor), 0);
        lv_obj_set_style_border_color(badge, lv_color_hex(borderColor), 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(textColor), 0);
    }
}

void NetworkDiagnosticsPanel::updateConnectOverlay()
{
    if (!passwordOverlay_ || !passwordPromptNetwork_.has_value()) {
        return;
    }
    if (connectOverlayMode_ != ConnectOverlayMode::Connecting || !connectProgressTitleLabel_
        || !connectProgressPhaseLabel_ || !connectProgressDetailLabel_) {
        return;
    }

    const std::string ssid = connectAwaitingConfirmationSsid_.has_value()
        ? connectAwaitingConfirmationSsid_.value()
        : (connectProgress_.has_value() ? connectProgress_->ssid : passwordPromptNetwork_->ssid);
    const std::string titleText = "Connecting to " + ssid;
    lv_label_set_text(connectProgressTitleLabel_, titleText.c_str());

    const std::string phaseText = formatAnimatedConnectPhaseText();
    lv_label_set_text(connectProgressPhaseLabel_, phaseText.c_str());
    updateConnectPhaseBadges();

    const bool awaitingConfirmationOnly =
        connectAwaitingConfirmationSsid_.has_value() && !connectProgress_.has_value();
    const std::string elapsedText = formatConnectElapsedText();
    const std::string detailText = awaitingConfirmationOnly
        ? (elapsedText.empty() ? "Waiting for connection confirmation"
                               : "Waiting for connection confirmation • " + elapsedText)
        : elapsedText;
    lv_label_set_text(connectProgressDetailLabel_, detailText.c_str());

    if (connectProgressCancelButton_) {
        if (awaitingConfirmationOnly || isConnectFinalizing(connectProgress_)) {
            lv_obj_add_flag(connectProgressCancelButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(connectProgressCancelButton_, LV_OBJ_FLAG_HIDDEN);
            const bool canCancel =
                connectProgress_.has_value() ? connectProgress_->canCancel : true;
            setActionButtonEnabled(connectProgressCancelButton_, canCancel);
            setActionButtonText(connectProgressCancelButton_, canCancel ? "Cancel" : "Canceling");
        }
    }
}

void NetworkDiagnosticsPanel::finalizeConfirmedConnect()
{
    if (!connectAwaitingConfirmationSsid_.has_value()) {
        return;
    }

    const std::string confirmedSsid = connectAwaitingConfirmationSsid_.value();
    LOG_INFO(Controls, "WiFi connect confirmed for {}", confirmedSsid);

    connectAwaitingConfirmationSsid_.reset();
    if (passwordOverlay_) {
        closePasswordPrompt();
    }

    endAsyncAction(AsyncActionKind::Connect);
    setConnectProgress(std::nullopt);
    updateWifiStatus(
        Result<Network::WifiStatus, std::string>::okay(
            Network::WifiStatus{
                .connected = true,
                .ssid = confirmedSsid,
            }));
    if (!networks_.empty()) {
        updateNetworkDisplay(
            Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
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

void NetworkDiagnosticsPanel::setPasswordPromptStatus(const std::string& message, uint32_t color)
{
    if (!passwordStatusLabel_) {
        return;
    }

    if (message.empty()) {
        lv_label_set_text(passwordStatusLabel_, "");
        lv_obj_add_flag(passwordStatusLabel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(passwordStatusLabel_, message.c_str());
    lv_obj_set_style_text_color(passwordStatusLabel_, lv_color_hex(color), 0);
    lv_obj_clear_flag(passwordStatusLabel_, LV_OBJ_FLAG_HIDDEN);
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

void NetworkDiagnosticsPanel::setScannerRefreshButtonEnabled(bool enabled)
{
    if (!scannerRefreshButton_) {
        return;
    }

    lv_obj_t* button = lv_obj_get_child(scannerRefreshButton_, 0);
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

void NetworkDiagnosticsPanel::setLiveScanToggleEnabled(bool enabled)
{
    if (!liveScanToggle_) {
        return;
    }

    lv_obj_t* container = lv_obj_get_parent(liveScanToggle_);
    if (enabled) {
        lv_obj_clear_state(liveScanToggle_, LV_STATE_DISABLED);
        if (container) {
            lv_obj_clear_state(container, LV_STATE_DISABLED);
        }
    }
    else {
        lv_obj_add_state(liveScanToggle_, LV_STATE_DISABLED);
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

bool NetworkDiagnosticsPanel::startAsyncDiagnosticsModeSet(bool enabled)
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->diagnosticsModeUpdateInProgress) {
            return false;
        }
        asyncState_->diagnosticsModeUpdateInProgress = true;
    }

    auto state = asyncState_;
    std::thread([state, enabled]() {
        Result<bool, std::string> result =
            Result<bool, std::string>::error("Network diagnostics mode update failed");

        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<bool, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::NetworkDiagnosticsModeSet::Command cmd{ .active = enabled };
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::NetworkDiagnosticsModeSet::Okay>(
                        cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<bool, std::string>::error(
                        "NetworkDiagnosticsModeSet failed: " + response.errorValue());
                }
                else {
                    const auto inner = response.value();
                    if (inner.isError()) {
                        result = Result<bool, std::string>::error(
                            "NetworkDiagnosticsModeSet failed: " + inner.errorValue().message);
                    }
                    else {
                        result = Result<bool, std::string>::okay(inner.value().active);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<bool, std::string>::error(e.what());
        }
        catch (...) {
            result = Result<bool, std::string>::error("Network diagnostics mode update failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingDiagnosticsModeUpdate = std::move(result);
        state->diagnosticsModeUpdateInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncRefresh(bool forceRefresh)
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

    auto state = asyncState_;
    std::thread([state, forceRefresh]() {
        PendingRefreshData data;
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
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
                OsApi::SystemStatus::Command statusCmd{};
                const auto accessResponse =
                    client.sendCommandAndGetResponse<OsApi::SystemStatus::Okay>(statusCmd, 2000);
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
                        status.scannerModeAvailable = accessInner.value().scanner_mode_available;
                        status.scannerModeActive = accessInner.value().scanner_mode_active;
                        status.scannerModeDetail = accessInner.value().scanner_mode_detail;
                        data.accessStatusResult =
                            Result<NetworkAccessStatus, std::string>::okay(std::move(status));
                    }
                }

                const bool scannerModeActive = !data.accessStatusResult.isError()
                    && data.accessStatusResult.value().scannerModeActive;
                if (scannerModeActive) {
                    data.statusResult =
                        Result<Network::WifiStatus, std::string>::error("Scanner mode active");
                    data.listResult =
                        Result<std::vector<Network::WifiNetworkInfo>, std::string>::error(
                            "Scanner mode active");
                    client.disconnect();
                }
                else {
                    OsApi::NetworkSnapshotGet::Command snapshotCmd{ .forceRefresh = forceRefresh };
                    const auto snapshotResponse =
                        client.sendCommandAndGetResponse<OsApi::NetworkSnapshotGet::Okay>(
                            snapshotCmd, NETWORK_SNAPSHOT_TIMEOUT_MS);
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
                                    "NetworkSnapshotGet failed: "
                                    + snapshotInner.errorValue().message);
                        }
                        else {
                            std::vector<Network::WifiNetworkInfo> networks;
                            networks.reserve(snapshotInner.value().networks.size());
                            for (const auto& network : snapshotInner.value().networks) {
                                networks.push_back(toUiWifiNetworkInfo(network));
                            }

                            std::vector<Network::WifiAccessPointInfo> accessPoints;
                            accessPoints.reserve(snapshotInner.value().accessPoints.size());
                            for (const auto& accessPoint : snapshotInner.value().accessPoints) {
                                accessPoints.push_back(toUiWifiAccessPointInfo(accessPoint));
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
                            data.accessPoints = std::move(accessPoints);
                            data.activeBssid = snapshotInner.value().activeBssid;
                            data.localAddresses = std::move(localAddresses);
                            if (snapshotInner.value().connectOutcome.has_value()) {
                                data.connectOutcome = toUiWifiConnectOutcome(
                                    snapshotInner.value().connectOutcome.value());
                            }
                            if (snapshotInner.value().connectProgress.has_value()) {
                                data.connectProgress = toUiWifiConnectProgress(
                                    snapshotInner.value().connectProgress.value());
                            }
                            data.lastScanAgeMs = snapshotInner.value().lastScanAgeMs;
                            data.scanInProgress = snapshotInner.value().scanInProgress;
                        }
                    }
                }
            }

            if (!data.accessStatusResult.isError()) {
                NetworkAccessCache cache;
                cache.webUiEnabled = data.accessStatusResult.value().webUiEnabled;
                cache.webSocketEnabled = data.accessStatusResult.value().webSocketEnabled;
                cache.webSocketToken = data.accessStatusResult.value().webSocketToken;
                cache.scannerModeAvailable = data.accessStatusResult.value().scannerModeAvailable;
                cache.scannerModeActive = data.accessStatusResult.value().scannerModeActive;
                cache.scannerModeDetail = data.accessStatusResult.value().scannerModeDetail;
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

bool NetworkDiagnosticsPanel::startAsyncScanRequest()
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->scanRequestInProgress) {
            return false;
        }
        asyncState_->scanRequestInProgress = true;
    }

    auto state = asyncState_;
    std::thread([state]() {
        Result<std::monostate, std::string> result =
            Result<std::monostate, std::string>::error("WiFi scan request failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<std::monostate, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WifiScanRequest::Command cmd{};
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::WifiScanRequest::Okay>(cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "WifiScanRequest failed: " + response.errorValue());
                }
                else if (response.value().isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "WifiScanRequest failed: " + response.value().errorValue().message);
                }
                else {
                    result = Result<std::monostate, std::string>::okay(std::monostate{});
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<std::monostate, std::string>::error(e.what());
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingScanRequest = std::move(result);
        state->scanRequestInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncScannerEnter()
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->scannerEnterInProgress || asyncState_->scannerExitInProgress) {
            return false;
        }
        asyncState_->scannerEnterInProgress = true;
    }

    scannerActionInProgress_ = true;
    if (scannerStatusLabel_) {
        lv_label_set_text(scannerStatusLabel_, "Entering scanner mode...");
    }
    updateScannerControls();
    setScannerRefreshButtonEnabled(false);

    auto state = asyncState_;
    std::thread([state]() {
        Result<std::monostate, std::string> result =
            Result<std::monostate, std::string>::error("Scanner mode enter failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<std::monostate, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::ScannerModeEnter::Command cmd{};
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::ScannerModeEnter::Okay>(cmd, 10000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "ScannerModeEnter failed: " + response.errorValue());
                }
                else if (response.value().isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "ScannerModeEnter failed: " + response.value().errorValue().message);
                }
                else {
                    result = Result<std::monostate, std::string>::okay(std::monostate{});
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<std::monostate, std::string>::error(e.what());
        }
        catch (...) {
            result = Result<std::monostate, std::string>::error("Scanner mode enter failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingScannerEnter = std::move(result);
        state->scannerEnterInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncScannerExit()
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->scannerEnterInProgress || asyncState_->scannerExitInProgress) {
            return false;
        }
        asyncState_->scannerExitInProgress = true;
    }

    scannerActionInProgress_ = true;
    if (scannerStatusLabel_) {
        lv_label_set_text(scannerStatusLabel_, "Restoring normal Wi-Fi...");
    }
    updateScannerControls();
    setScannerRefreshButtonEnabled(false);

    auto state = asyncState_;
    std::thread([state]() {
        Result<std::monostate, std::string> result =
            Result<std::monostate, std::string>::error("Scanner mode exit failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<std::monostate, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::ScannerModeExit::Command cmd{};
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::ScannerModeExit::Okay>(cmd, 60000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "ScannerModeExit failed: " + response.errorValue());
                }
                else if (response.value().isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "ScannerModeExit failed: " + response.value().errorValue().message);
                }
                else {
                    result = Result<std::monostate, std::string>::okay(std::monostate{});
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<std::monostate, std::string>::error(e.what());
        }
        catch (...) {
            result = Result<std::monostate, std::string>::error("Scanner mode exit failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingScannerExit = std::move(result);
        state->scannerExitInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncScannerSnapshot()
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->scannerSnapshotInProgress) {
            return false;
        }
        asyncState_->scannerSnapshotInProgress = true;
    }

    auto state = asyncState_;
    std::thread([state]() {
        Result<ScannerSnapshot, ScannerSnapshotError> result =
            Result<ScannerSnapshot, ScannerSnapshotError>::error(
                ScannerSnapshotError{ "Scanner snapshot failed" });
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<ScannerSnapshot, ScannerSnapshotError>::error(
                    ScannerSnapshotError{ "Failed to connect to os-manager: "
                                          + connectResult.errorValue() });
            }
            else {
                OsApi::ScannerSnapshotGet::Command cmd{};
                cmd.maxRadios = 48;
                cmd.maxAgeMs = 15000;
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::ScannerSnapshotGet::Okay>(cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<ScannerSnapshot, ScannerSnapshotError>::error(
                        ScannerSnapshotError{ "ScannerSnapshotGet failed: "
                                              + response.errorValue() });
                }
                else if (response.value().isError()) {
                    result = Result<ScannerSnapshot, ScannerSnapshotError>::error(
                        ScannerSnapshotError{ "ScannerSnapshotGet failed: "
                                              + response.value().errorValue().message });
                }
                else {
                    const auto& okay = response.value().value();
                    ScannerSnapshot snapshot;
                    snapshot.active = okay.active;
                    snapshot.currentChannel = okay.currentChannel;
                    snapshot.detail = okay.detail;
                    snapshot.radios.reserve(okay.radios.size());
                    for (const auto& radio : okay.radios) {
                        ScannerObservedRadio entry;
                        entry.bssid = radio.bssid;
                        entry.ssid = radio.ssid;
                        entry.signalDbm = radio.signalDbm;
                        entry.channel = radio.channel;
                        entry.lastSeenAgeMs = radio.lastSeenAgeMs;
                        snapshot.radios.push_back(std::move(entry));
                    }

                    result =
                        Result<ScannerSnapshot, ScannerSnapshotError>::okay(std::move(snapshot));
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<ScannerSnapshot, ScannerSnapshotError>::error(
                ScannerSnapshotError{ e.what() });
        }
        catch (...) {
            result = Result<ScannerSnapshot, ScannerSnapshotError>::error(
                ScannerSnapshotError{ "Scanner snapshot failed" });
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingScannerSnapshot = std::move(result);
        state->scannerSnapshotInProgress = false;
    }).detach();

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncConnect(
    const Network::WifiNetworkInfo& network, const std::optional<std::string>& password)
{
    Network::WifiNetworkInfo networkCopy = network;
    if (!beginAsyncAction(AsyncActionKind::Connect, networkCopy, "Connecting to")) {
        return false;
    }

    auto state = asyncState_;
    std::thread([state, networkCopy, password]() {
        Result<Network::WifiConnectResult, std::string> result =
            Result<Network::WifiConnectResult, std::string>::error("WiFi connect failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<Network::WifiConnectResult, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WifiConnect::Command cmd{ .ssid = networkCopy.ssid, .password = password };
                const auto response = client.sendCommandAndGetResponse<OsApi::WifiConnect::Okay>(
                    cmd, OS_MANAGER_CONNECT_TIMEOUT_MS);
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

    return true;
}

bool NetworkDiagnosticsPanel::startAsyncConnectCancel()
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->connectCancelInProgress) {
            return false;
        }
        asyncState_->connectCancelInProgress = true;
    }

    auto state = asyncState_;
    std::thread([state]() {
        Result<std::monostate, std::string> result =
            Result<std::monostate, std::string>::error("WiFi connect cancel failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<std::monostate, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WifiConnectCancel::Command cmd{};
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::WifiConnectCancel::Okay>(cmd, 5000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "WifiConnectCancel failed: " + response.errorValue());
                }
                else if (response.value().isError()) {
                    result = Result<std::monostate, std::string>::error(
                        "WifiConnectCancel failed: " + response.value().errorValue().message);
                }
                else {
                    result = Result<std::monostate, std::string>::okay(std::monostate{});
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<std::monostate, std::string>::error(e.what());
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingConnectCancel = std::move(result);
        state->connectCancelInProgress = false;
    }).detach();

    return true;
}

void NetworkDiagnosticsPanel::startAsyncDisconnect(const Network::WifiNetworkInfo& network)
{
    Network::WifiNetworkInfo networkCopy = network;
    if (!beginAsyncAction(AsyncActionKind::Disconnect, networkCopy, "Disconnecting from")) {
        return;
    }

    auto state = asyncState_;
    std::thread([state, networkCopy]() {
        Result<Network::WifiDisconnectResult, std::string> result =
            Result<Network::WifiDisconnectResult, std::string>::error("WiFi disconnect failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<Network::WifiDisconnectResult, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::WifiDisconnect::Command cmd{ .ssid = networkCopy.ssid };
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::WifiDisconnect::Okay>(cmd, 25000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<Network::WifiDisconnectResult, std::string>::error(
                        "WifiDisconnect failed: " + response.errorValue());
                }
                else {
                    const auto inner = response.value();
                    if (inner.isError()) {
                        result = Result<Network::WifiDisconnectResult, std::string>::error(
                            "WifiDisconnect failed: " + inner.errorValue().message);
                    }
                    else {
                        result = Result<Network::WifiDisconnectResult, std::string>::okay(
                            toUiWifiDisconnectResult(inner.value()));
                    }
                }
            }
        }
        catch (const std::exception& e) {
            LOG_WARN(Controls, "WiFi disconnect exception: {}", e.what());
            result = Result<Network::WifiDisconnectResult, std::string>::error(e.what());
        }
        catch (...) {
            LOG_WARN(Controls, "WiFi disconnect exception: unknown");
            result =
                Result<Network::WifiDisconnectResult, std::string>::error("WiFi disconnect failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingDisconnect = result;
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
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
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

    auto state = asyncState_;
    std::thread([state, enabled]() {
        auto fetchAccessStatus = []() -> Result<NetworkAccessStatus, std::string> {
            Network::WebSocketService client;
            auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
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
            status.scannerModeAvailable = inner.value().scanner_mode_available;
            status.scannerModeActive = inner.value().scanner_mode_active;
            status.scannerModeDetail = inner.value().scanner_mode_detail;
            return Result<NetworkAccessStatus, std::string>::okay(std::move(status));
        };

        Result<NetworkAccessStatus, std::string> result =
            Result<NetworkAccessStatus, std::string>::error("Unknown error");

        try {
            Network::WebSocketService client;
            auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
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
            cache.scannerModeAvailable = result.value().scannerModeAvailable;
            cache.scannerModeActive = result.value().scannerModeActive;
            cache.scannerModeDetail = result.value().scannerModeDetail;
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

    auto state = asyncState_;
    std::thread([state, enabled]() {
        auto fetchAccessStatus = []() -> Result<NetworkAccessStatus, std::string> {
            Network::WebSocketService client;
            auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
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
            status.scannerModeAvailable = inner.value().scanner_mode_available;
            status.scannerModeActive = inner.value().scanner_mode_active;
            status.scannerModeDetail = inner.value().scanner_mode_detail;
            return Result<NetworkAccessStatus, std::string>::okay(std::move(status));
        };

        Result<NetworkAccessStatus, std::string> result =
            Result<NetworkAccessStatus, std::string>::error("Unknown error");

        try {
            Network::WebSocketService client;
            auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
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
            cache.scannerModeAvailable = result.value().scannerModeAvailable;
            cache.scannerModeActive = result.value().scannerModeActive;
            cache.scannerModeDetail = result.value().scannerModeDetail;
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
    if (kind == AsyncActionKind::Connect) {
        connectStartedAt_ = std::chrono::steady_clock::now();
        lastConnectPhase_ = Network::WifiConnectPhase::Starting;
    }

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

    return true;
}

void NetworkDiagnosticsPanel::endAsyncAction(AsyncActionKind kind)
{
    if (actionState_.kind != kind) {
        return;
    }

    if (kind == AsyncActionKind::Connect) {
        connectStartedAt_.reset();
    }

    actionState_.kind = AsyncActionKind::None;
    actionState_.ssid.clear();
}

bool NetworkDiagnosticsPanel::isActionInProgress() const
{
    return actionState_.kind != AsyncActionKind::None || scannerActionInProgress_;
}

void NetworkDiagnosticsPanel::submitPasswordPrompt()
{
    if (!passwordPromptNetwork_.has_value() || !passwordTextArea_ || isActionInProgress()) {
        return;
    }

    const char* passwordText = lv_textarea_get_text(passwordTextArea_);
    if (!passwordText || *passwordText == '\0') {
        setPasswordPromptError("Password is required.");
        updatePasswordJoinButton();
        return;
    }

    setPasswordPromptError("");
    if (!startAsyncConnect(passwordPromptNetwork_.value(), std::string(passwordText))) {
        return;
    }

    setPasswordPromptStatus("", MUTED_TEXT_COLOR);
    setConnectOverlayMode(ConnectOverlayMode::Connecting);
    updateConnectOverlay();
}

void NetworkDiagnosticsPanel::updatePasswordJoinButton()
{
    if (!passwordJoinButton_) {
        return;
    }

    if (passwordPromptNetwork_.has_value()) {
        setActionButtonText(
            passwordJoinButton_, passwordPromptSubmitText(passwordPromptNetwork_.value()));
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
    if (scannerActionInProgress_) {
        latestWifiStatus_.reset();
        setWifiStatusMessage(
            scannerModeActive_ ? "Restoring normal Wi-Fi..." : "Switching Wi-Fi modes...",
            MUTED_TEXT_COLOR);
        return;
    }

    if (scannerModeActive_) {
        latestWifiStatus_.reset();
        setWifiStatusMessage(
            "Scanner mode active. Return to Wi-Fi to manage connections.", MUTED_TEXT_COLOR);
        return;
    }

    if (statusResult.isError()) {
        latestWifiStatus_.reset();
    }
    else {
        latestWifiStatus_ = statusResult.value();
    }

    if (connectProgress_.has_value() || connectAwaitingConfirmationSsid_.has_value()) {
        setWifiStatusMessage(formatConnectStatusMessage(), 0x00CED1);
        return;
    }

    if (statusResult.isError()) {
        setWifiStatusMessage("Wi-Fi unavailable", ERROR_TEXT_COLOR);
        LOG_WARN(Controls, "WiFi status failed: {}", statusResult.errorValue());
        return;
    }

    if (scanInProgress_) {
        setWifiStatusMessage("Scanning nearby networks...", MUTED_TEXT_COLOR);
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

void NetworkDiagnosticsPanel::updateScannerStatus(
    const Result<NetworkAccessStatus, std::string>& statusResult)
{
    if (statusResult.isError()) {
        scannerModeAvailable_ = false;
        scannerModeActive_ = false;
        scannerModeDetail_ = statusResult.errorValue();
        if (scannerStatusLabel_) {
            const std::string text = "Scanner status unavailable.\n" + scannerModeDetail_;
            lv_label_set_text(scannerStatusLabel_, text.c_str());
        }
        updateScannerControls();
        return;
    }

    const auto& status = statusResult.value();
    scannerModeAvailable_ = status.scannerModeAvailable;
    scannerModeActive_ = status.scannerModeActive;
    scannerModeDetail_ = status.scannerModeDetail;

    if (scannerStatusLabel_) {
        std::string text;
        if (scannerModeActive_) {
            text = "Scanner mode active.\n";
        }
        else if (scannerModeAvailable_) {
            text = "Scanner mode ready.\n";
        }
        else {
            text = "Scanner mode unavailable.\n";
        }
        text += scannerModeDetail_;
        lv_label_set_text(scannerStatusLabel_, text.c_str());
    }

    updateScannerControls();
}

void NetworkDiagnosticsPanel::updateScannerSnapshot(
    const Result<ScannerSnapshot, ScannerSnapshotError>& result)
{
    if (!scannerDataLabel_) {
        return;
    }

    if (result.isError()) {
        const std::string text = "Scanner data unavailable: " + result.errorValue().message;
        lv_label_set_text(scannerDataLabel_, text.c_str());
        lv_obj_set_style_text_color(scannerDataLabel_, lv_color_hex(ERROR_TEXT_COLOR), 0);
        if (scannerChannelPlot24_) {
            scannerChannelPlot24_->clear();
        }
        if (scannerChannelPlot5_) {
            scannerChannelPlot5_->clear();
        }
        return;
    }

    const auto& snapshot = result.value();

    std::vector<float> channel24MaxSignal(11, 0.0f);
    std::vector<float> channel5MaxSignal(9, 0.0f);
    constexpr std::array<int, 9> kChannel5Plan{ { 36, 40, 44, 48, 149, 153, 157, 161, 165 } };

    for (const auto& radio : snapshot.radios) {
        if (!radio.channel.has_value() || !radio.signalDbm.has_value()) {
            continue;
        }

        const int channel = radio.channel.value();
        const float signal = static_cast<float>(radio.signalDbm.value());

        if (channel >= 1 && channel <= 11) {
            const size_t idx = static_cast<size_t>(channel - 1);
            if (channel24MaxSignal[idx] == 0.0f || signal > channel24MaxSignal[idx]) {
                channel24MaxSignal[idx] = signal;
            }
            continue;
        }

        const auto it = std::find(kChannel5Plan.begin(), kChannel5Plan.end(), channel);
        if (it != kChannel5Plan.end()) {
            const size_t idx = static_cast<size_t>(std::distance(kChannel5Plan.begin(), it));
            if (channel5MaxSignal[idx] == 0.0f || signal > channel5MaxSignal[idx]) {
                channel5MaxSignal[idx] = signal;
            }
        }
    }

    if (scannerChannelPlot24_) {
        scannerChannelPlot24_->setSamples(channel24MaxSignal);
    }
    if (scannerChannelPlot5_) {
        scannerChannelPlot5_->setSamples(channel5MaxSignal);
    }

    std::string text;
    if (!snapshot.detail.empty()) {
        text += snapshot.detail;
        text += "\n";
    }
    if (snapshot.currentChannel.has_value()) {
        text += "Channel: ";
        text += std::to_string(*snapshot.currentChannel);
        text += "\n";
    }
    text += "Observed radios: ";
    text += std::to_string(snapshot.radios.size());
    text += "\n\n";
    if (snapshot.radios.empty()) {
        text += "No radios observed yet.";
    }
    else {
        const size_t maxRows = 10;
        const size_t rowCount = std::min(snapshot.radios.size(), maxRows);
        for (size_t i = 0; i < rowCount; ++i) {
            const auto& radio = snapshot.radios[i];
            const std::string ssid = !radio.ssid.empty() ? radio.ssid : std::string("<hidden>");
            text += ssid;
            if (radio.channel.has_value()) {
                text += "  ch ";
                text += std::to_string(*radio.channel);
            }
            if (radio.signalDbm.has_value()) {
                text += "  ";
                text += std::to_string(*radio.signalDbm);
                text += " dBm";
            }
            if (radio.lastSeenAgeMs.has_value()) {
                text += "  ";
                text += std::to_string(*radio.lastSeenAgeMs);
                text += "ms";
            }
            text += "\n";
            text += radio.bssid;
            text += "\n";
        }
        if (snapshot.radios.size() > rowCount) {
            text += "\n";
            text += "…";
            text += std::to_string(snapshot.radios.size() - rowCount);
            text += " more";
        }
    }

    lv_label_set_text(scannerDataLabel_, text.c_str());
    lv_obj_set_style_text_color(scannerDataLabel_, lv_color_hex(0xFFFFFF), 0);
}

void NetworkDiagnosticsPanel::updateScannerSnapshotPolling()
{
    if (viewMode_ != ViewMode::Scanner) {
        return;
    }

    if (!scannerModeActive_) {
        if (scannerDataLabel_) {
            lv_label_set_text(scannerDataLabel_, "Enter scanner mode to view observed radios.");
            lv_obj_set_style_text_color(scannerDataLabel_, lv_color_hex(MUTED_TEXT_COLOR), 0);
        }
        if (scannerChannelPlot24_) {
            scannerChannelPlot24_->clear();
        }
        if (scannerChannelPlot5_) {
            scannerChannelPlot5_->clear();
        }
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (scannerSnapshotLastRequestedAt_.has_value()
        && now - scannerSnapshotLastRequestedAt_.value() < std::chrono::milliseconds(800)) {
        return;
    }

    if (startAsyncScannerSnapshot()) {
        scannerSnapshotLastRequestedAt_ = now;
    }
}

void NetworkDiagnosticsPanel::updateScannerControls()
{
    if (scannerHintLabel_) {
        if (scannerModeActive_) {
            lv_label_set_text(
                scannerHintLabel_,
                "wlan0 is dedicated to monitoring while scanner mode is active. Return to Wi-Fi "
                "when you are done.");
        }
        else {
            lv_label_set_text(
                scannerHintLabel_,
                "Scanner mode is exclusive. While active, wlan0 leaves NetworkManager and normal "
                "Wi-Fi connections are unavailable.");
        }
    }

    setActionButtonEnabled(
        scannerEnterButton_, !isActionInProgress() && scannerModeAvailable_ && !scannerModeActive_);
    setActionButtonEnabled(scannerExitButton_, !isActionInProgress() && scannerModeActive_);
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

std::string NetworkDiagnosticsPanel::formatAddressSummaryMultiline() const
{
    std::vector<std::string> parts;
    parts.reserve(localAddresses_.size());
    for (const auto& info : localAddresses_) {
        parts.push_back(info.name + " " + info.address);
    }

    return joinTextParts(parts, "\n");
}

std::string NetworkDiagnosticsPanel::formatCurrentConnectionDetails(
    const Network::WifiNetworkInfo& info) const
{
    std::vector<std::string> parts;
    if (info.signalDbm.has_value()) {
        parts.push_back(std::to_string(info.signalDbm.value()) + " dBm");
    }

    parts.push_back(info.security.empty() ? "unknown" : info.security);

    std::vector<std::string> lines;
    if (!parts.empty()) {
        lines.push_back(joinTextParts(parts, " • "));
    }

    auto appendAddressLines = [this, &lines](bool wifiFirst) {
        for (const auto& info : localAddresses_) {
            const bool isWifiAddress =
                info.name.rfind("wl", 0) == 0 || info.name.rfind("wifi", 0) == 0;
            if (isWifiAddress != wifiFirst) {
                continue;
            }

            lines.push_back(info.name + " " + info.address);
        }
    };

    appendAddressLines(true);
    appendAddressLines(false);

    if (!info.lastUsedRelative.empty() && info.lastUsedRelative != "not saved") {
        lines.push_back("used " + info.lastUsedRelative);
    }

    return joinTextParts(lines, "\n");
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

std::string NetworkDiagnosticsPanel::formatLastScanAgeText() const
{
    if (!lastScanAgeMs_.has_value()) {
        return "unavailable";
    }

    uint64_t ageMs = lastScanAgeMs_.value();
    if (lastScanAgeUpdatedAt_.has_value()) {
        const auto elapsed = std::chrono::steady_clock::now() - lastScanAgeUpdatedAt_.value();
        ageMs += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    return formatDurationAgo(ageMs);
}

std::optional<Network::WifiAccessPointInfo> NetworkDiagnosticsPanel::preferredAccessPointForSsid(
    const std::string& ssid) const
{
    return selectPreferredAccessPointForSsid(accessPoints_, ssid);
}

void NetworkDiagnosticsPanel::updateDetailsLastScanLabel()
{
    if (!networkDetailsLastScanValueLabel_) {
        return;
    }

    const std::string lastScanText = formatLastScanAgeText();
    lv_label_set_text(networkDetailsLastScanValueLabel_, lastScanText.c_str());
}

void NetworkDiagnosticsPanel::updateDetailsSignalHistoryPlots()
{
    for (auto& binding : networkDetailsPlotBindings_) {
        if (!binding.plot) {
            continue;
        }

        const auto historyIt = signalHistoryByBssid_.find(binding.bssid);
        if (historyIt == signalHistoryByBssid_.end() || historyIt->second.samples.empty()) {
            binding.plot->clear();
            binding.plot->clearBottomLabels();
            continue;
        }

        binding.plot->setSamples(historyIt->second.samples);
        binding.plot->clearBottomLabels();
    }
}

void NetworkDiagnosticsPanel::updateSignalHistory(bool forceSample)
{
    if (viewMode_ != ViewMode::Wifi) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!forceSample && signalHistoryLastSampleAt_.has_value()
        && now - signalHistoryLastSampleAt_.value() < std::chrono::seconds(1)) {
        updateDetailsLastScanLabel();
        updateDetailsSignalHistoryPlots();
        return;
    }

    signalHistoryLastSampleAt_ = now;
    std::unordered_set<std::string> currentBssids;
    for (const auto& accessPoint : accessPoints_) {
        if (!accessPoint.bssid.empty()) {
            currentBssids.insert(accessPoint.bssid);
        }
    }

    for (const auto& accessPoint : accessPoints_) {
        if (accessPoint.bssid.empty() || !accessPoint.signalDbm.has_value()) {
            continue;
        }

        auto& series = signalHistoryByBssid_[accessPoint.bssid];
        series.ssid = accessPoint.ssid;
        series.samples.push_back(static_cast<float>(accessPoint.signalDbm.value()));
        if (series.samples.size() > NETWORK_SIGNAL_HISTORY_MAX_SAMPLES) {
            series.samples.erase(series.samples.begin());
        }
    }

    for (auto it = signalHistoryByBssid_.begin(); it != signalHistoryByBssid_.end();) {
        if (!currentBssids.contains(it->first)) {
            it = signalHistoryByBssid_.erase(it);
            continue;
        }

        ++it;
    }

    updateDetailsLastScanLabel();
    updateDetailsSignalHistoryPlots();
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
    disconnectContexts_.clear();
    forgetContexts_.clear();
    networkDetailsContexts_.clear();

    if (scannerModeActive_) {
        updateCurrentConnectionSummary();
        lv_label_set_text(networksTitleLabel_, "Scanner mode");
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, "Scanner mode active.\nReturn to Wi-Fi to manage connections.");
        lv_obj_set_style_text_color(label, lv_color_hex(MUTED_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        return;
    }

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
        lv_obj_set_style_pad_all(row, NETWORK_ROW_PADDING, 0);
        lv_obj_set_style_pad_column(row, NETWORK_ROW_PADDING, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(NETWORK_ROW_BG_COLOR), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(NETWORK_ROW_BORDER_COLOR), 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x262626), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(
            row, lv_color_hex(CONNECT_STAGE_ACTIVE_BORDER_COLOR), LV_STATE_PRESSED);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        auto detailsContext = std::make_unique<NetworkDetailsContext>();
        detailsContext->panel = this;
        detailsContext->index = i;
        networkDetailsContexts_.push_back(std::move(detailsContext));
        NetworkDetailsContext* detailsContextPtr = networkDetailsContexts_.back().get();
        lv_obj_add_event_cb(row, onNetworkDetailsClicked, LV_EVENT_CLICKED, detailsContextPtr);

        lv_obj_t* textColumn = lv_obj_create(row);
        lv_obj_set_size(textColumn, 0, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textColumn, 1);
        lv_obj_set_flex_flow(textColumn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(textColumn, 0, 0);
        lv_obj_set_style_pad_row(textColumn, 2, 0);
        lv_obj_set_style_bg_opa(textColumn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textColumn, 0, 0);
        lv_obj_clear_flag(textColumn, LV_OBJ_FLAG_SCROLLABLE);

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

        lv_obj_t* chevronLabel = lv_label_create(row);
        lv_label_set_text(chevronLabel, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(chevronLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(chevronLabel, lv_color_hex(HEADER_TEXT_COLOR), 0);
        lv_obj_set_style_text_color(
            chevronLabel, lv_color_hex(CONNECT_STAGE_ACTIVE_BG_COLOR), LV_STATE_PRESSED);

        const bool isConnecting =
            actionState_.kind == AsyncActionKind::Connect && network.ssid == actionState_.ssid;
        const bool actionsDisabled = isActionInProgress();
        const bool requiresPassword = networkRequiresPassword(network);
        const bool usesJoinSemantics = shouldUseJoinSemantics(network, requiresPassword);

        std::string buttonText = usesJoinSemantics ? "Join" : "Connect";
        if (isConnecting) {
            buttonText = usesJoinSemantics ? "Joining" : "Connecting";
        }

        auto context = std::make_unique<ConnectContext>();
        context->panel = this;
        context->index = i;
        connectContexts_.push_back(std::move(context));
        ConnectContext* contextPtr = connectContexts_.back().get();

        lv_obj_t* buttonContainer = LVGLBuilder::actionButton(row)
                                        .text(buttonText.c_str())
                                        .mode(LVGLBuilder::ActionMode::Push)
                                        .width(NETWORK_ROW_BUTTON_WIDTH)
                                        .height(NETWORK_ACTION_BUTTON_HEIGHT)
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
        self->refresh(true);
        LOG_INFO(Controls, "Network info refreshed by user");
    }
}

void NetworkDiagnosticsPanel::onScannerRefreshClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->refresh(false);
        LOG_INFO(Controls, "Scanner status refreshed by user");
    }
}

void NetworkDiagnosticsPanel::onScannerEnterClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->startAsyncScannerEnter();
    }
}

void NetworkDiagnosticsPanel::onScannerExitClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->startAsyncScannerExit();
    }
}

void NetworkDiagnosticsPanel::applyPendingUpdates()
{
    if (!asyncState_) {
        return;
    }

    std::optional<Result<Network::WifiConnectResult, std::string>> connectResult;
    std::optional<Result<Network::WifiDisconnectResult, std::string>> disconnectResult;
    std::optional<Result<std::monostate, std::string>> connectCancelResult;
    std::optional<Result<bool, std::string>> diagnosticsModeUpdateResult;
    std::optional<Result<Network::WifiForgetResult, std::string>> forgetResult;
    std::optional<Result<std::monostate, std::string>> scannerEnterResult;
    std::optional<Result<std::monostate, std::string>> scannerExitResult;
    std::optional<Result<ScannerSnapshot, ScannerSnapshotError>> scannerSnapshotResult;
    std::optional<PendingRefreshData> refreshData;
    std::optional<Result<std::monostate, std::string>> scanRequestResult;
    std::optional<Result<NetworkAccessStatus, std::string>> webSocketUpdateResult;
    std::optional<Result<NetworkAccessStatus, std::string>> webUiUpdateResult;

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        connectResult = asyncState_->pendingConnect;
        asyncState_->pendingConnect.reset();

        disconnectResult = asyncState_->pendingDisconnect;
        asyncState_->pendingDisconnect.reset();

        connectCancelResult = asyncState_->pendingConnectCancel;
        asyncState_->pendingConnectCancel.reset();

        diagnosticsModeUpdateResult = asyncState_->pendingDiagnosticsModeUpdate;
        asyncState_->pendingDiagnosticsModeUpdate.reset();

        forgetResult = asyncState_->pendingForget;
        asyncState_->pendingForget.reset();

        scannerEnterResult = asyncState_->pendingScannerEnter;
        asyncState_->pendingScannerEnter.reset();

        scannerExitResult = asyncState_->pendingScannerExit;
        asyncState_->pendingScannerExit.reset();

        scannerSnapshotResult = asyncState_->pendingScannerSnapshot;
        asyncState_->pendingScannerSnapshot.reset();

        refreshData = asyncState_->pendingRefresh;
        asyncState_->pendingRefresh.reset();

        scanRequestResult = asyncState_->pendingScanRequest;
        asyncState_->pendingScanRequest.reset();

        webSocketUpdateResult = asyncState_->pendingWebSocketUpdate;
        asyncState_->pendingWebSocketUpdate.reset();

        webUiUpdateResult = asyncState_->pendingWebUiUpdate;
        asyncState_->pendingWebUiUpdate.reset();
    }

    if (refreshData.has_value()) {
        scanInProgress_ = refreshData->scanInProgress;
        updateScannerStatus(refreshData->accessStatusResult);
        const auto connectOutcome = refreshData->connectOutcome;
        setConnectProgress(refreshData->connectProgress);
        activeBssid_ = refreshData->activeBssid;
        lastScanAgeMs_ = refreshData->lastScanAgeMs;
        if (refreshData->lastScanAgeMs.has_value()) {
            lastScanAgeUpdatedAt_ = std::chrono::steady_clock::now();
        }
        else {
            lastScanAgeUpdatedAt_.reset();
        }
        if (refreshData->accessPoints.has_value()) {
            accessPoints_ = std::move(refreshData->accessPoints.value());
        }
        else if (refreshData->listResult.isError()) {
            accessPoints_.clear();
        }
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
        updateSignalHistory(true);

        if (networkDetailsOverlay_ && networkDetailsNetwork_.has_value()) {
            const int32_t detailsScrollY =
                networkDetailsContent_ ? lv_obj_get_scroll_y(networkDetailsContent_) : 0;
            auto it = std::find_if(
                networks_.begin(),
                networks_.end(),
                [this](const Network::WifiNetworkInfo& network) {
                    return network.ssid == networkDetailsNetwork_->ssid;
                });
            if (it != networks_.end()) {
                openNetworkDetailsOverlay(*it);
                if (networkDetailsContent_) {
                    lv_obj_scroll_to_y(networkDetailsContent_, detailsScrollY, LV_ANIM_OFF);
                }
            }
            else {
                closeNetworkDetailsOverlay();
            }
        }

        if (connectOutcome.has_value()) {
            const bool outcomeMatchesPending = actionState_.kind == AsyncActionKind::Connect
                && actionState_.ssid == connectOutcome->ssid;
            if (outcomeMatchesPending) {
                const bool overlayConnect = passwordPromptNetwork_.has_value()
                    && passwordPromptNetwork_->ssid == connectOutcome->ssid;
                const bool needsPasswordPrompt = !connectOutcome->canceled && overlayConnect
                    && !connectOverlayHasPasswordEntry_
                    && connectFailureNeedsPasswordPrompt(connectOutcome->message);
                connectAwaitingConfirmationSsid_.reset();
                endAsyncAction(AsyncActionKind::Connect);
                setConnectProgress(std::nullopt);
                if (connectOutcome->canceled) {
                    LOG_INFO(Controls, "WiFi connect canceled for {}", connectOutcome->ssid);
                }
                else {
                    LOG_WARN(Controls, "WiFi connect failed: {}", connectOutcome->message);
                }
                setWifiStatusMessage(
                    connectOutcome->canceled ? "Wi-Fi connection canceled" : "Wi-Fi connect failed",
                    connectOutcome->canceled ? MUTED_TEXT_COLOR : ERROR_TEXT_COLOR);
                if (overlayConnect) {
                    if (needsPasswordPrompt) {
                        connectOverlayHasPasswordEntry_ = true;
                        setConnectOverlayMode(ConnectOverlayMode::PasswordEntry);
                        setPasswordPromptStatus("", MUTED_TEXT_COLOR);
                        setPasswordPromptError("Password required.");
                        if (passwordTextArea_) {
                            lv_textarea_set_text(passwordTextArea_, "");
                        }
                        updatePasswordJoinButton();
                    }
                    else if (connectOverlayHasPasswordEntry_) {
                        setConnectOverlayMode(ConnectOverlayMode::PasswordEntry);
                        setPasswordPromptStatus("", MUTED_TEXT_COLOR);
                        setPasswordPromptError(
                            connectOutcome->canceled ? "" : connectOutcome->message);
                        updatePasswordJoinButton();
                    }
                    else {
                        closePasswordPrompt();
                    }
                }
                if (!networks_.empty()) {
                    updateNetworkDisplay(
                        Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(
                            networks_));
                }
            }
        }

        if (connectAwaitingConfirmationSsid_.has_value() && !connectProgress_.has_value()
            && isConnectedToSsid(connectAwaitingConfirmationSsid_.value())) {
            finalizeConfirmedConnect();
        }
    }

    if (scannerEnterResult.has_value()) {
        scannerActionInProgress_ = false;
        if (scannerEnterResult->isError()) {
            LOG_WARN(Controls, "Scanner mode enter failed: {}", scannerEnterResult->errorValue());
            if (scannerStatusLabel_) {
                const std::string text =
                    "Failed to enter scanner mode.\n" + scannerEnterResult->errorValue();
                lv_label_set_text(scannerStatusLabel_, text.c_str());
            }
            updateScannerControls();
        }
        else {
            LOG_INFO(Controls, "Scanner mode entered");
            showScannerView();
            refresh();
        }
    }

    if (scannerSnapshotResult.has_value()) {
        updateScannerSnapshot(scannerSnapshotResult.value());
    }

    if (scannerExitResult.has_value()) {
        scannerActionInProgress_ = false;
        if (scannerExitResult->isError()) {
            LOG_WARN(Controls, "Scanner mode exit failed: {}", scannerExitResult->errorValue());
            if (scannerStatusLabel_) {
                const std::string text =
                    "Failed to return to Wi-Fi.\n" + scannerExitResult->errorValue();
                lv_label_set_text(scannerStatusLabel_, text.c_str());
            }
            updateScannerControls();
        }
        else {
            LOG_INFO(Controls, "Scanner mode exited");
            refresh();
        }
    }

    if (connectCancelResult.has_value() && connectCancelResult->isError()) {
        LOG_WARN(Controls, "WiFi connect cancel failed: {}", connectCancelResult->errorValue());
        setWifiStatusMessage("Wi-Fi cancel failed", ERROR_TEXT_COLOR);
        updateConnectOverlay();
    }

    if (diagnosticsModeUpdateResult.has_value()) {
        setLiveScanToggleEnabled(true);
        if (diagnosticsModeUpdateResult->isError()) {
            LOG_WARN(
                Controls,
                "Network diagnostics mode update failed: {}",
                diagnosticsModeUpdateResult->errorValue());
            if (liveScanToggle_) {
                liveScanToggleLocked_ = true;
                if (liveScanEnabled_) {
                    lv_obj_add_state(liveScanToggle_, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_clear_state(liveScanToggle_, LV_STATE_CHECKED);
                }
                liveScanToggleLocked_ = false;
            }
        }
        else {
            liveScanEnabled_ = diagnosticsModeUpdateResult->value();
            if (liveScanToggle_) {
                liveScanToggleLocked_ = true;
                if (liveScanEnabled_) {
                    lv_obj_add_state(liveScanToggle_, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_clear_state(liveScanToggle_, LV_STATE_CHECKED);
                }
                liveScanToggleLocked_ = false;
            }
        }
    }

    if (connectResult.has_value()) {
        if (connectResult->isError() && actionState_.kind != AsyncActionKind::Connect) {
            LOG_DEBUG(Controls, "Ignoring already handled WiFi connect result.");
        }
        else {
            const bool overlayConnect = passwordPromptNetwork_.has_value()
                && actionState_.kind == AsyncActionKind::Connect
                && actionState_.ssid == passwordPromptNetwork_->ssid;
            const bool canceled = connectResult->isError()
                && connectResult->errorValue() == "WiFi connection canceled";
            const bool needsPasswordPrompt = connectResult->isError() && !canceled && overlayConnect
                && !connectOverlayHasPasswordEntry_
                && connectFailureNeedsPasswordPrompt(connectResult->errorValue());
            const std::string connectSsid = actionState_.ssid;
            if (connectResult->isError()) {
                connectAwaitingConfirmationSsid_.reset();
                endAsyncAction(AsyncActionKind::Connect);
                setConnectProgress(std::nullopt);
                if (canceled) {
                    LOG_INFO(Controls, "WiFi connect canceled for {}", connectSsid);
                }
                else {
                    LOG_WARN(Controls, "WiFi connect failed: {}", connectResult->errorValue());
                }
                setWifiStatusMessage(
                    canceled ? "Wi-Fi connection canceled" : "Wi-Fi connect failed",
                    canceled ? MUTED_TEXT_COLOR : ERROR_TEXT_COLOR);
                if (overlayConnect) {
                    if (needsPasswordPrompt) {
                        connectOverlayHasPasswordEntry_ = true;
                        setConnectOverlayMode(ConnectOverlayMode::PasswordEntry);
                        setPasswordPromptStatus("", MUTED_TEXT_COLOR);
                        setPasswordPromptError("Password required.");
                        if (passwordTextArea_) {
                            lv_textarea_set_text(passwordTextArea_, "");
                        }
                        updatePasswordJoinButton();
                    }
                    else if (connectOverlayHasPasswordEntry_) {
                        setConnectOverlayMode(ConnectOverlayMode::PasswordEntry);
                        setPasswordPromptStatus("", MUTED_TEXT_COLOR);
                        setPasswordPromptError(canceled ? "" : connectResult->errorValue());
                        updatePasswordJoinButton();
                    }
                    else {
                        closePasswordPrompt();
                    }
                }
                if (!networks_.empty()) {
                    updateNetworkDisplay(
                        Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(
                            networks_));
                }
            }
            else {
                LOG_INFO(Controls, "WiFi connect completed for {}", connectResult->value().ssid);
                connectAwaitingConfirmationSsid_ = connectResult->value().ssid;
                finalizeConfirmedConnect();
                refresh();
            }
        }
    }

    if (disconnectResult.has_value()) {
        endAsyncAction(AsyncActionKind::Disconnect);
        if (disconnectResult->isError()) {
            LOG_WARN(Controls, "WiFi disconnect failed: {}", disconnectResult->errorValue());
            setWifiStatusMessage("Wi-Fi disconnect failed", ERROR_TEXT_COLOR);
            if (!networks_.empty()) {
                updateNetworkDisplay(
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
            }
        }
        else {
            const std::string disconnectedSsid = disconnectResult->value().ssid;
            LOG_INFO(Controls, "WiFi disconnect completed for {}", disconnectedSsid);
            latestWifiStatus_ = Network::WifiStatus{ .connected = false, .ssid = "" };
            for (auto& network : networks_) {
                if (network.ssid != disconnectedSsid
                    || network.status != Network::WifiNetworkStatus::Connected) {
                    continue;
                }

                network.status = network.connectionId.empty()
                    ? (isOpenSecurity(network.security) ? Network::WifiNetworkStatus::Open
                                                        : Network::WifiNetworkStatus::Available)
                    : Network::WifiNetworkStatus::Saved;
                break;
            }

            updateWifiStatus(
                Result<Network::WifiStatus, std::string>::okay(
                    Network::WifiStatus{ .connected = false, .ssid = "" }));
            if (!networks_.empty()) {
                updateNetworkDisplay(
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
            }
            if (!hasEventStreamConnection()) {
                refresh();
            }
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
            if (!networks_.empty()) {
                updateNetworkDisplay(
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
            }
            if (!hasEventStreamConnection()) {
                refresh();
            }
        }
    }

    if (scanRequestResult.has_value()) {
        if (scanRequestResult->isError()) {
            scanInProgress_ = false;
            LOG_WARN(Controls, "WiFi scan request failed: {}", scanRequestResult->errorValue());
            setWifiStatusMessage("Wi-Fi scan failed", ERROR_TEXT_COLOR);
        }
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
    bool connectCancelInProgress = false;
    bool diagnosticsModeUpdateInProgress = false;
    bool scanRequestInProgress = false;
    bool scannerEnterInProgress = false;
    bool scannerExitInProgress = false;
    bool scannerSnapshotInProgress = false;
    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        connectCancelInProgress = asyncState_->connectCancelInProgress;
        diagnosticsModeUpdateInProgress = asyncState_->diagnosticsModeUpdateInProgress;
        refreshInProgress = asyncState_->refreshInProgress;
        scanRequestInProgress = asyncState_->scanRequestInProgress;
        scannerEnterInProgress = asyncState_->scannerEnterInProgress;
        scannerExitInProgress = asyncState_->scannerExitInProgress;
        scannerSnapshotInProgress = asyncState_->scannerSnapshotInProgress;
        hasPending = asyncState_->pendingRefresh.has_value()
            || asyncState_->pendingConnectCancel.has_value()
            || asyncState_->pendingConnect.has_value() || asyncState_->pendingDisconnect.has_value()
            || asyncState_->pendingDiagnosticsModeUpdate.has_value()
            || asyncState_->pendingForget.has_value()
            || asyncState_->pendingScannerEnter.has_value()
            || asyncState_->pendingScannerExit.has_value()
            || asyncState_->pendingScannerSnapshot.has_value()
            || asyncState_->pendingScanRequest.has_value()
            || asyncState_->pendingWebSocketUpdate.has_value()
            || asyncState_->pendingWebUiUpdate.has_value() || asyncState_->connectCancelInProgress
            || asyncState_->diagnosticsModeUpdateInProgress || asyncState_->scannerEnterInProgress
            || asyncState_->scannerExitInProgress || asyncState_->scannerSnapshotInProgress
            || asyncState_->webSocketUpdateInProgress || asyncState_->webUiUpdateInProgress;
    }

    setRefreshButtonEnabled(
        !scanInProgress_ && !refreshInProgress && !scanRequestInProgress && !connectCancelInProgress
        && !diagnosticsModeUpdateInProgress && !scannerEnterInProgress && !scannerExitInProgress
        && !isActionInProgress() && !hasPending);
    setScannerRefreshButtonEnabled(
        !refreshInProgress && !scannerEnterInProgress && !scannerExitInProgress
        && !scannerSnapshotInProgress && !hasPending);
}

void NetworkDiagnosticsPanel::onRefreshTimer(lv_timer_t* timer)
{
    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    self->applyPendingUpdates();
    self->updateSignalHistory();
    self->updateConnectOverlay();
    self->updateScannerSnapshotPolling();
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

    ctx->panel->openConnectingOverlay(network);
    if (!ctx->panel->startAsyncConnect(network)) {
        ctx->panel->closePasswordPrompt();
    }
}

void NetworkDiagnosticsPanel::onNetworkDetailsClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* row = lv_event_get_current_target(e);
    auto* target = lv_event_get_target(e);
    if (!row || target != row) {
        return;
    }

    NetworkDetailsContext* ctx = static_cast<NetworkDetailsContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->panel || ctx->panel->isActionInProgress()) {
        return;
    }

    if (ctx->index >= ctx->panel->networks_.size()) {
        return;
    }

    ctx->panel->openNetworkDetailsOverlay(ctx->panel->networks_[ctx->index]);
}

void NetworkDiagnosticsPanel::onNetworkDetailsCloseClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->closeNetworkDetailsOverlay();
}

void NetworkDiagnosticsPanel::onNetworkDetailsForgetClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->networkDetailsNetwork_.has_value()) {
        return;
    }

    const Network::WifiNetworkInfo network = self->networkDetailsNetwork_.value();
    if (self->isActionInProgress()) {
        return;
    }

    self->closeNetworkDetailsOverlay();
    self->startAsyncForget(network);
}

void NetworkDiagnosticsPanel::onNetworkDetailsUpdatePasswordClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->networkDetailsNetwork_.has_value() || self->isActionInProgress()) {
        return;
    }

    const Network::WifiNetworkInfo network = self->networkDetailsNetwork_.value();
    self->closeNetworkDetailsOverlay();
    self->openPasswordPrompt(network);
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

void NetworkDiagnosticsPanel::onDisconnectClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    DisconnectContext* ctx = static_cast<DisconnectContext*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->panel) {
        return;
    }

    if (ctx->index >= ctx->panel->networks_.size()) {
        return;
    }

    ctx->panel->startAsyncDisconnect(ctx->panel->networks_[ctx->index]);
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

void NetworkDiagnosticsPanel::onConnectProgressCancelClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    if (self->startAsyncConnectCancel() && self->connectProgressCancelButton_) {
        setActionButtonEnabled(self->connectProgressCancelButton_, false);
        setActionButtonText(self->connectProgressCancelButton_, "Canceling");
    }
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

void NetworkDiagnosticsPanel::onLiveScanToggleChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || self->liveScanToggleLocked_) {
        return;
    }

    if (!self->liveScanToggle_) {
        return;
    }

    const bool enabled = lv_obj_has_state(self->liveScanToggle_, LV_STATE_CHECKED);
    if (self->userSettingsManager_) {
        Api::UserSettingsPatch::Command patchCmd{ .networkLiveScanPreferred = enabled };
        self->userSettingsManager_->patchOrAssert(patchCmd, 1000);
        self->liveScanEnabled_ = self->userSettingsManager_->get().networkLiveScanPreferred;
    }

    self->setLiveScanToggleEnabled(false);
    if (!self->startAsyncDiagnosticsModeSet(self->liveScanEnabled_)) {
        self->setLiveScanToggleEnabled(true);
        self->liveScanToggleLocked_ = true;
        if (self->liveScanEnabled_) {
            lv_obj_add_state(self->liveScanToggle_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(self->liveScanToggle_, LV_STATE_CHECKED);
        }
        self->liveScanToggleLocked_ = false;
    }
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
