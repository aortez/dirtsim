#pragma once

#include "core/network/WifiManager.h"
#include "lvgl/lvgl.h"
#include "os-manager/ScannerTypes.h"
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
class ScannerChannelMapWidget;
class UserSettingsManager;

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
    enum class AutomationScreen {
        LanAccess,
        Scanner,
        Wifi,
        WifiConnecting,
        WifiDetails,
        WifiPassword,
    };

    struct AutomationConnectProgress {
        std::string phase;
        std::string ssid;
        bool canCancel = true;
    };

    struct AutomationNetworkInfo {
        std::string ssid;
        std::string status;
        bool requiresPassword = false;
    };

    struct AutomationState {
        std::optional<AutomationConnectProgress> connectProgress;
        std::optional<std::string> connectedSsid;
        std::optional<std::string> connectTargetSsid;
        std::optional<std::string> passwordPromptTargetSsid;
        std::string passwordError;
        std::string scannerStatusMessage;
        std::vector<AutomationNetworkInfo> networks;
        AutomationScreen screen = AutomationScreen::Wifi;
        std::string viewMode;
        std::string wifiStatusMessage;
        bool connectCancelEnabled = false;
        bool connectCancelVisible = false;
        bool connectOverlayVisible = false;
        bool passwordPromptVisible = false;
        bool passwordSubmitEnabled = false;
        bool scannerEnterEnabled = false;
        bool scannerExitEnabled = false;
        bool scannerModeActive = false;
        bool scannerModeAvailable = false;
    };

    /**
     * @brief Construct the network diagnostics panel.
     * @param container Parent LVGL container to build UI in.
     */
    explicit NetworkDiagnosticsPanel(lv_obj_t* container, UserSettingsManager& userSettingsManager);
    ~NetworkDiagnosticsPanel();

    Result<AutomationState, std::string> getAutomationState();
    Result<std::monostate, std::string> pressAutomationConnect(const std::string& ssid);
    Result<std::monostate, std::string> pressAutomationConnectCancel();
    Result<std::monostate, std::string> submitAutomationPassword(const std::string& password);
    Result<std::monostate, std::string> pressAutomationScannerEnter();
    Result<std::monostate, std::string> pressAutomationScannerExit();

    bool isScannerModeActive() const;
    bool isScannerModeBusy() const;
    bool isScannerModeActiveOrBusy();
    Result<std::monostate, std::string> requestScannerExit();

    void showLanAccessView();
    void showScannerView();
    /**
     * @brief Refresh the network information display.
     *
     * Call this to update the displayed IP addresses (e.g., if network
     * configuration changes).
     */
    void refresh(bool forceRefresh = false);
    void showWifiView();

private:
    enum class ViewMode { LanAccess, Scanner, Wifi, WifiConnectFlow, WifiDetails };
    enum class ConnectOverlayMode { PasswordEntry, Connecting };
    using ScannerBand = OsManager::ScannerBand;

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
    lv_obj_t* connectFlowView_ = nullptr;
    lv_obj_t* networkDetailsView_ = nullptr;
    lv_obj_t* refreshButton_ = nullptr;
    lv_obj_t* scannerAutoButton_ = nullptr;
    lv_obj_t* scannerBandDropdown_ = nullptr;
    lv_obj_t* scannerEnterButton_ = nullptr;
    lv_obj_t* scannerExitButton_ = nullptr;
    lv_obj_t* scannerRadiosHeaderLabel_ = nullptr;
    lv_obj_t* scannerRadiosList_ = nullptr;
    lv_obj_t* scannerRefreshButton_ = nullptr;
    lv_obj_t* scannerStatusLabel_ = nullptr;
    lv_obj_t* scannerView_ = nullptr;
    lv_obj_t* scannerWidthDropdown_ = nullptr;
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
    lv_obj_t* passwordStatusLabel_ = nullptr;
    lv_obj_t* passwordTextArea_ = nullptr;
    lv_obj_t* passwordVisibilityButton_ = nullptr;
    lv_obj_t* liveScanToggle_ = nullptr;
    lv_obj_t* webUiToggle_ = nullptr;
    lv_obj_t* webSocketToggle_ = nullptr;
    lv_obj_t* webSocketTokenTitleLabel_ = nullptr;
    lv_obj_t* webSocketTokenLabel_ = nullptr;
    lv_timer_t* refreshTimer_ = nullptr;
    UserSettingsManager* userSettingsManager_ = nullptr;

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
        bool scannerModeAvailable = false;
        bool scannerModeActive = false;
        std::string scannerModeDetail;
    };

    struct PendingRefreshData {
        Result<Network::WifiStatus, std::string> statusResult;
        Result<std::vector<Network::WifiNetworkInfo>, std::string> listResult;
        Result<NetworkAccessStatus, std::string> accessStatusResult;
        std::optional<Result<OsManager::ScannerConfig, std::string>> scannerConfigResult;
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

    struct ScannerObservedRadio {
        std::string bssid;
        std::string ssid;
        std::optional<int> signalDbm;
        std::optional<int> channel;
        std::optional<uint64_t> lastSeenAgeMs;
        OsManager::ScannerObservationKind observationKind =
            OsManager::ScannerObservationKind::Direct;
    };

    struct ScannerSnapshot {
        bool active = false;
        OsManager::ScannerConfig config = OsManager::scannerDefaultConfig();
        std::optional<OsManager::ScannerTuning> currentTuning;
        std::string detail;
        std::vector<ScannerObservedRadio> radios;
    };

    struct ScannerSnapshotError {
        std::string message;
    };

    struct ScannerRadioRowState {
        lv_obj_t* row = nullptr;
        lv_obj_t* ssidLabel = nullptr;
        lv_obj_t* channelLabel = nullptr;
        lv_obj_t* rssiLabel = nullptr;
        lv_obj_t* ageLabel = nullptr;
        std::string pendingSwapAboveKey;
        ScannerObservedRadio radio;
        float smoothedSignalDbm = -200.0f;
        int pendingSwapAboveUpdates = 0;
        bool hasSmoothedSignal = false;
    };

    struct AsyncState {
        std::mutex mutex;
        bool eventStreamConnected = false;
        bool refreshInProgress = false;
        bool scanRequestInProgress = false;
        bool scannerSnapshotInProgress = false;
        std::optional<PendingRefreshData> pendingRefresh;
        std::optional<Result<std::monostate, std::string>> pendingConnectCancel;
        std::optional<Result<Network::WifiConnectResult, std::string>> pendingConnect;
        std::optional<Result<Network::WifiDisconnectResult, std::string>> pendingDisconnect;
        std::optional<Result<bool, std::string>> pendingDiagnosticsModeUpdate;
        std::optional<Result<Network::WifiForgetResult, std::string>> pendingForget;
        std::optional<Result<std::monostate, std::string>> pendingScannerEnter;
        std::optional<Result<std::monostate, std::string>> pendingScannerExit;
        std::optional<Result<ScannerSnapshot, ScannerSnapshotError>> pendingScannerSnapshot;
        std::optional<Result<std::monostate, std::string>> pendingScanRequest;
        std::optional<Result<NetworkAccessStatus, std::string>> pendingWebSocketUpdate;
        std::optional<Result<NetworkAccessStatus, std::string>> pendingWebUiUpdate;
        bool connectCancelInProgress = false;
        bool diagnosticsModeUpdateInProgress = false;
        bool scannerEnterInProgress = false;
        bool scannerExitInProgress = false;
        bool scannerConfigSetInProgress = false;
        bool webSocketUpdateInProgress = false;
        bool webUiUpdateInProgress = false;
        std::optional<Result<OsManager::ScannerConfig, std::string>> pendingScannerConfigSet;
    };

    std::vector<NetworkInterfaceInfo> localAddresses_;
    std::vector<Network::WifiAccessPointInfo> accessPoints_;
    std::vector<Network::WifiNetworkInfo> networks_;
    std::vector<std::unique_ptr<ConnectContext>> connectContexts_;
    std::vector<std::unique_ptr<DisconnectContext>> disconnectContexts_;
    std::vector<std::unique_ptr<ForgetContext>> forgetContexts_;
    std::vector<std::unique_ptr<NetworkDetailsContext>> networkDetailsContexts_;
    std::vector<NetworkDetailsPlotBinding> networkDetailsPlotBindings_;
    std::unique_ptr<ScannerChannelMapWidget> scannerChannelMap_;
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
    bool liveScanEnabled_ = false;
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
    bool scannerActionInProgress_ = false;
    bool scannerModeActive_ = false;
    bool scannerModeAvailable_ = false;
    std::string scannerModeDetail_;
    std::optional<std::chrono::steady_clock::time_point> scannerSnapshotActivityAt_;
    std::optional<OsManager::ScannerTuning> scannerCurrentTuning_;
    size_t scannerObservedRadioCount_ = 0;
    std::vector<ScannerObservedRadio> scannerObservedRadios_;
    std::unordered_map<std::string, ScannerRadioRowState> scannerRadioRowsByKey_;
    std::vector<std::string> scannerRadioOrder_;
    std::optional<ScannerBand> scannerRenderedBand_;
    std::string scannerSnapshotErrorMessage_;
    bool scannerSnapshotReceived_ = false;
    bool scannerSnapshotStale_ = false;
    bool scannerStatusUnavailable_ = false;
    OsManager::ScannerConfig scannerConfig_ = OsManager::scannerDefaultConfig();
    bool scannerConfigSetInProgress_ = false;
    bool scannerRadiosListScrolling_ = false;
    bool liveScanToggleLocked_ = false;
    bool webUiToggleLocked_ = false;
    bool webSocketToggleLocked_ = false;

    void createUI();
    bool hasEventStreamConnection() const;
    void startEventStream();
    bool startAsyncRefresh(bool forceRefresh);
    bool startAsyncScannerConfigSet(const OsManager::ScannerConfig& config);
    bool startAsyncScannerEnter();
    bool startAsyncScannerExit();
    bool startAsyncScannerSnapshot();
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
    void setScannerRefreshButtonEnabled(bool enabled);
    void setViewMode(ViewMode mode);
    void setLiveScanToggleEnabled(bool enabled);
    void setWebSocketToggleEnabled(bool enabled);
    void setWebUiToggleEnabled(bool enabled);
    void submitPasswordPrompt();
    std::optional<size_t> findNetworkIndexBySsid(const std::string& ssid) const;
    bool isScannerSnapshotStale() const;
    void resetScannerSnapshotState();
    void clearScannerRadioRows();
    bool isScannerConfigRequestInFlight() const;
    std::optional<OsManager::ScannerTuning> scannerDisplayedManualTuning() const;
    ScannerBand scannerDisplayedBand() const;
    bool isScannerManualRetunePending() const;
    std::optional<OsManager::ScannerTuning> scannerRequestedManualTuning() const;
    std::string scannerRadioIdentity(const ScannerObservedRadio& radio) const;
    bool scannerRadioMatchesSelectedBand(const ScannerObservedRadio& radio) const;
    std::string scannerSelectedBandLabel() const;
    int scannerSelectedWidthMhz() const;
    void applyScannerConfigChange(OsManager::ScannerConfig nextConfig);
    void onScannerChannelSelected(int channel);
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
    void updateScannerRadioRowDisplay(
        ScannerRadioRowState& rowState, const ScannerObservedRadio& radio);
    void updateScannerConfigControls();
    void updateScannerSnapshot(const Result<ScannerSnapshot, ScannerSnapshotError>& result);
    void updateScannerControls();
    void updateScannerChannelMap();
    void updateScannerRadioList();
    void updateScannerRadioRowOrder();
    void updateScannerStaleState();
    void updateScannerStatusLabel();
    void updateScannerStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    void updateWifiStatus(const Result<Network::WifiStatus, std::string>& statusResult);
    void updateWebSocketStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    void updateWebSocketTokenLabel();
    void updateWebUiStatus(const Result<NetworkAccessStatus, std::string>& statusResult);
    bool startAsyncDiagnosticsModeSet(bool enabled);
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
    static void onScannerAutoClicked(lv_event_t* e);
    static void onScannerBandChanged(lv_event_t* e);
    static void onScannerEnterClicked(lv_event_t* e);
    static void onScannerExitClicked(lv_event_t* e);
    static void onScannerRadiosListScroll(lv_event_t* e);
    static void onScannerRefreshClicked(lv_event_t* e);
    static void onScannerWidthChanged(lv_event_t* e);
    static void onLiveScanToggleChanged(lv_event_t* e);
    static void onWebSocketToggleChanged(lv_event_t* e);
    static void onWebUiToggleChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
