#include "NetworkDiagnosticsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <arpa/inet.h>
#include <exception>
#include <ifaddrs.h>
#include <net/if.h>
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
} // namespace

NetworkDiagnosticsPanel::NetworkDiagnosticsPanel(lv_obj_t* container)
    : container_(container), asyncState_(std::make_shared<AsyncState>())
{
    createUI();
    LOG_INFO(Controls, "NetworkDiagnosticsPanel created");
}

NetworkDiagnosticsPanel::~NetworkDiagnosticsPanel()
{
    if (refreshTimer_) {
        lv_timer_delete(refreshTimer_);
        refreshTimer_ = nullptr;
    }
    LOG_INFO(Controls, "NetworkDiagnosticsPanel destroyed");
}

void NetworkDiagnosticsPanel::createUI()
{
    // Title.
    lv_obj_t* title = lv_label_create(container_);
    lv_label_set_text(title, "Network");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(title, LV_PCT(100));

    lv_obj_t* contentRow = lv_obj_create(container_);
    lv_obj_set_size(contentRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(contentRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        contentRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(contentRow, 0, 0);
    lv_obj_set_style_pad_column(contentRow, 16, 0);
    lv_obj_set_style_bg_opa(contentRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentRow, 0, 0);
    lv_obj_clear_flag(contentRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* leftColumn = lv_obj_create(contentRow);
    lv_obj_set_size(leftColumn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(leftColumn, 3);
    lv_obj_set_flex_flow(leftColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        leftColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(leftColumn, 0, 0);
    lv_obj_set_style_pad_row(leftColumn, 6, 0);
    lv_obj_set_style_bg_opa(leftColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftColumn, 0, 0);
    lv_obj_clear_flag(leftColumn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* rightColumn = lv_obj_create(contentRow);
    lv_obj_set_size(rightColumn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(rightColumn, 2);
    lv_obj_set_flex_flow(rightColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        rightColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(rightColumn, 0, 0);
    lv_obj_set_style_pad_row(rightColumn, 8, 0);
    lv_obj_set_style_bg_opa(rightColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightColumn, 0, 0);
    lv_obj_clear_flag(rightColumn, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi status label.
    wifiStatusLabel_ = lv_label_create(leftColumn);
    lv_obj_set_style_text_font(wifiStatusLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifiStatusLabel_, lv_color_hex(0x00CED1), 0);
    lv_obj_set_width(wifiStatusLabel_, LV_PCT(100));
    lv_label_set_long_mode(wifiStatusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(wifiStatusLabel_, 8, 0);

    // Networks section header.
    lv_obj_t* networksHeader = lv_label_create(leftColumn);
    lv_label_set_text(networksHeader, "Networks");
    lv_obj_set_style_text_font(networksHeader, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(networksHeader, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_top(networksHeader, 16, 0);

    // Networks list container.
    networksContainer_ = lv_obj_create(leftColumn);
    lv_obj_set_size(networksContainer_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(networksContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        networksContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(networksContainer_, 0, 0);
    lv_obj_set_style_pad_row(networksContainer_, 8, 0);
    lv_obj_set_style_bg_opa(networksContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(networksContainer_, 0, 0);

    // IP Address section header.
    lv_obj_t* ipHeader = lv_label_create(leftColumn);
    lv_label_set_text(ipHeader, "IP Address:");
    lv_obj_set_style_text_font(ipHeader, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ipHeader, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_top(ipHeader, 16, 0);

    // Address display label (will be updated with actual addresses).
    addressLabel_ = lv_label_create(leftColumn);
    lv_obj_set_style_text_font(addressLabel_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(addressLabel_, lv_color_hex(0x00CED1), 0);
    lv_obj_set_width(addressLabel_, LV_PCT(100));
    lv_label_set_long_mode(addressLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(addressLabel_, 8, 0);

    webUiToggle_ = LVGLBuilder::labeledSwitch(rightColumn)
                       .label("LAN Web UI")
                       .initialState(false)
                       .callback(onWebUiToggleChanged, this)
                       .buildOrLog();

    if (webUiToggle_) {
        lv_obj_set_style_pad_top(webUiToggle_, 8, 0);
    }

    webSocketToggle_ = LVGLBuilder::labeledSwitch(rightColumn)
                           .label("Incoming WebSocket Traffic")
                           .initialState(false)
                           .callback(onWebSocketToggleChanged, this)
                           .buildOrLog();

    if (webSocketToggle_) {
        lv_obj_set_style_pad_top(webSocketToggle_, 12, 0);
    }

    webSocketTokenTitleLabel_ = lv_label_create(rightColumn);
    lv_obj_set_style_text_font(webSocketTokenTitleLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(webSocketTokenTitleLabel_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(webSocketTokenTitleLabel_, LV_PCT(100));
    lv_label_set_long_mode(webSocketTokenTitleLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(webSocketTokenTitleLabel_, 12, 0);
    lv_label_set_text(webSocketTokenTitleLabel_, "WebSocket token");

    webSocketTokenLabel_ = lv_label_create(rightColumn);
    lv_obj_set_style_text_font(webSocketTokenLabel_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(webSocketTokenLabel_, lv_color_hex(0x00CED1), 0);
    lv_obj_set_width(webSocketTokenLabel_, LV_PCT(100));
    lv_label_set_long_mode(webSocketTokenLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(webSocketTokenLabel_, 4, 0);
    lv_label_set_text(webSocketTokenLabel_, "--");

    // Refresh button.
    refreshButton_ = LVGLBuilder::actionButton(leftColumn)
                         .text("Refresh")
                         .icon(LV_SYMBOL_REFRESH)
                         .mode(LVGLBuilder::ActionMode::Push)
                         .width(LV_PCT(95))
                         .callback(onRefreshClicked, this)
                         .buildOrLog();

    if (refreshButton_) {
        lv_obj_set_style_pad_top(refreshButton_, 16, 0);
    }

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

    // Initial display update.
    refresh();
}

void NetworkDiagnosticsPanel::refresh()
{
    updateAddressDisplay();
    setLoadingState();
    startAsyncRefresh();
}

void NetworkDiagnosticsPanel::updateAddressDisplay()
{
    std::vector<NetworkInterfaceInfo> addresses = getLocalAddresses();

    if (addresses.empty()) {
        lv_label_set_text(addressLabel_, "No network");
        return;
    }

    // Build display string with all addresses.
    std::string displayText;
    for (const auto& info : addresses) {
        if (!displayText.empty()) {
            displayText += "\n";
        }
        displayText += info.name + ": " + info.address;
    }

    lv_label_set_text(addressLabel_, displayText.c_str());
    LOG_DEBUG(Controls, "Network addresses updated: {}", displayText);
}

void NetworkDiagnosticsPanel::setLoadingState()
{
    if (wifiStatusLabel_) {
        lv_label_set_text(wifiStatusLabel_, "WiFi: checking...");
    }

    if (networksContainer_) {
        lv_obj_clean(networksContainer_);
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, "Scanning networks...");
        lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }

    setRefreshButtonEnabled(false);
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

    if (enabled) {
        lv_obj_clear_state(webUiToggle_, LV_STATE_DISABLED);
    }
    else {
        lv_obj_add_state(webUiToggle_, LV_STATE_DISABLED);
    }
}

void NetworkDiagnosticsPanel::setWebSocketToggleEnabled(bool enabled)
{
    if (!webSocketToggle_) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(webSocketToggle_, LV_STATE_DISABLED);
    }
    else {
        lv_obj_add_state(webSocketToggle_, LV_STATE_DISABLED);
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
            Network::WifiManager wifiManager;
            data.statusResult = wifiManager.getStatus();
            data.listResult = wifiManager.listNetworks();

            auto fetchAccessStatus = []() -> Result<NetworkAccessStatus, std::string> {
                Network::WebSocketService client;
                const std::string address = "ws://localhost:9090";
                auto connectResult = client.connect(address, 2000);
                if (connectResult.isError()) {
                    return Result<NetworkAccessStatus, std::string>::error(
                        "Failed to connect to os-manager: " + connectResult.errorValue());
                }

                OsApi::SystemStatus::Command cmd{};
                auto response =
                    client.sendCommandAndGetResponse<OsApi::SystemStatus::Okay>(cmd, 2000);
                client.disconnect();

                if (response.isError()) {
                    return Result<NetworkAccessStatus, std::string>::error(
                        "SystemStatus failed: " + response.errorValue());
                }

                const auto inner = response.value();
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

            data.accessStatusResult = fetchAccessStatus();
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

void NetworkDiagnosticsPanel::startAsyncConnect(const Network::WifiNetworkInfo& network)
{
    Network::WifiNetworkInfo networkCopy = network;
    if (!beginAsyncAction(AsyncActionKind::Connect, networkCopy, "connecting to")) {
        return;
    }

    auto state = asyncState_;
    std::thread([state, networkCopy]() {
        Result<Network::WifiConnectResult, std::string> result =
            Result<Network::WifiConnectResult, std::string>::error("WiFi connect failed");
        try {
            Network::WifiManager wifiManager;
            result = wifiManager.connect(networkCopy);
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
    if (!beginAsyncAction(AsyncActionKind::Forget, networkCopy, "forgetting")) {
        return;
    }

    auto state = asyncState_;
    std::thread([state, networkCopy]() {
        Result<Network::WifiForgetResult, std::string> result =
            Result<Network::WifiForgetResult, std::string>::error("WiFi forget failed");
        try {
            Network::WifiManager wifiManager;
            result = wifiManager.forget(networkCopy.ssid);
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

    if (wifiStatusLabel_) {
        std::string text = "WiFi: " + verb;
        if (!network.ssid.empty()) {
            text += " " + network.ssid;
        }
        lv_label_set_text(wifiStatusLabel_, text.c_str());
    }

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

void NetworkDiagnosticsPanel::updateWifiStatus(
    const Result<Network::WifiStatus, std::string>& statusResult)
{
    if (!wifiStatusLabel_) {
        return;
    }

    if (statusResult.isError()) {
        lv_label_set_text(wifiStatusLabel_, "WiFi: unavailable");
        LOG_WARN(Controls, "WiFi status failed: {}", statusResult.errorValue());
        return;
    }

    const auto& status = statusResult.value();
    if (!status.connected || status.ssid.empty()) {
        lv_label_set_text(wifiStatusLabel_, "WiFi: disconnected");
        return;
    }

    const std::string text = "WiFi: " + status.ssid;
    lv_label_set_text(wifiStatusLabel_, text.c_str());
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

std::string NetworkDiagnosticsPanel::statusText(const Network::WifiNetworkInfo& info) const
{
    switch (info.status) {
        case Network::WifiNetworkStatus::Connected:
            return "connected";
        case Network::WifiNetworkStatus::Open:
            return "open";
        case Network::WifiNetworkStatus::Saved:
        default:
            return "saved";
    }
}

std::string NetworkDiagnosticsPanel::formatNetworkDetails(
    const Network::WifiNetworkInfo& info) const
{
    const std::string status = statusText(info);
    const std::string signal =
        info.signalDbm.has_value() ? std::to_string(info.signalDbm.value()) + " dBm" : "--";
    const std::string security = info.security.empty() ? "unknown" : info.security;

    std::string lastUsed = info.lastUsedRelative.empty() ? "n/a" : info.lastUsedRelative;
    if (info.lastUsedDate.has_value() && !info.lastUsedDate.value().empty()) {
        lastUsed = info.lastUsedDate.value() + " (" + lastUsed + ")";
    }

    return status + " | " + signal + " | " + security + " | " + lastUsed;
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
        std::string text = "WiFi unavailable: " + listResult.errorValue();
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF6666), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        return;
    }

    networks_ = listResult.value();
    if (networks_.empty()) {
        lv_obj_t* label = lv_label_create(networksContainer_);
        lv_label_set_text(label, "No saved or open networks");
        lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        return;
    }

    for (size_t i = 0; i < networks_.size(); ++i) {
        const auto& network = networks_[i];

        lv_obj_t* row = lv_obj_create(networksContainer_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x404040), 0);
        lv_obj_set_style_radius(row, 6, 0);

        lv_obj_t* textColumn = lv_obj_create(row);
        lv_obj_set_size(textColumn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(textColumn, 1);
        lv_obj_set_flex_flow(textColumn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(textColumn, 0, 0);
        lv_obj_set_style_bg_opa(textColumn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textColumn, 0, 0);

        lv_obj_t* ssidLabel = lv_label_create(textColumn);
        lv_label_set_text(ssidLabel, network.ssid.c_str());
        lv_obj_set_style_text_font(ssidLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(
            ssidLabel,
            network.status == Network::WifiNetworkStatus::Connected
                ? lv_color_hex(0x00FF7F)
                : (network.status == Network::WifiNetworkStatus::Open ? lv_color_hex(0x00CED1)
                                                                      : lv_color_hex(0xFFFFFF)),
            0);
        lv_label_set_long_mode(ssidLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ssidLabel, LV_PCT(100));

        const std::string details = formatNetworkDetails(network);
        lv_obj_t* detailsLabel = lv_label_create(textColumn);
        lv_label_set_text(detailsLabel, details.c_str());
        lv_obj_set_style_text_font(detailsLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(detailsLabel, lv_color_hex(0xAAAAAA), 0);
        lv_label_set_long_mode(detailsLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(detailsLabel, LV_PCT(100));

        lv_obj_t* buttonColumn = lv_obj_create(row);
        lv_obj_set_size(buttonColumn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(buttonColumn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            buttonColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(buttonColumn, 0, 0);
        lv_obj_set_style_pad_row(buttonColumn, 6, 0);
        lv_obj_set_style_bg_opa(buttonColumn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(buttonColumn, 0, 0);

        const bool isConnecting =
            actionState_.kind == AsyncActionKind::Connect && network.ssid == actionState_.ssid;
        const bool isForgetting =
            actionState_.kind == AsyncActionKind::Forget && network.ssid == actionState_.ssid;
        const bool actionsDisabled = isActionInProgress();
        const bool canForget = network.autoConnect || network.hasCredentials;

        std::string buttonText = "Connect";
        if (network.status == Network::WifiNetworkStatus::Open) {
            buttonText = "Join";
        }
        else if (network.status == Network::WifiNetworkStatus::Connected) {
            buttonText = "Connected";
        }
        else if (isConnecting) {
            buttonText = "Connecting";
        }

        auto context = std::make_unique<ConnectContext>();
        context->panel = this;
        context->index = i;
        connectContexts_.push_back(std::move(context));
        ConnectContext* contextPtr = connectContexts_.back().get();

        lv_obj_t* buttonContainer = LVGLBuilder::actionButton(buttonColumn)
                                        .text(buttonText.c_str())
                                        .mode(LVGLBuilder::ActionMode::Push)
                                        .width(90)
                                        .height(60)
                                        .callback(onConnectClicked, contextPtr)
                                        .buildOrLog();

        if (buttonContainer) {
            lv_obj_t* button = lv_obj_get_child(buttonContainer, 0);
            if (button
                && (network.status == Network::WifiNetworkStatus::Connected || actionsDisabled)) {
                lv_obj_add_state(button, LV_STATE_DISABLED);
            }
        }

        if (canForget) {
            auto forgetContext = std::make_unique<ForgetContext>();
            forgetContext->panel = this;
            forgetContext->index = i;
            forgetContexts_.push_back(std::move(forgetContext));
            ForgetContext* forgetContextPtr = forgetContexts_.back().get();

            const char* forgetText = isForgetting ? "Forgetting" : "Forget";
            lv_obj_t* forgetContainer = LVGLBuilder::actionButton(buttonColumn)
                                            .text(forgetText)
                                            .mode(LVGLBuilder::ActionMode::Push)
                                            .width(90)
                                            .height(48)
                                            .callback(onForgetClicked, forgetContextPtr)
                                            .buildOrLog();

            if (forgetContainer) {
                lv_obj_t* button = lv_obj_get_child(forgetContainer, 0);
                if (button && actionsDisabled) {
                    lv_obj_add_state(button, LV_STATE_DISABLED);
                }
            }
        }
    }
}

std::vector<NetworkInterfaceInfo> NetworkDiagnosticsPanel::getLocalAddresses()
{
    std::vector<NetworkInterfaceInfo> result;

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        LOG_WARN(Controls, "Failed to get network interfaces: {}", strerror(errno));
        return result;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        // Only interested in IPv4.
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // Skip loopback interfaces.
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        // Skip interfaces that are down.
        if ((ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }

        // Get the address string.
        char addrBuf[INET_ADDRSTRLEN];
        struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sa->sin_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
            result.push_back({ ifa->ifa_name, addrBuf });
            LOG_DEBUG(Controls, "Found interface {}: {}", ifa->ifa_name, addrBuf);
        }
    }

    freeifaddrs(ifaddr);
    return result;
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
        endAsyncAction(AsyncActionKind::Connect);
        if (connectResult->isError()) {
            LOG_WARN(Controls, "WiFi connect failed: {}", connectResult->errorValue());
            if (wifiStatusLabel_) {
                lv_label_set_text(wifiStatusLabel_, "WiFi: connect failed");
            }
            if (!networks_.empty()) {
                updateNetworkDisplay(
                    Result<std::vector<Network::WifiNetworkInfo>, std::string>::okay(networks_));
            }
        }
        else {
            LOG_INFO(Controls, "WiFi connect requested for {}", connectResult->value().ssid);
            refresh();
        }
    }

    if (forgetResult.has_value()) {
        endAsyncAction(AsyncActionKind::Forget);
        if (forgetResult->isError()) {
            LOG_WARN(Controls, "WiFi forget failed: {}", forgetResult->errorValue());
            if (wifiStatusLabel_) {
                lv_label_set_text(wifiStatusLabel_, "WiFi: forget failed");
            }
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

    ctx->panel->startAsyncConnect(ctx->panel->networks_[ctx->index]);
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
