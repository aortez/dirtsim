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

    void showLanAccessView();
    /**
     * @brief Refresh the network information display.
     *
     * Call this to update the displayed IP addresses (e.g., if network
     * configuration changes).
     */
    void refresh();
    void showWifiView();

    /**
     * @brief Get all non-loopback IPv4 addresses on the system.
     * @return Vector of interface info structs.
     */
    static std::vector<NetworkInterfaceInfo> getLocalAddresses();

private:
    enum class ViewMode { LanAccess, Wifi };

    lv_obj_t* container_;
    lv_obj_t* currentNetworkContainer_ = nullptr;
    lv_obj_t* lanAccessView_ = nullptr;
    lv_obj_t* networksTitleLabel_ = nullptr;
    lv_obj_t* pagesContainer_ = nullptr;
    lv_obj_t* refreshButton_ = nullptr;
    lv_obj_t* wifiStatusLabel_ = nullptr;
    lv_obj_t* wifiView_ = nullptr;
    lv_obj_t* networksContainer_ = nullptr;
    lv_obj_t* passwordCancelButton_ = nullptr;
    lv_obj_t* passwordErrorLabel_ = nullptr;
    lv_obj_t* passwordJoinButton_ = nullptr;
    lv_obj_t* passwordKeyboard_ = nullptr;
    lv_obj_t* passwordOverlay_ = nullptr;
    lv_obj_t* passwordTextArea_ = nullptr;
    lv_obj_t* passwordVisibilityButton_ = nullptr;
    lv_obj_t* webUiToggle_ = nullptr;
    lv_obj_t* webSocketToggle_ = nullptr;
    lv_obj_t* webSocketTokenTitleLabel_ = nullptr;
    lv_obj_t* webSocketTokenLabel_ = nullptr;
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

    struct NetworkAccessStatus {
        bool webUiEnabled = false;
        bool webSocketEnabled = false;
        std::string webSocketToken;
    };

    struct PendingRefreshData {
        Result<Network::WifiStatus, std::string> statusResult;
        Result<std::vector<Network::WifiNetworkInfo>, std::string> listResult;
        Result<NetworkAccessStatus, std::string> accessStatusResult;
    };

    struct AsyncState {
        std::mutex mutex;
        bool refreshInProgress = false;
        std::optional<PendingRefreshData> pendingRefresh;
        std::optional<Result<Network::WifiConnectResult, std::string>> pendingConnect;
        std::optional<Result<Network::WifiForgetResult, std::string>> pendingForget;
        std::optional<Result<NetworkAccessStatus, std::string>> pendingWebSocketUpdate;
        std::optional<Result<NetworkAccessStatus, std::string>> pendingWebUiUpdate;
        bool webSocketUpdateInProgress = false;
        bool webUiUpdateInProgress = false;
    };

    std::vector<NetworkInterfaceInfo> localAddresses_;
    std::vector<Network::WifiNetworkInfo> networks_;
    std::vector<std::unique_ptr<ConnectContext>> connectContexts_;
    std::vector<std::unique_ptr<ForgetContext>> forgetContexts_;
    std::shared_ptr<AsyncState> asyncState_;
    AsyncActionState actionState_;
    bool webUiEnabled_ = false;
    bool passwordVisible_ = false;
    bool webSocketEnabled_ = false;
    std::optional<Network::WifiNetworkInfo> passwordPromptNetwork_;
    std::string webSocketToken_;
    ViewMode viewMode_ = ViewMode::Wifi;
    bool webUiToggleLocked_ = false;
    bool webSocketToggleLocked_ = false;

    void createUI();
    bool startAsyncRefresh();
    void closePasswordPrompt();
    bool networkRequiresPassword(const Network::WifiNetworkInfo& network) const;
    void openPasswordPrompt(const Network::WifiNetworkInfo& network);
    void startAsyncConnect(
        const Network::WifiNetworkInfo& network,
        const std::optional<std::string>& password = std::nullopt);
    void startAsyncForget(const Network::WifiNetworkInfo& network);
    bool beginAsyncAction(
        AsyncActionKind kind, const Network::WifiNetworkInfo& network, const std::string& verb);
    void endAsyncAction(AsyncActionKind kind);
    bool isActionInProgress() const;
    void applyPendingUpdates();
    void setLoadingState();
    void setPasswordPromptBusy(bool busy);
    void setPasswordPromptError(const std::string& message);
    void setRefreshButtonEnabled(bool enabled);
    void setViewMode(ViewMode mode);
    void setWebSocketToggleEnabled(bool enabled);
    void setWebUiToggleEnabled(bool enabled);
    void submitPasswordPrompt();
    void updateAddressDisplay();
    void updateCurrentConnectionSummary();
    void updateNetworkDisplay(
        const Result<std::vector<Network::WifiNetworkInfo>, std::string>& listResult);
    void updatePasswordJoinButton();
    void updatePasswordVisibilityButton();
    void updateWifiStatus(const Result<Network::WifiStatus, std::string>& statusResult);
    void updateWebSocketStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    void updateWebSocketTokenLabel();
    void updateWebUiStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    bool startAsyncWebUiAccessSet(bool enabled);
    bool startAsyncWebSocketAccessSet(bool enabled);

    std::string formatAddressSummary() const;
    std::string formatCurrentConnectionDetails(const Network::WifiNetworkInfo& info) const;
    std::string formatNetworkDetails(const Network::WifiNetworkInfo& info) const;
    void setWifiStatusMessage(const std::string& text, uint32_t color);

    static void onRefreshClicked(lv_event_t* e);
    static void onRefreshTimer(lv_timer_t* timer);
    static void onConnectClicked(lv_event_t* e);
    static void onForgetClicked(lv_event_t* e);
    static void onPasswordCancelClicked(lv_event_t* e);
    static void onPasswordJoinClicked(lv_event_t* e);
    static void onPasswordKeyboardEvent(lv_event_t* e);
    static void onPasswordTextAreaEvent(lv_event_t* e);
    static void onPasswordVisibilityClicked(lv_event_t* e);
    static void onWebSocketToggleChanged(lv_event_t* e);
    static void onWebUiToggleChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
