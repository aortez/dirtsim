#pragma once

#include "core/network/WifiManager.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace DirtSim {
namespace Ui {

/**
 * @brief Network interface information for display.
 */
struct NetworkInterfaceInfo {
    std::string name;    // Interface name (e.g., "eth0", "wlan0").
    std::string address; // IPv4 address.
};

/**
 * @brief Panel displaying network diagnostics information.
 *
 * Shows the device's IP address(es) and other network status information.
 * Useful for remotely connecting to the device.
 */
class NetworkDiagnosticsPanel {
public:
    /**
     * @brief Construct the network diagnostics panel.
     * @param container Parent LVGL container to build UI in.
     */
    explicit NetworkDiagnosticsPanel(lv_obj_t* container);
    ~NetworkDiagnosticsPanel();

    /**
     * @brief Refresh the network information display.
     *
     * Call this to update the displayed IP addresses (e.g., if network
     * configuration changes).
     */
    void refresh();

    /**
     * @brief Get all non-loopback IPv4 addresses on the system.
     * @return Vector of interface info structs.
     */
    static std::vector<NetworkInterfaceInfo> getLocalAddresses();

private:
    lv_obj_t* container_;
    lv_obj_t* addressLabel_ = nullptr;
    lv_obj_t* refreshButton_ = nullptr;
    lv_obj_t* wifiStatusLabel_ = nullptr;
    lv_obj_t* networksContainer_ = nullptr;
    lv_obj_t* webUiToggle_ = nullptr;
    lv_obj_t* webUiTokenTitleLabel_ = nullptr;
    lv_obj_t* webUiTokenLabel_ = nullptr;
    lv_timer_t* refreshTimer_ = nullptr;

    struct ConnectContext {
        NetworkDiagnosticsPanel* panel = nullptr;
        size_t index = 0;
    };

    struct ForgetContext {
        NetworkDiagnosticsPanel* panel = nullptr;
        size_t index = 0;
    };

    enum class AsyncActionKind { None, Connect, Forget };

    struct AsyncActionState {
        AsyncActionKind kind = AsyncActionKind::None;
        std::string ssid;
    };

    struct WebUiStatus {
        bool enabled = false;
        std::string token;
    };

    struct WebUiAccessUpdate {
        bool enabled = false;
        std::string token;
    };

    struct PendingRefreshData {
        Result<Network::WifiStatus, std::string> statusResult;
        Result<std::vector<Network::WifiNetworkInfo>, std::string> listResult;
        Result<WebUiStatus, std::string> webUiStatusResult;
    };

    struct AsyncState {
        std::mutex mutex;
        bool refreshInProgress = false;
        std::optional<PendingRefreshData> pendingRefresh;
        std::optional<Result<Network::WifiConnectResult, std::string>> pendingConnect;
        std::optional<Result<Network::WifiForgetResult, std::string>> pendingForget;
        std::optional<Result<WebUiAccessUpdate, std::string>> pendingWebUiUpdate;
        bool webUiUpdateInProgress = false;
    };

    std::vector<Network::WifiNetworkInfo> networks_;
    std::vector<std::unique_ptr<ConnectContext>> connectContexts_;
    std::vector<std::unique_ptr<ForgetContext>> forgetContexts_;
    std::shared_ptr<AsyncState> asyncState_;
    AsyncActionState actionState_;
    bool webUiEnabled_ = false;
    std::string webUiToken_;
    bool webUiToggleLocked_ = false;

    void createUI();
    bool startAsyncRefresh();
    void startAsyncConnect(const Network::WifiNetworkInfo& network);
    void startAsyncForget(const Network::WifiNetworkInfo& network);
    bool beginAsyncAction(
        AsyncActionKind kind, const Network::WifiNetworkInfo& network, const std::string& verb);
    void endAsyncAction(AsyncActionKind kind);
    bool isActionInProgress() const;
    void applyPendingUpdates();
    void setLoadingState();
    void setRefreshButtonEnabled(bool enabled);
    void setWebUiToggleEnabled(bool enabled);
    void updateAddressDisplay();
    void updateNetworkDisplay(
        const Result<std::vector<Network::WifiNetworkInfo>, std::string>& listResult);
    void updateWifiStatus(const Result<Network::WifiStatus, std::string>& statusResult);
    void updateWebUiStatus(const Result<WebUiStatus, std::string>& statusResult);
    void updateWebUiTokenLabel();
    bool startAsyncWebUiAccessSet(bool enabled);

    std::string formatNetworkDetails(const Network::WifiNetworkInfo& info) const;
    std::string statusText(const Network::WifiNetworkInfo& info) const;

    static void onRefreshClicked(lv_event_t* e);
    static void onRefreshTimer(lv_timer_t* timer);
    static void onConnectClicked(lv_event_t* e);
    static void onForgetClicked(lv_event_t* e);
    static void onWebUiToggleChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
