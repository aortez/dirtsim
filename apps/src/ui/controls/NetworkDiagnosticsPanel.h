#pragma once

#include "core/network/WifiManager.h"
#include "lvgl/lvgl.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace DirtSim {
namespace Network {
class WebSocketService;
}

namespace Ui {

class TimeSeriesPlotWidget;

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
    void refresh(bool forceRefresh = false);
    void showWifiView();

private:
    enum class ViewMode { LanAccess, Wifi };
    enum class ConnectOverlayMode { PasswordEntry, Connecting };

    lv_obj_t* container_;
    lv_obj_t* connectProgressCancelButton_ = nullptr;
    lv_obj_t* connectProgressContainer_ = nullptr;
    lv_obj_t* connectProgressDetailLabel_ = nullptr;
    lv_obj_t* connectProgressPhaseLabel_ = nullptr;
    lv_obj_t* connectProgressStagesRow_ = nullptr;
    lv_obj_t* connectProgressTitleLabel_ = nullptr;
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
    lv_obj_t* passwordFormContainer_ = nullptr;
    lv_obj_t* passwordJoinButton_ = nullptr;
    lv_obj_t* passwordKeyboard_ = nullptr;
    lv_obj_t* networkDetailsOverlay_ = nullptr;
    lv_obj_t* networkDetailsContent_ = nullptr;
    lv_obj_t* networkDetailsLastScanValueLabel_ = nullptr;
    lv_obj_t* passwordOverlay_ = nullptr;
    lv_obj_t* passwordStatusLabel_ = nullptr;
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

    struct DisconnectContext {
        NetworkDiagnosticsPanel* panel = nullptr;
        size_t index = 0;
    };

    struct NetworkDetailsContext {
        NetworkDiagnosticsPanel* panel = nullptr;
        size_t index = 0;
    };

    struct ForgetContext {
        NetworkDiagnosticsPanel* panel = nullptr;
        size_t index = 0;
    };

    enum class AsyncActionKind { None, Connect, Disconnect, Forget };

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
        std::optional<std::vector<Network::WifiAccessPointInfo>> accessPoints;
        std::optional<std::string> activeBssid;
        std::optional<std::vector<NetworkInterfaceInfo>> localAddresses;
        std::optional<Network::WifiConnectOutcome> connectOutcome;
        std::optional<Network::WifiConnectProgress> connectProgress;
        std::optional<uint64_t> lastScanAgeMs;
        bool scanInProgress = false;
    };

    struct SignalHistorySeries {
        std::string ssid;
        std::vector<float> samples;
    };

    struct NetworkDetailsPlotBinding {
        std::string bssid;
        std::unique_ptr<TimeSeriesPlotWidget> plot;
    };

    struct AsyncState {
        std::mutex mutex;
        bool eventStreamConnected = false;
        bool refreshInProgress = false;
        bool scanRequestInProgress = false;
        std::optional<PendingRefreshData> pendingRefresh;
        std::optional<Result<std::monostate, std::string>> pendingConnectCancel;
        std::optional<Result<Network::WifiConnectResult, std::string>> pendingConnect;
        std::optional<Result<Network::WifiDisconnectResult, std::string>> pendingDisconnect;
        std::optional<Result<Network::WifiForgetResult, std::string>> pendingForget;
        std::optional<Result<std::monostate, std::string>> pendingScanRequest;
        std::optional<Result<NetworkAccessStatus, std::string>> pendingWebSocketUpdate;
        std::optional<Result<NetworkAccessStatus, std::string>> pendingWebUiUpdate;
        bool connectCancelInProgress = false;
        bool webSocketUpdateInProgress = false;
        bool webUiUpdateInProgress = false;
    };

    std::vector<NetworkInterfaceInfo> localAddresses_;
    std::vector<Network::WifiAccessPointInfo> accessPoints_;
    std::vector<Network::WifiNetworkInfo> networks_;
    std::vector<std::unique_ptr<ConnectContext>> connectContexts_;
    std::vector<std::unique_ptr<DisconnectContext>> disconnectContexts_;
    std::vector<std::unique_ptr<ForgetContext>> forgetContexts_;
    std::vector<std::unique_ptr<NetworkDetailsContext>> networkDetailsContexts_;
    std::vector<NetworkDetailsPlotBinding> networkDetailsPlotBindings_;
    std::shared_ptr<AsyncState> asyncState_;
    std::shared_ptr<Network::WebSocketService> eventClient_;
    AsyncActionState actionState_;
    std::optional<std::string> activeBssid_;
    std::optional<std::string> connectAwaitingConfirmationSsid_;
    std::optional<Network::WifiConnectProgress> connectProgress_;
    std::optional<std::chrono::steady_clock::time_point> connectStartedAt_;
    std::array<lv_obj_t*, 4> connectProgressStageBadges_{};
    ConnectOverlayMode connectOverlayMode_ = ConnectOverlayMode::PasswordEntry;
    Network::WifiConnectPhase lastConnectPhase_ = Network::WifiConnectPhase::Starting;
    std::optional<uint64_t> lastScanAgeMs_;
    std::optional<std::chrono::steady_clock::time_point> lastScanAgeUpdatedAt_;
    std::optional<Network::WifiStatus> latestWifiStatus_;
    bool webUiEnabled_ = false;
    bool connectOverlayHasPasswordEntry_ = false;
    std::optional<Network::WifiNetworkInfo> networkDetailsNetwork_;
    bool passwordVisible_ = false;
    bool scanInProgress_ = false;
    std::optional<std::chrono::steady_clock::time_point> signalHistoryLastSampleAt_;
    std::unordered_map<std::string, SignalHistorySeries> signalHistoryByBssid_;
    bool webSocketEnabled_ = false;
    std::optional<Network::WifiNetworkInfo> passwordPromptNetwork_;
    std::string webSocketToken_;
    ViewMode viewMode_ = ViewMode::Wifi;
    bool webUiToggleLocked_ = false;
    bool webSocketToggleLocked_ = false;

    void createUI();
    bool hasEventStreamConnection() const;
    void startEventStream();
    bool startAsyncRefresh(bool forceRefresh);
    bool startAsyncScanRequest();
    bool startAsyncConnect(
        const Network::WifiNetworkInfo& network,
        const std::optional<std::string>& password = std::nullopt);
    bool startAsyncConnectCancel();
    void startAsyncDisconnect(const Network::WifiNetworkInfo& network);
    void closeNetworkDetailsOverlay();
    void closePasswordPrompt();
    bool networkRequiresPassword(const Network::WifiNetworkInfo& network) const;
    void openNetworkDetailsOverlay(const Network::WifiNetworkInfo& network);
    void openConnectingOverlay(const Network::WifiNetworkInfo& network);
    void openPasswordPrompt(const Network::WifiNetworkInfo& network);
    void startAsyncForget(const Network::WifiNetworkInfo& network);
    bool beginAsyncAction(
        AsyncActionKind kind, const Network::WifiNetworkInfo& network, const std::string& verb);
    void endAsyncAction(AsyncActionKind kind);
    bool isActionInProgress() const;
    void applyPendingUpdates();
    std::string formatAnimatedConnectPhaseText() const;
    std::string formatConnectElapsedText() const;
    std::string formatConnectPhaseText() const;
    std::string formatConnectStatusMessage() const;
    void finalizeConfirmedConnect();
    bool isConnectedToSsid(const std::string& ssid) const;
    void setLoadingState();
    void setConnectOverlayMode(ConnectOverlayMode mode);
    void setConnectProgress(const std::optional<Network::WifiConnectProgress>& progress);
    void setPasswordPromptError(const std::string& message);
    void setPasswordPromptStatus(const std::string& message, uint32_t color);
    void setRefreshButtonEnabled(bool enabled);
    void setViewMode(ViewMode mode);
    void setWebSocketToggleEnabled(bool enabled);
    void setWebUiToggleEnabled(bool enabled);
    void submitPasswordPrompt();
    void updateCurrentConnectionSummary();
    void updateDetailsLastScanLabel();
    void updateDetailsSignalHistoryPlots();
    void updateSignalHistory(bool forceSample = false);
    void updateNetworkDisplay(
        const Result<std::vector<Network::WifiNetworkInfo>, std::string>& listResult);
    void updateConnectOverlay();
    void updateConnectPhaseBadges();
    void updatePasswordJoinButton();
    void updatePasswordVisibilityButton();
    void updateWifiStatus(const Result<Network::WifiStatus, std::string>& statusResult);
    void updateWebSocketStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    void updateWebSocketTokenLabel();
    void updateWebUiStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    bool startAsyncWebUiAccessSet(bool enabled);
    bool startAsyncWebSocketAccessSet(bool enabled);

    std::string formatAddressSummary() const;
    std::string formatAddressSummaryMultiline() const;
    std::string formatCurrentConnectionDetails(const Network::WifiNetworkInfo& info) const;
    std::string formatLastScanAgeText() const;
    std::string formatNetworkDetails(const Network::WifiNetworkInfo& info) const;
    std::optional<Network::WifiAccessPointInfo> preferredAccessPointForSsid(
        const std::string& ssid) const;
    void setWifiStatusMessage(const std::string& text, uint32_t color);

    static void onRefreshClicked(lv_event_t* e);
    static void onRefreshTimer(lv_timer_t* timer);
    static void onConnectClicked(lv_event_t* e);
    static void onConnectProgressCancelClicked(lv_event_t* e);
    static void onDisconnectClicked(lv_event_t* e);
    static void onForgetClicked(lv_event_t* e);
    static void onNetworkDetailsClicked(lv_event_t* e);
    static void onNetworkDetailsCloseClicked(lv_event_t* e);
    static void onNetworkDetailsForgetClicked(lv_event_t* e);
    static void onNetworkDetailsUpdatePasswordClicked(lv_event_t* e);
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
