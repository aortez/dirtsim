#include "NetworkDiagnosticsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/ClientHello.h"
#include "core/network/WebSocketService.h"
#include "os-manager/api/NetworkDiagnosticsModeSet.h"
#include "os-manager/api/NetworkSnapshotChanged.h"
#include "os-manager/api/NetworkSnapshotGet.h"
#include "os-manager/api/ScannerConfigGet.h"
#include "os-manager/api/ScannerConfigSet.h"
#include "os-manager/api/ScannerModeEnter.h"
#include "os-manager/api/ScannerModeExit.h"
#include "os-manager/api/ScannerSnapshotChanged.h"
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
#include "ui/widgets/ScannerChannelMapWidget.h"
#include "ui/widgets/TimeSeriesPlotWidget.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
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

lv_obj_t* getActionDropdownWidget(lv_obj_t* container)
{
    if (!container) {
        return nullptr;
    }

    return static_cast<lv_obj_t*>(lv_obj_get_user_data(container));
}

void setActionDropdownOptions(lv_obj_t* container, const std::string& options)
{
    auto* dropdown = getActionDropdownWidget(container);
    if (!dropdown) {
        return;
    }

    lv_dropdown_set_options(dropdown, options.c_str());
}

void styleScannerDropdownPopup(lv_obj_t* container)
{
    constexpr int popupLineSpace = 28;
    constexpr int popupPadVertical = 28;

    auto* dropdown = getActionDropdownWidget(container);
    if (!dropdown) {
        return;
    }

    auto* list = lv_dropdown_get_list(dropdown);
    if (!list) {
        return;
    }

    lv_obj_set_style_pad_top(list, popupPadVertical, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list, popupPadVertical, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(list, popupLineSpace, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(list, popupLineSpace, LV_PART_SELECTED);

    auto* label = lv_obj_get_child(list, 0);
    if (label) {
        lv_obj_set_style_text_line_space(label, popupLineSpace, LV_PART_MAIN);
    }
}

void setControlEnabled(lv_obj_t* control, const bool enabled)
{
    if (!control) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(control, LV_STATE_DISABLED);
        return;
    }

    lv_obj_add_state(control, LV_STATE_DISABLED);
}

std::vector<int> scannerWidthChoicesForBand(const OsManager::ScannerBand band)
{
    if (band == OsManager::ScannerBand::Band24Ghz) {
        return { 20 };
    }

    return { 20, 40, 80 };
}

void normalizeScannerConfig(OsManager::ScannerConfig& config)
{
    if (!scannerWidthSupported(config.autoConfig.band, config.autoConfig.widthMhz)) {
        config.autoConfig.widthMhz = scannerDefaultWidthMhz(config.autoConfig.band);
    }

    if (!scannerWidthSupported(config.manualConfig.band, config.manualConfig.widthMhz)) {
        config.manualConfig.widthMhz = scannerDefaultWidthMhz(config.manualConfig.band);
    }

    const auto targetChannels =
        scannerManualTargetChannels(config.manualConfig.band, config.manualConfig.widthMhz);
    if (targetChannels.empty()) {
        return;
    }

    const auto targetIt =
        std::find(targetChannels.begin(), targetChannels.end(), config.manualConfig.targetChannel);
    if (targetIt != targetChannels.end()) {
        return;
    }

    config.manualConfig.targetChannel = targetChannels.front();
}

bool scannerConfigsEqual(const OsManager::ScannerConfig& lhs, const OsManager::ScannerConfig& rhs)
{
    return lhs.mode == rhs.mode && lhs.autoConfig.band == rhs.autoConfig.band
        && lhs.autoConfig.widthMhz == rhs.autoConfig.widthMhz
        && lhs.manualConfig.band == rhs.manualConfig.band
        && lhs.manualConfig.widthMhz == rhs.manualConfig.widthMhz
        && lhs.manualConfig.targetChannel == rhs.manualConfig.targetChannel;
}

bool scannerTuningsEqual(const OsManager::ScannerTuning& lhs, const OsManager::ScannerTuning& rhs)
{
    return lhs.band == rhs.band && lhs.primaryChannel == rhs.primaryChannel
        && lhs.widthMhz == rhs.widthMhz && lhs.centerChannel == rhs.centerChannel;
}

std::optional<int> scannerManualPrimaryChannel(const OsManager::ScannerConfig& config)
{
    const auto tuningResult = scannerManualTargetToTuning(config.manualConfig);
    if (!tuningResult.isValue()) {
        return std::nullopt;
    }

    return tuningResult.value().primaryChannel;
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
constexpr int NETWORK_SCANNER_CONFIG_DROPDOWN_WIDTH = 132;
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
constexpr int NETWORK_SCANNER_LIST_AGE_WIDTH = 72;
constexpr int NETWORK_SCANNER_LIST_CHANNEL_WIDTH = 40;
constexpr int NETWORK_SCANNER_LIST_RSSI_WIDTH = 56;
constexpr int NETWORK_SCANNER_MAP_HEIGHT = 116;
constexpr int NETWORK_SCANNER_SQUARE_BUTTON_SIZE = LVGLBuilder::Style::ACTION_SIZE;
constexpr uint64_t NETWORK_SCANNER_ROW_STALE_AGE_MS = 5000;
constexpr float NETWORK_SCANNER_RSSI_SMOOTHING_ALPHA = 0.3f;
constexpr float NETWORK_SCANNER_RSSI_SWAP_THRESHOLD_DB = 5.0f;
constexpr int NETWORK_SCANNER_SWAP_CONFIRM_UPDATES = 2;
constexpr int NETWORK_STAGE_BADGE_HEIGHT = 34;
constexpr int NETWORK_SIGNAL_HISTORY_MAX_SAMPLES = 60;
constexpr int WIFI_LIST_COLUMN_GROW = 7;
constexpr int WIFI_SUMMARY_COLUMN_GROW = 4;
constexpr int WIFI_SUMMARY_MIN_WIDTH = 220;
constexpr const char* OS_MANAGER_ADDRESS = "ws://localhost:9090";

const std::array<const char*, 4> kConnectStageTitles = {
    "1 Join", "2 Associate", "3 Auth", "4 Address"
};

int scannerSignalBucketDbm(const int signalDbm)
{
    constexpr int minSignalDbm = -90;
    constexpr int maxSignalDbm = -30;
    constexpr int bucketSizeDb = 4;

    const int clampedSignal = std::clamp(signalDbm, minSignalDbm, maxSignalDbm);
    const int bucketBase =
        minSignalDbm + ((clampedSignal - minSignalDbm) / bucketSizeDb) * bucketSizeDb;
    return std::min(bucketBase + bucketSizeDb / 2, maxSignalDbm);
}

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

std::string formatCompactAge(const std::optional<uint64_t> ageMs)
{
    if (!ageMs.has_value()) {
        return "--";
    }

    if (ageMs.value() < 1000) {
        return std::to_string(ageMs.value()) + "ms";
    }

    const uint64_t ageSeconds = ageMs.value() / 1000;
    if (ageSeconds < 60) {
        return std::to_string(ageSeconds) + "s";
    }

    const uint64_t ageMinutes = ageSeconds / 60;
    if (ageMinutes < 60) {
        return std::to_string(ageMinutes) + "m";
    }

    const uint64_t ageHours = ageMinutes / 60;
    return std::to_string(ageHours) + "h";
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

void setObjectVisible(lv_obj_t* object, const bool visible)
{
    if (!object) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
        return;
    }

    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
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

    connectFlowView_ = lv_obj_create(pagesContainer_);
    stylePanelColumn(connectFlowView_);
    lv_obj_set_size(connectFlowView_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(connectFlowView_, 1);

    networkDetailsView_ = lv_obj_create(pagesContainer_);
    stylePanelColumn(networkDetailsView_);
    lv_obj_set_size(networkDetailsView_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(networkDetailsView_, 1);

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

    lv_obj_clear_flag(scannerView_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* scannerHeaderRow = lv_obj_create(scannerView_);
    lv_obj_set_size(scannerHeaderRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scannerHeaderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        scannerHeaderRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scannerHeaderRow, 0, 0);
    lv_obj_set_style_pad_column(scannerHeaderRow, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_style_bg_opa(scannerHeaderRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scannerHeaderRow, 0, 0);
    lv_obj_clear_flag(scannerHeaderRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* scannerStatusColumn = lv_obj_create(scannerHeaderRow);
    lv_obj_set_size(scannerStatusColumn, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(scannerStatusColumn, 1);
    lv_obj_set_flex_flow(scannerStatusColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        scannerStatusColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scannerStatusColumn, 0, 0);
    lv_obj_set_style_bg_opa(scannerStatusColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scannerStatusColumn, 0, 0);
    lv_obj_clear_flag(scannerStatusColumn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* scannerTitleLabel = lv_label_create(scannerStatusColumn);
    lv_label_set_text(scannerTitleLabel, "Scanner");
    lv_obj_set_style_text_font(scannerTitleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(scannerTitleLabel, lv_color_hex(HEADER_TEXT_COLOR), 0);
    lv_obj_set_width(scannerTitleLabel, LV_PCT(100));

    scannerStatusLabel_ = lv_label_create(scannerStatusColumn);
    lv_label_set_text(scannerStatusLabel_, "Scanner status unavailable.");
    lv_obj_set_style_text_font(scannerStatusLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scannerStatusLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(scannerStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(scannerStatusLabel_, LV_PCT(100));

    lv_obj_t* scannerControlsRow = lv_obj_create(scannerHeaderRow);
    lv_obj_set_size(scannerControlsRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scannerControlsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        scannerControlsRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scannerControlsRow, 0, 0);
    lv_obj_set_style_pad_column(scannerControlsRow, 8, 0);
    lv_obj_set_style_bg_opa(scannerControlsRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scannerControlsRow, 0, 0);
    lv_obj_clear_flag(scannerControlsRow, LV_OBJ_FLAG_SCROLLABLE);

    scannerAutoButton_ = LVGLBuilder::actionButton(scannerControlsRow)
                             .text("Auto")
                             .mode(LVGLBuilder::ActionMode::Push)
                             .size(NETWORK_SCANNER_SQUARE_BUTTON_SIZE)
                             .callback(onScannerAutoClicked, this)
                             .buildOrLog();

    scannerBandDropdown_ = LVGLBuilder::actionDropdown(scannerControlsRow)
                               .options("2.4 GHz\n5 GHz")
                               .selected(1)
                               .width(NETWORK_SCANNER_CONFIG_DROPDOWN_WIDTH)
                               .callback(onScannerBandChanged, this)
                               .buildOrLog();
    styleScannerDropdownPopup(scannerBandDropdown_);

    scannerWidthDropdown_ = LVGLBuilder::actionDropdown(scannerControlsRow)
                                .options("20 MHz")
                                .selected(0)
                                .width(NETWORK_SCANNER_CONFIG_DROPDOWN_WIDTH)
                                .callback(onScannerWidthChanged, this)
                                .buildOrLog();
    styleScannerDropdownPopup(scannerWidthDropdown_);

    scannerEnterButton_ = LVGLBuilder::actionButton(scannerControlsRow)
                              .text("Scan")
                              .mode(LVGLBuilder::ActionMode::Push)
                              .size(NETWORK_SCANNER_SQUARE_BUTTON_SIZE)
                              .callback(onScannerEnterClicked, this)
                              .buildOrLog();

    scannerExitButton_ = LVGLBuilder::actionButton(scannerControlsRow)
                             .text("Wi-Fi")
                             .mode(LVGLBuilder::ActionMode::Push)
                             .size(NETWORK_SCANNER_SQUARE_BUTTON_SIZE)
                             .callback(onScannerExitClicked, this)
                             .buildOrLog();

    scannerRefreshButton_ = LVGLBuilder::actionButton(scannerControlsRow)
                                .text("Retry")
                                .mode(LVGLBuilder::ActionMode::Push)
                                .size(NETWORK_SCANNER_SQUARE_BUTTON_SIZE)
                                .callback(onScannerRefreshClicked, this)
                                .buildOrLog();

    scannerChannelMap_ = std::make_unique<ScannerChannelMapWidget>(scannerView_);
    lv_obj_set_size(scannerChannelMap_->getContainer(), LV_PCT(100), NETWORK_SCANNER_MAP_HEIGHT);
    scannerChannelMap_->setChannelSelectedCallback(
        [this](const int channel) { onScannerChannelSelected(channel); });

    lv_obj_t* scannerRadiosHeaderRow = lv_obj_create(scannerView_);
    lv_obj_set_size(scannerRadiosHeaderRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scannerRadiosHeaderRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        scannerRadiosHeaderRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scannerRadiosHeaderRow, 0, 0);
    lv_obj_set_style_pad_column(scannerRadiosHeaderRow, 8, 0);
    lv_obj_set_style_bg_opa(scannerRadiosHeaderRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scannerRadiosHeaderRow, 0, 0);
    lv_obj_clear_flag(scannerRadiosHeaderRow, LV_OBJ_FLAG_SCROLLABLE);

    scannerRadiosHeaderLabel_ = lv_label_create(scannerRadiosHeaderRow);
    lv_label_set_text(scannerRadiosHeaderLabel_, "Observed radios");
    lv_obj_set_size(scannerRadiosHeaderLabel_, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(scannerRadiosHeaderLabel_, 1);
    lv_obj_set_style_text_font(scannerRadiosHeaderLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scannerRadiosHeaderLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(scannerRadiosHeaderLabel_, LV_LABEL_LONG_DOT);

    auto createScannerColumnLabel = [scannerRadiosHeaderRow](const char* text, const int width) {
        lv_obj_t* label = lv_label_create(scannerRadiosHeaderRow);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, width);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(MUTED_TEXT_COLOR), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        return label;
    };

    createScannerColumnLabel("Ch", NETWORK_SCANNER_LIST_CHANNEL_WIDTH);
    createScannerColumnLabel("RSSI", NETWORK_SCANNER_LIST_RSSI_WIDTH);
    createScannerColumnLabel("Age", NETWORK_SCANNER_LIST_AGE_WIDTH);

    scannerRadiosList_ = lv_obj_create(scannerView_);
    lv_obj_set_size(scannerRadiosList_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(scannerRadiosList_, 1);
    lv_obj_set_flex_flow(scannerRadiosList_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        scannerRadiosList_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(scannerRadiosList_, 0, 0);
    lv_obj_set_style_pad_row(scannerRadiosList_, 8, 0);
    lv_obj_set_style_bg_opa(scannerRadiosList_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scannerRadiosList_, 0, 0);
    lv_obj_set_scroll_dir(scannerRadiosList_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scannerRadiosList_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(scannerRadiosList_, onScannerRadiosListScroll, LV_EVENT_SCROLL_BEGIN, this);
    lv_obj_add_event_cb(scannerRadiosList_, onScannerRadiosListScroll, LV_EVENT_SCROLL_END, this);

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
    updateScannerChannelMap();
    updateScannerRadioList();
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
            state.screen = AutomationScreen::Wifi;
            state.viewMode = "Wifi";
            break;
        case ViewMode::WifiConnectFlow:
            state.screen = connectOverlayMode_ == ConnectOverlayMode::PasswordEntry
                ? AutomationScreen::WifiPassword
                : AutomationScreen::WifiConnecting;
            state.viewMode = "Wifi";
            break;
        case ViewMode::WifiDetails:
            state.screen = AutomationScreen::WifiDetails;
            state.viewMode = "Wifi";
            break;
        case ViewMode::LanAccess:
            state.screen = AutomationScreen::LanAccess;
            state.viewMode = "LanAccess";
            break;
        case ViewMode::Scanner:
            state.screen = AutomationScreen::Scanner;
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
    const bool connectFlowVisible = viewMode_ == ViewMode::WifiConnectFlow && connectFlowView_
        && !lv_obj_has_flag(connectFlowView_, LV_OBJ_FLAG_HIDDEN);
    state.passwordPromptVisible = connectFlowVisible
        && connectOverlayMode_ == ConnectOverlayMode::PasswordEntry
        && connectOverlayHasPasswordEntry_;
    state.connectOverlayVisible =
        connectFlowVisible && connectOverlayMode_ == ConnectOverlayMode::Connecting;
    state.passwordSubmitEnabled = passwordJoinButton_
        && !lv_obj_has_state(getActionButtonInnerButton(passwordJoinButton_), LV_STATE_DISABLED);
    state.scannerModeActive = scannerModeActive_;
    state.scannerModeAvailable = scannerModeAvailable_;

    if (scannerEnterButton_) {
        const lv_obj_t* innerButton = getActionButtonInnerButton(scannerEnterButton_);
        state.scannerEnterEnabled = !lv_obj_has_flag(scannerEnterButton_, LV_OBJ_FLAG_HIDDEN)
            && innerButton
            && !lv_obj_has_state(const_cast<lv_obj_t*>(innerButton), LV_STATE_DISABLED);
    }
    if (scannerExitButton_) {
        const lv_obj_t* innerButton = getActionButtonInnerButton(scannerExitButton_);
        state.scannerExitEnabled = !lv_obj_has_flag(scannerExitButton_, LV_OBJ_FLAG_HIDDEN)
            && innerButton
            && !lv_obj_has_state(const_cast<lv_obj_t*>(innerButton), LV_STATE_DISABLED);
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
    const bool wasWifiMode = previousMode == ViewMode::Wifi
        || previousMode == ViewMode::WifiConnectFlow || previousMode == ViewMode::WifiDetails;
    const bool isWifiMode = mode == ViewMode::Wifi || mode == ViewMode::WifiConnectFlow
        || mode == ViewMode::WifiDetails;
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
    setVisibility(connectFlowView_, mode == ViewMode::WifiConnectFlow);
    setVisibility(networkDetailsView_, mode == ViewMode::WifiDetails);
    setVisibility(lanAccessView_, mode == ViewMode::LanAccess);
    setVisibility(scannerView_, mode == ViewMode::Scanner);
    viewMode_ = mode;
    if (!isWifiMode) {
        signalHistoryByBssid_.clear();
        signalHistoryLastSampleAt_.reset();
        return;
    }

    if (!wasWifiMode) {
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
            if (messageType == OsApi::ScannerSnapshotChanged::name()) {
                try {
                    const auto changed =
                        Network::deserialize_payload<OsApi::ScannerSnapshotChanged>(payload);
                    ScannerSnapshot snapshot;
                    snapshot.active = changed.snapshot.active;
                    snapshot.requestedConfig = changed.snapshot.requestedConfig;
                    snapshot.appliedConfig = changed.snapshot.appliedConfig;
                    snapshot.currentTuning = changed.snapshot.currentTuning;
                    snapshot.detail = changed.snapshot.detail;
                    snapshot.radios.reserve(changed.snapshot.radios.size());
                    for (const auto& radio : changed.snapshot.radios) {
                        ScannerObservedRadio entry;
                        entry.bssid = radio.bssid;
                        entry.ssid = radio.ssid;
                        entry.signalDbm = radio.signalDbm;
                        entry.channel = radio.channel;
                        entry.lastSeenAgeMs = radio.lastSeenAgeMs;
                        entry.observationKind = radio.observationKind;
                        snapshot.radios.push_back(std::move(entry));
                    }

                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->pendingScannerSnapshot =
                        Result<ScannerSnapshot, ScannerSnapshotError>::okay(std::move(snapshot));
                }
                catch (const std::exception& e) {
                    LOG_ERROR(
                        Controls, "Failed to deserialize ScannerSnapshotChanged: {}", e.what());
                }
                return;
            }

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
    if (connectFlowView_) {
        lv_obj_clean(connectFlowView_);
        if (viewMode_ == ViewMode::WifiConnectFlow) {
            setViewMode(ViewMode::Wifi);
        }
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
    if (networkDetailsView_) {
        lv_obj_clean(networkDetailsView_);
        if (viewMode_ == ViewMode::WifiDetails) {
            setViewMode(ViewMode::Wifi);
        }
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

    if (!connectFlowView_) {
        return;
    }

    setViewMode(ViewMode::WifiConnectFlow);
    lv_obj_clean(connectFlowView_);

    lv_obj_t* modal = lv_obj_create(connectFlowView_);
    lv_obj_set_size(modal, LV_PCT(100), 0);
    lv_obj_set_flex_grow(modal, 1);
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

    if (!networkDetailsView_) {
        return;
    }

    setViewMode(ViewMode::WifiDetails);
    lv_obj_clean(networkDetailsView_);

    networkDetailsOverlay_ = lv_obj_create(networkDetailsView_);
    lv_obj_set_size(networkDetailsOverlay_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(networkDetailsOverlay_, 1);
    lv_obj_set_style_bg_color(networkDetailsOverlay_, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(networkDetailsOverlay_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(networkDetailsOverlay_, 0, 0);
    lv_obj_set_style_radius(networkDetailsOverlay_, 0, 0);
    lv_obj_set_style_pad_all(networkDetailsOverlay_, NETWORK_CARD_PADDING, 0);
    lv_obj_set_style_pad_row(networkDetailsOverlay_, NETWORK_CARD_ROW_PADDING, 0);
    lv_obj_set_flex_flow(networkDetailsOverlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        networkDetailsOverlay_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(networkDetailsOverlay_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* modal = networkDetailsOverlay_;

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
        if (scannerModeActive_) {
            startAsyncScannerSnapshot();
        }
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

bool NetworkDiagnosticsPanel::isScannerSnapshotStale() const
{
    if (!scannerModeActive_ || scannerActionInProgress_ || scannerStatusUnavailable_
        || !scannerSnapshotErrorMessage_.empty() || !scannerSnapshotActivityAt_.has_value()) {
        return false;
    }

    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - scannerSnapshotActivityAt_.value());
    return age.count() > 5000;
}

void NetworkDiagnosticsPanel::resetScannerSnapshotState()
{
    scannerAppliedConfig_.reset();
    scannerSnapshotActivityAt_.reset();
    scannerCurrentTuning_.reset();
    scannerObservedRadioCount_ = 0;
    scannerObservedRadios_.clear();
    scannerRadioOrder_.clear();
    scannerRadioRowsByKey_.clear();
    scannerRenderedBand_.reset();
    scannerSnapshotErrorMessage_.clear();
    scannerSnapshotReceived_ = false;
    scannerSnapshotStale_ = false;
    scannerRadiosListScrolling_ = false;
}

void NetworkDiagnosticsPanel::clearScannerRadioRows()
{
    scannerRadioOrder_.clear();
    scannerRadioRowsByKey_.clear();
    scannerRenderedBand_.reset();
    if (scannerRadiosList_) {
        lv_obj_clean(scannerRadiosList_);
    }
}

bool NetworkDiagnosticsPanel::isScannerConfigRequestInFlight() const
{
    if (!asyncState_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(asyncState_->mutex);
    return asyncState_->scannerConfigSetInProgress;
}

std::optional<OsManager::ScannerTuning> NetworkDiagnosticsPanel::scannerAppliedManualTuning() const
{
    if (!scannerAppliedConfig_.has_value()
        || scannerAppliedConfig_->mode != OsManager::ScannerConfigMode::Manual) {
        return std::nullopt;
    }

    const auto tuningResult = scannerManualTargetToTuning(scannerAppliedConfig_->manualConfig);
    if (tuningResult.isError()) {
        return std::nullopt;
    }

    return tuningResult.value();
}

std::optional<OsManager::ScannerTuning> NetworkDiagnosticsPanel::scannerRequestedManualTuning()
    const
{
    if (scannerConfig_.mode != OsManager::ScannerConfigMode::Manual) {
        return std::nullopt;
    }

    const auto tuningResult = scannerManualTargetToTuning(scannerConfig_.manualConfig);
    if (tuningResult.isError()) {
        return std::nullopt;
    }

    return tuningResult.value();
}

bool NetworkDiagnosticsPanel::isScannerManualRetunePending() const
{
    if (!scannerModeActive_ || scannerConfig_.mode != OsManager::ScannerConfigMode::Manual) {
        return false;
    }

    const auto requestedTuning = scannerRequestedManualTuning();
    if (!requestedTuning.has_value()) {
        return false;
    }

    if (const auto appliedTuning = scannerAppliedManualTuning(); appliedTuning.has_value()) {
        return !scannerTuningsEqual(appliedTuning.value(), requestedTuning.value());
    }

    if (scannerCurrentTuning_.has_value()) {
        return !scannerTuningsEqual(scannerCurrentTuning_.value(), requestedTuning.value());
    }

    return false;
}

std::optional<OsManager::ScannerTuning> NetworkDiagnosticsPanel::scannerDisplayedManualTuning()
    const
{
    if (scannerConfig_.mode != OsManager::ScannerConfigMode::Manual) {
        return std::nullopt;
    }

    if (scannerModeActive_) {
        if (scannerCurrentTuning_.has_value()) {
            return scannerCurrentTuning_;
        }

        if (const auto appliedTuning = scannerAppliedManualTuning(); appliedTuning.has_value()) {
            return appliedTuning;
        }
    }

    return scannerRequestedManualTuning();
}

NetworkDiagnosticsPanel::ScannerBand NetworkDiagnosticsPanel::scannerDisplayedBand() const
{
    if (const auto displayTuning = scannerDisplayedManualTuning(); displayTuning.has_value()) {
        return displayTuning->band;
    }

    return scannerConfigBand(scannerConfig_);
}

std::string NetworkDiagnosticsPanel::scannerRadioIdentity(const ScannerObservedRadio& radio) const
{
    if (!radio.bssid.empty()) {
        return radio.bssid;
    }

    std::string identity = radio.ssid.empty() ? "<hidden>" : radio.ssid;
    identity += "|";
    identity += radio.channel.has_value() ? std::to_string(radio.channel.value()) : "--";
    return identity;
}

bool NetworkDiagnosticsPanel::scannerRadioMatchesSelectedBand(
    const ScannerObservedRadio& radio) const
{
    if (!radio.channel.has_value()) {
        return false;
    }

    const int channel = radio.channel.value();
    switch (scannerDisplayedBand()) {
        case ScannerBand::Band24Ghz:
            return channel >= 1 && channel <= 14;
        case ScannerBand::Band5Ghz:
            return channel > 14;
    }

    return false;
}

std::string NetworkDiagnosticsPanel::scannerSelectedBandLabel() const
{
    switch (scannerDisplayedBand()) {
        case ScannerBand::Band24Ghz:
            return "2.4 GHz";
        case ScannerBand::Band5Ghz:
            return "5 GHz";
    }

    return "Selected";
}

int NetworkDiagnosticsPanel::scannerSelectedWidthMhz() const
{
    if (const auto displayTuning = scannerDisplayedManualTuning(); displayTuning.has_value()) {
        return displayTuning->widthMhz;
    }

    return scannerConfigWidthMhz(scannerConfig_);
}

void NetworkDiagnosticsPanel::applyScannerConfigChange(OsManager::ScannerConfig nextConfig)
{
    normalizeScannerConfig(nextConfig);
    if (scannerConfigsEqual(nextConfig, scannerConfig_)) {
        updateScannerConfigControls();
        updateScannerControls();
        return;
    }

    if (!startAsyncScannerConfigSet(nextConfig)) {
        updateScannerConfigControls();
        updateScannerControls();
        return;
    }

    ++scannerConfigRefreshToken_;
    scannerConfig_ = nextConfig;
    updateScannerConfigControls();
    updateScannerStatusLabel();
    updateScannerChannelMap();
    updateScannerRadioList();
    updateScannerControls();
}

void NetworkDiagnosticsPanel::onScannerChannelSelected(const int channel)
{
    if (scannerActionInProgress_ || scannerConfigSetInProgress_ || scannerStatusUnavailable_
        || !scannerModeAvailable_) {
        return;
    }

    auto nextTarget = scannerManualTargetForPrimaryChannel(
        scannerConfigBand(scannerConfig_), scannerConfigWidthMhz(scannerConfig_), channel);
    if (!nextTarget.has_value()) {
        return;
    }

    OsManager::ScannerConfig nextConfig = scannerConfig_;
    if (scannerConfig_.mode == OsManager::ScannerConfigMode::Auto) {
        nextConfig.manualConfig.band = scannerConfig_.autoConfig.band;
        nextConfig.manualConfig.widthMhz = scannerConfig_.autoConfig.widthMhz;
    }
    nextConfig.mode = OsManager::ScannerConfigMode::Manual;
    nextConfig.manualConfig.band = scannerConfigBand(scannerConfig_);
    nextConfig.manualConfig.widthMhz = scannerConfigWidthMhz(scannerConfig_);
    nextConfig.manualConfig.targetChannel = nextTarget.value();
    applyScannerConfigChange(nextConfig);
}

void NetworkDiagnosticsPanel::updateScannerRadioRowDisplay(
    ScannerRadioRowState& rowState, const ScannerObservedRadio& radio)
{
    rowState.radio = radio;

    if (radio.signalDbm.has_value()) {
        const float signal = static_cast<float>(radio.signalDbm.value());
        if (!rowState.hasSmoothedSignal) {
            rowState.smoothedSignalDbm = signal;
            rowState.hasSmoothedSignal = true;
        }
        else {
            rowState.smoothedSignalDbm =
                (1.0f - NETWORK_SCANNER_RSSI_SMOOTHING_ALPHA) * rowState.smoothedSignalDbm
                + NETWORK_SCANNER_RSSI_SMOOTHING_ALPHA * signal;
        }
    }

    const bool rowIsStale = radio.lastSeenAgeMs.has_value()
        && radio.lastSeenAgeMs.value() > NETWORK_SCANNER_ROW_STALE_AGE_MS;
    const uint32_t primaryTextColor = rowIsStale ? MUTED_TEXT_COLOR : 0xFFFFFF;
    const uint32_t secondaryTextColor = rowIsStale ? HEADER_TEXT_COLOR : 0xFFFFFF;
    const std::string ageText = formatCompactAge(radio.lastSeenAgeMs);
    const std::string channelText =
        radio.channel.has_value() ? std::to_string(radio.channel.value()) : "--";
    const std::string rssiText =
        radio.signalDbm.has_value() ? std::to_string(radio.signalDbm.value()) : "--";
    const std::string ssid = radio.ssid.empty() ? "<hidden>" : radio.ssid;

    lv_label_set_text(rowState.ssidLabel, ssid.c_str());
    lv_label_set_text(rowState.channelLabel, channelText.c_str());
    lv_label_set_text(rowState.rssiLabel, rssiText.c_str());
    lv_label_set_text(rowState.ageLabel, ageText.c_str());

    lv_obj_set_style_text_color(rowState.ssidLabel, lv_color_hex(primaryTextColor), 0);
    lv_obj_set_style_text_color(rowState.channelLabel, lv_color_hex(secondaryTextColor), 0);
    lv_obj_set_style_text_color(rowState.rssiLabel, lv_color_hex(secondaryTextColor), 0);
    lv_obj_set_style_text_color(rowState.ageLabel, lv_color_hex(secondaryTextColor), 0);
    lv_obj_set_style_bg_opa(rowState.row, rowIsStale ? LV_OPA_70 : LV_OPA_COVER, 0);
}

void NetworkDiagnosticsPanel::updateScannerRadioRowOrder()
{
    if (scannerRadiosListScrolling_) {
        return;
    }

    for (size_t i = 1; i < scannerRadioOrder_.size(); ++i) {
        const std::string& upperKey = scannerRadioOrder_[i - 1];
        const std::string& lowerKey = scannerRadioOrder_[i];
        auto upperIt = scannerRadioRowsByKey_.find(upperKey);
        auto lowerIt = scannerRadioRowsByKey_.find(lowerKey);
        if (upperIt == scannerRadioRowsByKey_.end() || lowerIt == scannerRadioRowsByKey_.end()) {
            continue;
        }

        ScannerRadioRowState& upperRow = upperIt->second;
        ScannerRadioRowState& lowerRow = lowerIt->second;
        if (lowerRow.smoothedSignalDbm
            <= upperRow.smoothedSignalDbm + NETWORK_SCANNER_RSSI_SWAP_THRESHOLD_DB) {
            lowerRow.pendingSwapAboveKey.clear();
            lowerRow.pendingSwapAboveUpdates = 0;
            continue;
        }

        if (lowerRow.pendingSwapAboveKey != upperKey) {
            lowerRow.pendingSwapAboveKey = upperKey;
            lowerRow.pendingSwapAboveUpdates = 1;
            continue;
        }

        lowerRow.pendingSwapAboveUpdates += 1;
        if (lowerRow.pendingSwapAboveUpdates < NETWORK_SCANNER_SWAP_CONFIRM_UPDATES) {
            continue;
        }

        std::swap(scannerRadioOrder_[i - 1], scannerRadioOrder_[i]);
        lowerRow.pendingSwapAboveKey.clear();
        lowerRow.pendingSwapAboveUpdates = 0;
        upperRow.pendingSwapAboveKey.clear();
        upperRow.pendingSwapAboveUpdates = 0;
    }

    for (size_t i = 0; i < scannerRadioOrder_.size(); ++i) {
        auto it = scannerRadioRowsByKey_.find(scannerRadioOrder_[i]);
        if (it == scannerRadioRowsByKey_.end() || !it->second.row) {
            continue;
        }

        lv_obj_move_to_index(it->second.row, static_cast<int32_t>(i));
    }
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
    if (viewMode_ != ViewMode::WifiConnectFlow || !passwordPromptNetwork_.has_value()) {
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
    closePasswordPrompt();

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
    const uint64_t scannerConfigRefreshToken = scannerConfigRefreshToken_;
    std::thread([state, forceRefresh, scannerConfigRefreshToken]() {
        PendingRefreshData data;
        data.scannerConfigRefreshToken = scannerConfigRefreshToken;
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

                if (!data.accessStatusResult.isError()
                    && data.accessStatusResult.value().scannerModeAvailable) {
                    OsApi::ScannerConfigGet::Command scannerConfigCmd{};
                    const auto scannerConfigResponse =
                        client.sendCommandAndGetResponse<OsApi::ScannerConfigGet::Okay>(
                            scannerConfigCmd, 2000);
                    if (scannerConfigResponse.isError()) {
                        data.scannerConfigResult =
                            Result<OsManager::ScannerConfig, std::string>::error(
                                "ScannerConfigGet failed: " + scannerConfigResponse.errorValue());
                    }
                    else if (scannerConfigResponse.value().isError()) {
                        data.scannerConfigResult =
                            Result<OsManager::ScannerConfig, std::string>::error(
                                "ScannerConfigGet failed: "
                                + scannerConfigResponse.value().errorValue().message);
                    }
                    else {
                        data.scannerConfigResult =
                            Result<OsManager::ScannerConfig, std::string>::okay(
                                scannerConfigResponse.value().value().config);
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

bool NetworkDiagnosticsPanel::startAsyncScannerConfigSet(const OsManager::ScannerConfig& config)
{
    if (!asyncState_) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(asyncState_->mutex);
        if (asyncState_->scannerConfigSetInProgress) {
            return false;
        }
        asyncState_->scannerConfigSetInProgress = true;
    }

    scannerConfigSetInProgress_ = true;
    updateScannerConfigControls();
    updateScannerControls();

    auto state = asyncState_;
    std::thread([state, config]() {
        Result<OsManager::ScannerConfig, std::string> result =
            Result<OsManager::ScannerConfig, std::string>::error("Scanner config update failed");
        try {
            Network::WebSocketService client;
            const auto connectResult = client.connect(OS_MANAGER_ADDRESS, 2000);
            if (connectResult.isError()) {
                result = Result<OsManager::ScannerConfig, std::string>::error(
                    "Failed to connect to os-manager: " + connectResult.errorValue());
            }
            else {
                OsApi::ScannerConfigSet::Command cmd{ .config = config };
                const auto response =
                    client.sendCommandAndGetResponse<OsApi::ScannerConfigSet::Okay>(cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    result = Result<OsManager::ScannerConfig, std::string>::error(
                        "ScannerConfigSet failed: " + response.errorValue());
                }
                else if (response.value().isError()) {
                    result = Result<OsManager::ScannerConfig, std::string>::error(
                        "ScannerConfigSet failed: " + response.value().errorValue().message);
                }
                else {
                    result = Result<OsManager::ScannerConfig, std::string>::okay(
                        response.value().value().config);
                }
            }
        }
        catch (const std::exception& e) {
            result = Result<OsManager::ScannerConfig, std::string>::error(e.what());
        }
        catch (...) {
            result = Result<OsManager::ScannerConfig, std::string>::error(
                "Scanner config update failed");
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingScannerConfigSet = std::move(result);
        state->scannerConfigSetInProgress = false;
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
    scannerStatusUnavailable_ = false;
    resetScannerSnapshotState();
    if (scannerStatusLabel_) {
        lv_label_set_text(scannerStatusLabel_, "Entering scanner mode...");
    }
    if (scannerChannelMap_) {
        scannerChannelMap_->clear();
    }
    updateScannerChannelMap();
    updateScannerRadioList();
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
                    client.sendCommandAndGetResponse<OsApi::ScannerModeEnter::Okay>(cmd, 20000);
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
    scannerStatusUnavailable_ = false;
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

    scannerSnapshotActivityAt_ = std::chrono::steady_clock::now();
    scannerSnapshotErrorMessage_.clear();
    scannerSnapshotStale_ = false;
    updateScannerControls();

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
                    snapshot.requestedConfig = okay.requestedConfig;
                    snapshot.appliedConfig = okay.appliedConfig;
                    snapshot.currentTuning = okay.currentTuning;
                    snapshot.detail = okay.detail;
                    snapshot.radios.reserve(okay.radios.size());
                    for (const auto& radio : okay.radios) {
                        ScannerObservedRadio entry;
                        entry.bssid = radio.bssid;
                        entry.ssid = radio.ssid;
                        entry.signalDbm = radio.signalDbm;
                        entry.channel = radio.channel;
                        entry.lastSeenAgeMs = radio.lastSeenAgeMs;
                        entry.observationKind = radio.observationKind;
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
        scannerStatusUnavailable_ = true;
        scannerModeDetail_ = statusResult.errorValue();
        updateScannerStatusLabel();
        if (!scannerSnapshotReceived_ && scannerChannelMap_) {
            scannerChannelMap_->clear();
        }
        updateScannerConfigControls();
        updateScannerChannelMap();
        updateScannerRadioList();
        updateScannerControls();
        return;
    }

    const auto& status = statusResult.value();
    scannerStatusUnavailable_ = false;
    scannerModeAvailable_ = status.scannerModeAvailable;
    scannerModeActive_ = status.scannerModeActive;
    scannerModeDetail_ = status.scannerModeDetail;

    if (!scannerModeActive_) {
        scannerConfigSetInProgress_ = false;
        resetScannerSnapshotState();
        if (scannerChannelMap_) {
            scannerChannelMap_->clear();
        }
    }

    updateScannerConfigControls();
    updateScannerStatusLabel();
    updateScannerChannelMap();
    updateScannerRadioList();
    updateScannerControls();
}

void NetworkDiagnosticsPanel::updateScannerSnapshot(
    const Result<ScannerSnapshot, ScannerSnapshotError>& result)
{
    if (result.isError()) {
        scannerSnapshotErrorMessage_ = "Scanner data unavailable: " + result.errorValue().message;
        scannerSnapshotStale_ = false;
        updateScannerStatusLabel();
        updateScannerControls();
        if (!scannerSnapshotReceived_ && scannerChannelMap_) {
            scannerChannelMap_->clear();
        }
        updateScannerChannelMap();
        updateScannerRadioList();
        return;
    }

    const auto& snapshot = result.value();
    const bool scannerModeActiveChanged = scannerModeActive_ != snapshot.active;
    scannerModeActive_ = snapshot.active;
    scannerStatusUnavailable_ = false;
    scannerSnapshotActivityAt_ = std::chrono::steady_clock::now();
    scannerSnapshotErrorMessage_.clear();
    scannerSnapshotStale_ = false;
    scannerAppliedConfig_ = snapshot.appliedConfig;
    if (!scannerConfigSetInProgress_) {
        const bool configChanged = !scannerConfigsEqual(scannerConfig_, snapshot.requestedConfig);
        scannerConfig_ = snapshot.requestedConfig;
        if (configChanged) {
            ++scannerConfigRefreshToken_;
            updateScannerConfigControls();
        }
    }
    if (!snapshot.detail.empty()) {
        scannerModeDetail_ = snapshot.detail;
    }

    if (!scannerModeActive_) {
        scannerConfigSetInProgress_ = false;
        resetScannerSnapshotState();
        if (scannerChannelMap_) {
            scannerChannelMap_->clear();
        }
        updateScannerConfigControls();
        updateScannerStatusLabel();
        updateScannerChannelMap();
        updateScannerRadioList();
        updateScannerControls();
        if (scannerModeActiveChanged) {
            refresh();
        }
        return;
    }

    scannerCurrentTuning_ = snapshot.currentTuning;
    scannerObservedRadioCount_ = snapshot.radios.size();
    scannerObservedRadios_ = snapshot.radios;
    scannerSnapshotReceived_ = true;
    updateScannerStatusLabel();
    updateScannerChannelMap();
    updateScannerRadioList();
    updateScannerControls();

    if (scannerModeActiveChanged) {
        refresh();
    }
}

void NetworkDiagnosticsPanel::updateScannerConfigControls()
{
    const ScannerBand selectedBand = scannerConfigBand(scannerConfig_);
    const int selectedWidthMhz = scannerConfigWidthMhz(scannerConfig_);
    const bool controlsEnabled = scannerModeAvailable_ && !scannerActionInProgress_
        && !scannerStatusUnavailable_ && !scannerConfigSetInProgress_;

    if (scannerBandDropdown_) {
        LVGLBuilder::ActionDropdownBuilder::setSelected(
            scannerBandDropdown_, selectedBand == ScannerBand::Band24Ghz ? 0 : 1);
        setControlEnabled(scannerBandDropdown_, controlsEnabled);
        setControlEnabled(getActionDropdownWidget(scannerBandDropdown_), controlsEnabled);
    }

    std::vector<std::string> widthLabels;
    std::vector<int> widthChoices;
    widthChoices.push_back(20);
    widthLabels.push_back("20 MHz");
    if (selectedBand == ScannerBand::Band5Ghz) {
        widthChoices.push_back(40);
        widthChoices.push_back(80);
        widthLabels.push_back("40 MHz");
        widthLabels.push_back("80 MHz");
    }

    if (scannerWidthDropdown_) {
        setActionDropdownOptions(scannerWidthDropdown_, joinTextParts(widthLabels, "\n"));
        size_t selectedWidthIndex = 0;
        for (size_t i = 0; i < widthChoices.size(); ++i) {
            if (widthChoices[i] == selectedWidthMhz) {
                selectedWidthIndex = i;
                break;
            }
        }
        LVGLBuilder::ActionDropdownBuilder::setSelected(
            scannerWidthDropdown_, static_cast<uint16_t>(selectedWidthIndex));
        setControlEnabled(scannerWidthDropdown_, controlsEnabled);
        setControlEnabled(getActionDropdownWidget(scannerWidthDropdown_), controlsEnabled);
    }
}

void NetworkDiagnosticsPanel::updateScannerControls()
{
    const bool controlsBusy = isActionInProgress() || scannerConfigSetInProgress_;
    const bool showEnterButton = !scannerModeActive_;
    const bool showExitButton = scannerModeActive_;
    const bool showConfigControls = scannerModeAvailable_ || scannerModeActive_;
    const bool showAutoButton =
        scannerConfig_.mode == OsManager::ScannerConfigMode::Manual && showConfigControls;
    const bool showRetryButton =
        scannerStatusUnavailable_ || !scannerSnapshotErrorMessage_.empty() || scannerSnapshotStale_;

    setObjectVisible(scannerAutoButton_, showAutoButton);
    setObjectVisible(scannerBandDropdown_, showConfigControls);
    setObjectVisible(scannerWidthDropdown_, showConfigControls);
    setObjectVisible(scannerEnterButton_, showEnterButton);
    setObjectVisible(scannerExitButton_, showExitButton);
    setObjectVisible(scannerRefreshButton_, showRetryButton);

    setActionButtonText(scannerRefreshButton_, "Retry");
    setActionButtonEnabled(scannerAutoButton_, showAutoButton && !controlsBusy);
    setActionButtonEnabled(
        scannerEnterButton_,
        showEnterButton && !scannerStatusUnavailable_ && !controlsBusy && scannerModeAvailable_);
    setActionButtonEnabled(scannerExitButton_, showExitButton && !controlsBusy);
    setActionButtonEnabled(scannerRefreshButton_, showRetryButton && !controlsBusy);
}

void NetworkDiagnosticsPanel::updateScannerChannelMap()
{
    if (!scannerChannelMap_) {
        return;
    }

    ScannerChannelMapWidget::Model model;
    model.band = scannerDisplayedBand();
    model.mode = scannerConfig_.mode;
    if (scannerConfig_.mode == OsManager::ScannerConfigMode::Manual) {
        const auto displayTuning = scannerDisplayedManualTuning();
        if (displayTuning.has_value() && displayTuning->band == model.band) {
            model.currentTuning = displayTuning;
        }
    }
    else if (scannerCurrentTuning_.has_value() && scannerCurrentTuning_->band == model.band) {
        model.currentTuning = scannerCurrentTuning_;
    }

    auto bubbleKindForObservation = [](const OsManager::ScannerObservationKind observationKind) {
        return observationKind == OsManager::ScannerObservationKind::Incidental
            ? ScannerChannelMapWidget::BubbleKind::Incidental
            : ScannerChannelMapWidget::BubbleKind::Direct;
    };
    auto mergeBubbleKinds = [](const ScannerChannelMapWidget::BubbleKind lhs,
                               const ScannerChannelMapWidget::BubbleKind rhs) {
        if (lhs == rhs) {
            return lhs;
        }

        return ScannerChannelMapWidget::BubbleKind::Mixed;
    };

    struct BubbleAccumulator {
        ScannerChannelMapWidget::Bubble bubble;
        int signalSumDbm = 0;
    };

    constexpr int bucketSizeDb = 4;
    constexpr int bucketLowerOffsetDb = bucketSizeDb / 2;
    constexpr int bucketUpperOffsetDb = (bucketSizeDb - 1) - bucketLowerOffsetDb;

    std::map<std::pair<int, int>, BubbleAccumulator> bubblesByKey;
    for (const auto& radio : scannerObservedRadios_) {
        if (!scannerRadioMatchesSelectedBand(radio) || !radio.channel.has_value()
            || !radio.signalDbm.has_value()) {
            continue;
        }

        const int signalDbm = radio.signalDbm.value();
        const int channel = radio.channel.value();
        const int bucketDbm = scannerSignalBucketDbm(signalDbm);
        const auto key = std::make_pair(channel, bucketDbm);
        const auto observationKind = bubbleKindForObservation(radio.observationKind);
        auto [it, inserted] = bubblesByKey.try_emplace(
            key,
            BubbleAccumulator{
                .bubble =
                    ScannerChannelMapWidget::Bubble{
                        .channel = channel,
                        .rssiDbm = signalDbm,
                        .count = 0,
                        .freshestAgeMs = radio.lastSeenAgeMs,
                        .kind = observationKind,
                    },
                .signalSumDbm = 0,
            });
        auto& accumulator = it->second;
        auto& bubble = accumulator.bubble;
        if (!inserted) {
            bubble.kind = mergeBubbleKinds(bubble.kind, observationKind);
            if (!bubble.freshestAgeMs.has_value()) {
                bubble.freshestAgeMs = radio.lastSeenAgeMs;
            }
            else if (radio.lastSeenAgeMs.has_value()) {
                bubble.freshestAgeMs =
                    std::min(bubble.freshestAgeMs.value(), radio.lastSeenAgeMs.value());
            }
        }

        accumulator.signalSumDbm += signalDbm;
        ++bubble.count;
        const int meanDbm = static_cast<int>(std::round(
            static_cast<double>(accumulator.signalSumDbm) / static_cast<double>(bubble.count)));
        bubble.rssiDbm =
            std::clamp(meanDbm, bucketDbm - bucketLowerOffsetDb, bucketDbm + bucketUpperOffsetDb);
    }

    model.bubbles.reserve(bubblesByKey.size());
    for (const auto& [key, accumulator] : bubblesByKey) {
        (void)key;
        model.bubbles.push_back(accumulator.bubble);
    }
    std::sort(
        model.bubbles.begin(),
        model.bubbles.end(),
        [](const ScannerChannelMapWidget::Bubble& lhs, const ScannerChannelMapWidget::Bubble& rhs) {
            if (lhs.channel != rhs.channel) {
                return lhs.channel < rhs.channel;
            }

            return lhs.rssiDbm > rhs.rssiDbm;
        });
    scannerChannelMap_->setModel(std::move(model));
}

void NetworkDiagnosticsPanel::updateScannerRadioList()
{
    const std::string bandLabel = scannerSelectedBandLabel();
    std::vector<ScannerObservedRadio> radios;
    radios.reserve(scannerObservedRadios_.size());
    for (const auto& radio : scannerObservedRadios_) {
        if (!scannerRadioMatchesSelectedBand(radio)) {
            continue;
        }

        radios.push_back(radio);
    }

    if (scannerRadiosHeaderLabel_) {
        std::string headerText = "Observed radios";
        if (scannerModeActive_) {
            headerText = bandLabel + " radios: " + std::to_string(radios.size());
            if (scannerObservedRadioCount_ != radios.size()) {
                headerText += " of " + std::to_string(scannerObservedRadioCount_);
            }
        }
        lv_label_set_text(scannerRadiosHeaderLabel_, headerText.c_str());
    }

    if (!scannerRadiosList_) {
        return;
    }

    auto createMessageLabel = [this](const std::string& text, const uint32_t color) {
        lv_obj_t* label = lv_label_create(scannerRadiosList_);
        lv_label_set_text(label, text.c_str());
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    };

    if (!scannerModeActive_) {
        clearScannerRadioRows();
        createMessageLabel("Enter scanner mode to view observed radios.", MUTED_TEXT_COLOR);
        return;
    }

    if (!scannerSnapshotReceived_) {
        clearScannerRadioRows();
        if (!scannerSnapshotErrorMessage_.empty()) {
            createMessageLabel(scannerSnapshotErrorMessage_, ERROR_TEXT_COLOR);
        }
        else if (scannerStatusUnavailable_) {
            createMessageLabel("Scanner data unavailable.", ERROR_TEXT_COLOR);
        }
        else {
            createMessageLabel("Waiting for scanner data...", MUTED_TEXT_COLOR);
        }
        return;
    }

    if (scannerObservedRadios_.empty()) {
        clearScannerRadioRows();
        createMessageLabel("No radios observed yet.", MUTED_TEXT_COLOR);
        return;
    }

    if (radios.empty()) {
        clearScannerRadioRows();
        createMessageLabel("No " + bandLabel + " radios observed yet.", MUTED_TEXT_COLOR);
        return;
    }

    if (scannerRenderedBand_ != scannerDisplayedBand()) {
        clearScannerRadioRows();
    }
    scannerRenderedBand_ = scannerDisplayedBand();

    std::unordered_set<std::string> currentKeys;
    currentKeys.reserve(radios.size());

    auto createValueLabel = [](lv_obj_t* parent, const int width) -> lv_obj_t* {
        lv_obj_t* label = lv_label_create(parent);
        lv_obj_set_width(label, width);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        return label;
    };

    for (const auto& radio : radios) {
        const std::string key = scannerRadioIdentity(radio);
        currentKeys.insert(key);

        auto rowIt = scannerRadioRowsByKey_.find(key);
        if (rowIt == scannerRadioRowsByKey_.end()) {
            ScannerRadioRowState rowState;
            rowState.row = lv_obj_create(scannerRadiosList_);
            lv_obj_set_size(rowState.row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(rowState.row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(
                rowState.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_hor(rowState.row, 10, 0);
            lv_obj_set_style_pad_ver(rowState.row, 6, 0);
            lv_obj_set_style_pad_column(rowState.row, 8, 0);
            lv_obj_set_style_bg_color(rowState.row, lv_color_hex(NETWORK_ROW_BG_COLOR), 0);
            lv_obj_set_style_bg_opa(rowState.row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(rowState.row, 1, 0);
            lv_obj_set_style_border_color(rowState.row, lv_color_hex(NETWORK_ROW_BORDER_COLOR), 0);
            lv_obj_set_style_radius(rowState.row, 8, 0);
            lv_obj_clear_flag(rowState.row, LV_OBJ_FLAG_SCROLLABLE);

            rowState.ssidLabel = lv_label_create(rowState.row);
            lv_obj_set_size(rowState.ssidLabel, 0, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(rowState.ssidLabel, 1);
            lv_obj_set_style_text_font(rowState.ssidLabel, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(rowState.ssidLabel, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_long_mode(rowState.ssidLabel, LV_LABEL_LONG_DOT);

            rowState.channelLabel =
                createValueLabel(rowState.row, NETWORK_SCANNER_LIST_CHANNEL_WIDTH);
            rowState.rssiLabel = createValueLabel(rowState.row, NETWORK_SCANNER_LIST_RSSI_WIDTH);
            rowState.ageLabel = createValueLabel(rowState.row, NETWORK_SCANNER_LIST_AGE_WIDTH);

            auto [insertedIt, inserted] = scannerRadioRowsByKey_.emplace(key, std::move(rowState));
            rowIt = insertedIt;
        }

        ScannerRadioRowState& rowState = rowIt->second;
        updateScannerRadioRowDisplay(rowState, radio);

        if (std::find(scannerRadioOrder_.begin(), scannerRadioOrder_.end(), key)
            == scannerRadioOrder_.end()) {
            if (scannerRadiosListScrolling_) {
                scannerRadioOrder_.push_back(key);
            }
            else {
                auto insertIt = scannerRadioOrder_.begin();
                for (; insertIt != scannerRadioOrder_.end(); ++insertIt) {
                    auto existingIt = scannerRadioRowsByKey_.find(*insertIt);
                    if (existingIt == scannerRadioRowsByKey_.end()) {
                        continue;
                    }
                    if (rowState.smoothedSignalDbm > existingIt->second.smoothedSignalDbm) {
                        break;
                    }
                }
                scannerRadioOrder_.insert(insertIt, key);
            }
        }
    }

    for (auto orderIt = scannerRadioOrder_.begin(); orderIt != scannerRadioOrder_.end();) {
        if (currentKeys.contains(*orderIt)) {
            ++orderIt;
            continue;
        }

        auto rowIt = scannerRadioRowsByKey_.find(*orderIt);
        if (rowIt != scannerRadioRowsByKey_.end() && rowIt->second.row) {
            lv_obj_del(rowIt->second.row);
            scannerRadioRowsByKey_.erase(rowIt);
        }
        orderIt = scannerRadioOrder_.erase(orderIt);
    }

    updateScannerRadioRowOrder();

    if (scannerRadiosListScrolling_) {
        for (size_t i = 0; i < scannerRadioOrder_.size(); ++i) {
            auto rowIt = scannerRadioRowsByKey_.find(scannerRadioOrder_[i]);
            if (rowIt == scannerRadioRowsByKey_.end() || !rowIt->second.row) {
                continue;
            }

            lv_obj_move_to_index(rowIt->second.row, static_cast<int32_t>(i));
        }
    }
}

void NetworkDiagnosticsPanel::updateScannerStaleState()
{
    const bool stale = isScannerSnapshotStale();
    if (stale && scannerConfigSetInProgress_) {
        scannerConfigSetInProgress_ = false;
    }
    if (stale == scannerSnapshotStale_) {
        return;
    }

    scannerSnapshotStale_ = stale;
    updateScannerStatusLabel();
    updateScannerControls();
}

void NetworkDiagnosticsPanel::updateScannerStatusLabel()
{
    if (!scannerStatusLabel_ || scannerActionInProgress_) {
        return;
    }

    std::string text;
    if (scannerModeActive_) {
        std::vector<std::string> parts{ scannerConfig_.mode == OsManager::ScannerConfigMode::Manual
                                            ? "Scanner fixed"
                                            : "Scanner active" };
        if (scannerConfig_.mode == OsManager::ScannerConfigMode::Manual) {
            if (const auto displayTuning = scannerDisplayedManualTuning();
                displayTuning.has_value()) {
                parts.push_back(scannerManualTargetShortLabel(
                    displayTuning->band,
                    displayTuning->widthMhz,
                    displayTuning->centerChannel.value_or(displayTuning->primaryChannel)));
                parts.push_back(std::to_string(displayTuning->widthMhz) + " MHz");
            }
            else {
                parts.push_back(scannerManualTargetShortLabel(
                    scannerConfig_.manualConfig.band,
                    scannerConfig_.manualConfig.widthMhz,
                    scannerConfig_.manualConfig.targetChannel));
                parts.push_back(std::to_string(scannerConfig_.manualConfig.widthMhz) + " MHz");
            }
        }
        else if (scannerCurrentTuning_.has_value()) {
            parts.push_back(
                "ch " + std::to_string(scannerCurrentTuning_->primaryChannel) + " @ "
                + std::to_string(scannerCurrentTuning_->widthMhz) + " MHz");
        }
        if (scannerSnapshotReceived_) {
            parts.push_back(std::to_string(scannerObservedRadioCount_) + " radios");
        }

        text = joinTextParts(parts, " • ");
        if (text.empty()) {
            text = "Scanner active.";
        }

        if (!scannerSnapshotErrorMessage_.empty()) {
            text += "\n" + scannerSnapshotErrorMessage_;
        }
        else if (scannerStatusUnavailable_) {
            text += "\n" + scannerModeDetail_;
        }
        else if (scannerSnapshotStale_) {
            text += "\nNo fresh scanner data for 5 seconds.";
        }
        else if (!scannerSnapshotReceived_) {
            text += "\nWaiting for scanner data.";
        }
        else {
            if (isScannerManualRetunePending()) {
                text += "\nRetuning to "
                    + scannerManualTargetShortLabel(
                            scannerConfig_.manualConfig.band,
                            scannerConfig_.manualConfig.widthMhz,
                            scannerConfig_.manualConfig.targetChannel)
                    + ".";
            }

            if (!scannerCurrentTuning_.has_value() && !scannerModeDetail_.empty()) {
                text += "\n" + scannerModeDetail_;
            }
        }
    }
    else if (scannerStatusUnavailable_) {
        text = "Scanner status unavailable.";
        if (!scannerModeDetail_.empty()) {
            text += "\n" + scannerModeDetail_;
        }
    }
    else if (scannerModeAvailable_) {
        text = "Scanner ready.";
        if (!scannerModeDetail_.empty()) {
            text += "\n" + scannerModeDetail_;
        }
        else {
            text += "\n" + scannerConfigSummaryLabel(scannerConfig_);
        }
    }
    else {
        text = "Scanner unavailable.";
        if (!scannerModeDetail_.empty()) {
            text += "\n" + scannerModeDetail_;
        }
    }

    lv_label_set_text(scannerStatusLabel_, text.c_str());
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
    if (viewMode_ != ViewMode::Wifi && viewMode_ != ViewMode::WifiDetails) {
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
        LOG_INFO(Controls, "Scanner retry requested by user");
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

void NetworkDiagnosticsPanel::onScannerAutoClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    OsManager::ScannerConfig nextConfig = self->scannerConfig_;
    if (nextConfig.mode != OsManager::ScannerConfigMode::Auto) {
        nextConfig.autoConfig.band = self->scannerConfig_.manualConfig.band;
        nextConfig.autoConfig.widthMhz = self->scannerConfig_.manualConfig.widthMhz;
    }
    nextConfig.mode = OsManager::ScannerConfigMode::Auto;
    self->applyScannerConfigChange(nextConfig);
}

void NetworkDiagnosticsPanel::onScannerBandChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->scannerBandDropdown_) {
        return;
    }

    const ScannerBand selectedBand =
        LVGLBuilder::ActionDropdownBuilder::getSelected(self->scannerBandDropdown_) == 0
        ? ScannerBand::Band24Ghz
        : ScannerBand::Band5Ghz;

    OsManager::ScannerConfig nextConfig = self->scannerConfig_;
    if (nextConfig.mode == OsManager::ScannerConfigMode::Manual) {
        const auto preferredPrimaryChannel = scannerManualPrimaryChannel(self->scannerConfig_);
        nextConfig.manualConfig.band = selectedBand;
        if (preferredPrimaryChannel.has_value()) {
            const auto nextTarget = scannerManualTargetForPrimaryChannel(
                nextConfig.manualConfig.band,
                nextConfig.manualConfig.widthMhz,
                preferredPrimaryChannel.value());
            if (nextTarget.has_value()) {
                nextConfig.manualConfig.targetChannel = nextTarget.value();
            }
        }
    }
    else {
        nextConfig.autoConfig.band = selectedBand;
    }
    self->applyScannerConfigChange(nextConfig);
}

void NetworkDiagnosticsPanel::onScannerWidthChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self || !self->scannerWidthDropdown_) {
        return;
    }

    const auto widthChoices = scannerWidthChoicesForBand(scannerConfigBand(self->scannerConfig_));
    const uint16_t selectedIndex =
        LVGLBuilder::ActionDropdownBuilder::getSelected(self->scannerWidthDropdown_);
    const int selectedWidthMhz =
        selectedIndex < widthChoices.size() ? widthChoices[selectedIndex] : widthChoices.front();

    OsManager::ScannerConfig nextConfig = self->scannerConfig_;
    if (nextConfig.mode == OsManager::ScannerConfigMode::Manual) {
        const auto preferredPrimaryChannel = scannerManualPrimaryChannel(self->scannerConfig_);
        nextConfig.manualConfig.widthMhz = selectedWidthMhz;
        if (preferredPrimaryChannel.has_value()) {
            const auto nextTarget = scannerManualTargetForPrimaryChannel(
                nextConfig.manualConfig.band,
                nextConfig.manualConfig.widthMhz,
                preferredPrimaryChannel.value());
            if (!nextTarget.has_value()) {
                self->updateScannerConfigControls();
                return;
            }

            nextConfig.manualConfig.targetChannel = nextTarget.value();
        }
    }
    else {
        nextConfig.autoConfig.widthMhz = selectedWidthMhz;
    }
    self->applyScannerConfigChange(nextConfig);
}

void NetworkDiagnosticsPanel::onScannerRadiosListScroll(lv_event_t* e)
{
    auto* self = static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    const lv_event_code_t eventCode = lv_event_get_code(e);
    if (eventCode == LV_EVENT_SCROLL_BEGIN) {
        self->scannerRadiosListScrolling_ = true;
        return;
    }

    if (eventCode == LV_EVENT_SCROLL_END) {
        self->scannerRadiosListScrolling_ = false;
        self->updateScannerRadioList();
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
    std::optional<Result<OsManager::ScannerConfig, std::string>> scannerConfigSetResult;
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

        scannerConfigSetResult = asyncState_->pendingScannerConfigSet;
        asyncState_->pendingScannerConfigSet.reset();

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
        if (!scannerConfigSetInProgress_
            && refreshData->scannerConfigRefreshToken == scannerConfigRefreshToken_
            && refreshData->scannerConfigResult.has_value()
            && !refreshData->scannerConfigResult->isError()) {
            scannerConfig_ = refreshData->scannerConfigResult->value();
            updateScannerConfigControls();
            updateScannerStatusLabel();
        }
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

        if (viewMode_ == ViewMode::WifiDetails && networkDetailsOverlay_
            && networkDetailsNetwork_.has_value()) {
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

    if (scannerConfigSetResult.has_value()) {
        scannerConfigSetInProgress_ = false;
        if (scannerConfigSetResult->isError()) {
            LOG_WARN(
                Controls, "Scanner config update failed: {}", scannerConfigSetResult->errorValue());
            refresh();
        }
        else {
            scannerConfig_ = scannerConfigSetResult->value();
        }
        updateScannerConfigControls();
        updateScannerStatusLabel();
        updateScannerChannelMap();
        updateScannerRadioList();
        updateScannerControls();
    }

    if (scannerEnterResult.has_value()) {
        scannerActionInProgress_ = false;
        if (scannerEnterResult->isError()) {
            LOG_WARN(Controls, "Scanner mode enter failed: {}", scannerEnterResult->errorValue());
            resetScannerSnapshotState();
            if (scannerStatusLabel_) {
                const std::string text =
                    "Failed to enter scanner mode.\n" + scannerEnterResult->errorValue();
                lv_label_set_text(scannerStatusLabel_, text.c_str());
            }
            updateScannerChannelMap();
            updateScannerRadioList();
            updateScannerControls();
        }
        else {
            LOG_INFO(Controls, "Scanner mode entered");
            scannerStatusUnavailable_ = false;
            scannerModeActive_ = true;
            updateScannerStatusLabel();
            updateScannerChannelMap();
            updateScannerRadioList();
            updateScannerControls();
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
            scannerStatusUnavailable_ = false;
            scannerModeActive_ = false;
            resetScannerSnapshotState();
            if (scannerChannelMap_) {
                scannerChannelMap_->clear();
            }
            updateScannerStatusLabel();
            updateScannerChannelMap();
            updateScannerRadioList();
            updateScannerControls();
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
    self->updateScannerStaleState();
    self->updateSignalHistory();
    self->updateConnectOverlay();
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
