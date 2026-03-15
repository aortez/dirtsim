#include "NetworkService.h"

#include "core/LoggingChannels.h"
#include "core/network/WifiManagerLibNm.h"

#include <NetworkManager.h>
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <future>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <system_error>
#include <thread>

namespace DirtSim {
namespace OsManager {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kPeriodicRefreshInterval = std::chrono::seconds(15);

std::vector<NetworkService::LocalAddressInfo> collectLocalAddresses()
{
    std::vector<NetworkService::LocalAddressInfo> results;

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return results;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 || (ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }

        char addrBuf[INET_ADDRSTRLEN];
        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sa->sin_addr, addrBuf, sizeof(addrBuf)) == nullptr) {
            continue;
        }

        results.push_back({ .name = ifa->ifa_name, .address = addrBuf });
    }

    freeifaddrs(ifaddr);
    return results;
}

bool localAddressesEqual(
    const std::vector<NetworkService::LocalAddressInfo>& lhs,
    const std::vector<NetworkService::LocalAddressInfo>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].name != rhs[i].name || lhs[i].address != rhs[i].address) {
            return false;
        }
    }

    return true;
}

bool wifiNetworkInfoEqual(const Network::WifiNetworkInfo& lhs, const Network::WifiNetworkInfo& rhs)
{
    return lhs.ssid == rhs.ssid && lhs.status == rhs.status && lhs.signalDbm == rhs.signalDbm
        && lhs.security == rhs.security && lhs.autoConnect == rhs.autoConnect
        && lhs.hasCredentials == rhs.hasCredentials && lhs.lastUsedDate == rhs.lastUsedDate
        && lhs.lastUsedRelative == rhs.lastUsedRelative && lhs.connectionId == rhs.connectionId;
}

bool wifiNetworksEqual(
    const std::vector<Network::WifiNetworkInfo>& lhs,
    const std::vector<Network::WifiNetworkInfo>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!wifiNetworkInfoEqual(lhs[i], rhs[i])) {
            return false;
        }
    }

    return true;
}

bool wifiConnectProgressEqual(
    const std::optional<Network::WifiConnectProgress>& lhs,
    const std::optional<Network::WifiConnectProgress>& rhs)
{
    if (!lhs.has_value() || !rhs.has_value()) {
        return !lhs.has_value() && !rhs.has_value();
    }

    return lhs->ssid == rhs->ssid && lhs->phase == rhs->phase && lhs->canCancel == rhs->canCancel;
}

bool wifiConnectOutcomeEqual(
    const std::optional<Network::WifiConnectOutcome>& lhs,
    const std::optional<Network::WifiConnectOutcome>& rhs)
{
    if (!lhs.has_value() || !rhs.has_value()) {
        return !lhs.has_value() && !rhs.has_value();
    }

    return lhs->ssid == rhs->ssid && lhs->message == rhs->message && lhs->canceled == rhs->canceled;
}

bool isScanDerivedStatus(Network::WifiNetworkStatus status)
{
    return status == Network::WifiNetworkStatus::Available
        || status == Network::WifiNetworkStatus::Open;
}

std::vector<Network::WifiNetworkInfo> mergePassiveNetworkList(
    const std::vector<Network::WifiNetworkInfo>& cachedNetworks,
    const std::vector<Network::WifiNetworkInfo>& latestNetworks)
{
    std::vector<Network::WifiNetworkInfo> mergedNetworks = latestNetworks;
    std::unordered_map<std::string, size_t> latestIndexBySsid;
    latestIndexBySsid.reserve(latestNetworks.size());

    for (size_t i = 0; i < latestNetworks.size(); ++i) {
        latestIndexBySsid.emplace(latestNetworks[i].ssid, i);
    }

    std::vector<Network::WifiNetworkInfo> preservedScanEntries;
    for (const auto& cachedNetwork : cachedNetworks) {
        if (!isScanDerivedStatus(cachedNetwork.status)) {
            continue;
        }
        if (latestIndexBySsid.contains(cachedNetwork.ssid)) {
            continue;
        }

        preservedScanEntries.push_back(cachedNetwork);
    }

    std::sort(
        preservedScanEntries.begin(),
        preservedScanEntries.end(),
        [](const Network::WifiNetworkInfo& a, const Network::WifiNetworkInfo& b) {
            const int signalA = a.signalDbm.has_value() ? a.signalDbm.value() : -200;
            const int signalB = b.signalDbm.has_value() ? b.signalDbm.value() : -200;
            if (signalA != signalB) {
                return signalA > signalB;
            }
            return a.ssid < b.ssid;
        });

    mergedNetworks.insert(
        mergedNetworks.end(), preservedScanEntries.begin(), preservedScanEntries.end());
    return mergedNetworks;
}

bool snapshotsEqual(const NetworkService::Snapshot& lhs, const NetworkService::Snapshot& rhs)
{
    return lhs.status.connected == rhs.status.connected && lhs.status.ssid == rhs.status.ssid
        && lhs.scanInProgress == rhs.scanInProgress
        && wifiConnectOutcomeEqual(lhs.connectOutcome, rhs.connectOutcome)
        && wifiConnectProgressEqual(lhs.connectProgress, rhs.connectProgress)
        && wifiNetworksEqual(lhs.networks, rhs.networks)
        && localAddressesEqual(lhs.localAddresses, rhs.localAddresses);
}

std::optional<Network::WifiConnectPhase> wifiConnectPhaseFromDeviceState(NMDeviceState state)
{
    switch (state) {
        case NM_DEVICE_STATE_UNKNOWN:
        case NM_DEVICE_STATE_UNMANAGED:
        case NM_DEVICE_STATE_UNAVAILABLE:
        case NM_DEVICE_STATE_DISCONNECTED:
            return std::nullopt;
        case NM_DEVICE_STATE_PREPARE:
        case NM_DEVICE_STATE_CONFIG:
            return Network::WifiConnectPhase::Associating;
        case NM_DEVICE_STATE_NEED_AUTH:
            return Network::WifiConnectPhase::Authenticating;
        case NM_DEVICE_STATE_IP_CONFIG:
        case NM_DEVICE_STATE_IP_CHECK:
        case NM_DEVICE_STATE_SECONDARIES:
        case NM_DEVICE_STATE_ACTIVATED:
            return Network::WifiConnectPhase::GettingAddress;
        case NM_DEVICE_STATE_DEACTIVATING:
        case NM_DEVICE_STATE_FAILED:
            return std::nullopt;
    }

    return std::nullopt;
}

} // namespace

struct NetworkService::Impl {
    struct CachedSnapshot {
        Snapshot snapshot;
        Clock::time_point updatedAt = Clock::now();
    };

    struct ConnectCompletionTask {
        Impl* self = nullptr;
        GMainContext* context = nullptr;
        uint64_t operationId = 0;
        Result<Network::WifiConnectResult, std::string> result =
            Result<Network::WifiConnectResult, std::string>::error("WiFi connect failed");
    };

    struct SignalConnection {
        gpointer instance = nullptr;
        gulong handlerId = 0;

        void disconnect()
        {
            if (instance && handlerId != 0) {
                g_signal_handler_disconnect(instance, handlerId);
                instance = nullptr;
                handlerId = 0;
            }
        }
    };

    std::mutex mutex;
    std::condition_variable stateCondition;
    std::thread connectThread;
    uint64_t connectOperationId = 0;
    std::thread workerThread;
    bool running = false;
    bool stopRequested = false;
    bool threadReady = false;
    std::string startError;
    GMainContext* mainContext = nullptr;
    GMainLoop* mainLoop = nullptr;
    guint periodicRefreshSourceId = 0;
    guint scheduledRefreshSourceId = 0;
    bool refreshScheduled = false;
    bool refreshForceScan = false;
    Network::Internal::GObjectPtr<NMClient> client;
    NMDeviceWifi* wifiDevice = nullptr;
    std::vector<SignalConnection> clientSignals;
    std::vector<SignalConnection> deviceSignals;
    std::optional<CachedSnapshot> cachedSnapshot;
    std::optional<Network::WifiConnectOutcome> connectOutcome;
    std::optional<Network::WifiConnectProgress> connectProgress;
    bool connectCancelRequested = false;
    std::string lastRefreshError;
    SnapshotChangedCallback snapshotChangedCallback;
    bool scanInProgress = false;

    Result<std::monostate, std::string> start()
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (running) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }

        stopRequested = false;
        threadReady = false;
        startError.clear();

        try {
            workerThread = std::thread([this]() { workerLoop(); });
        }
        catch (const std::system_error& e) {
            return Result<std::monostate, std::string>::error(e.what());
        }

        stateCondition.wait(lock, [this]() { return threadReady || !startError.empty(); });
        if (!startError.empty()) {
            lock.unlock();
            if (workerThread.joinable()) {
                workerThread.join();
            }
            return Result<std::monostate, std::string>::error(startError);
        }

        running = true;
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    void stop()
    {
        GMainContext* context = nullptr;
        GMainLoop* loop = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!running && !workerThread.joinable()) {
                return;
            }

            stopRequested = true;
            context = mainContext;
            loop = mainLoop;
        }

        if (connectThread.joinable()) {
            connectThread.join();
        }

        if (context && loop) {
            g_main_context_invoke_full(
                context,
                G_PRIORITY_DEFAULT,
                +[](gpointer data) -> gboolean {
                    g_main_loop_quit(static_cast<GMainLoop*>(data));
                    return G_SOURCE_REMOVE;
                },
                loop,
                nullptr);
        }

        if (workerThread.joinable()) {
            workerThread.join();
        }

        cachedSnapshot.reset();
        connectOutcome.reset();
        connectProgress.reset();
        connectCancelRequested = false;
        lastRefreshError.clear();
        scanInProgress = false;
    }

    Result<Snapshot, std::string> getSnapshot(bool forceRefresh)
    {
        return invokeOnWorker<Result<Snapshot, std::string>>([this, forceRefresh]() {
            const bool needsInitialSnapshot = !cachedSnapshot.has_value();
            if (forceRefresh || needsInitialSnapshot) {
                refreshSnapshot(forceRefresh || needsInitialSnapshot);
            }

            if (cachedSnapshot.has_value()) {
                return Result<Snapshot, std::string>::okay(cachedSnapshot->snapshot);
            }

            const std::string error =
                lastRefreshError.empty() ? "Network snapshot unavailable" : lastRefreshError;
            return Result<Snapshot, std::string>::error(error);
        });
    }

    Result<std::monostate, std::string> cancelConnect()
    {
        return invokeOnWorker<Result<std::monostate, std::string>>([this]() {
            if (!connectProgress.has_value()) {
                return Result<std::monostate, std::string>::error("No WiFi connect in progress");
            }
            if (!ensureClient()) {
                return Result<std::monostate, std::string>::error(lastRefreshError);
            }

            const auto previousProgress = connectProgress;
            connectCancelRequested = true;
            setConnectProgress(
                Network::WifiConnectProgress{
                    .ssid = connectProgress->ssid,
                    .phase = Network::WifiConnectPhase::Canceling,
                    .canCancel = false,
                });

            const auto result = Network::Internal::requestConnectCancel(client.get(), mainContext);
            if (result.isError()) {
                connectCancelRequested = false;
                if (previousProgress.has_value()) {
                    setConnectProgress(previousProgress.value());
                }
                return result;
            }

            return Result<std::monostate, std::string>::okay(std::monostate{});
        });
    }

    void finalizeConnectOperation(
        const uint64_t operationId, Result<Network::WifiConnectResult, std::string> result)
    {
        if (operationId != connectOperationId) {
            return;
        }

        if (connectThread.joinable() && std::this_thread::get_id() != connectThread.get_id()) {
            connectThread.join();
        }

        const bool canceled = connectCancelRequested;
        const std::string targetSsid =
            connectProgress.has_value() ? connectProgress->ssid : std::string{};

        clearConnectProgress();
        connectCancelRequested = false;
        refreshSnapshot(false);

        const bool connectedToTarget = !targetSsid.empty() && cachedSnapshot.has_value()
            && cachedSnapshot->snapshot.status.connected
            && cachedSnapshot->snapshot.status.ssid == targetSsid;
        if (canceled && !connectedToTarget) {
            setConnectOutcome(
                Network::WifiConnectOutcome{
                    .ssid = targetSsid,
                    .message = "WiFi connection canceled",
                    .canceled = true,
                });
            return;
        }

        if (result.isError()) {
            setConnectOutcome(
                Network::WifiConnectOutcome{
                    .ssid = targetSsid,
                    .message = result.errorValue(),
                    .canceled = false,
                });
            return;
        }

        clearConnectOutcome();
    }

    Result<Network::WifiConnectResult, std::string> connectBySsid(
        const std::string& ssid, const std::optional<std::string>& password)
    {
        return invokeOnWorker<Result<Network::WifiConnectResult, std::string>>(
            [this, ssid, password]() {
                if (connectProgress.has_value()) {
                    return Result<Network::WifiConnectResult, std::string>::error(
                        "WiFi connect already in progress");
                }
                if (!ensureClient()) {
                    return Result<Network::WifiConnectResult, std::string>::error(lastRefreshError);
                }
                if (!mainContext) {
                    return Result<Network::WifiConnectResult, std::string>::error(
                        "NetworkService worker unavailable");
                }

                rebindWifiDevice();
                if (connectThread.joinable()) {
                    connectThread.join();
                }
                clearConnectOutcome();
                connectCancelRequested = false;
                setConnectProgress(
                    Network::WifiConnectProgress{
                        .ssid = ssid,
                        .phase = Network::WifiConnectPhase::Starting,
                        .canCancel = true,
                    });

                const uint64_t operationId = ++connectOperationId;
                GMainContext* completionContext = g_main_context_ref(mainContext);

                try {
                    connectThread =
                        std::thread([this, completionContext, operationId, ssid, password]() {
                            Network::WifiManager wifiManager;
                            auto result = wifiManager.connectBySsid(ssid, password);

                            auto* task = new ConnectCompletionTask{
                                .self = this,
                                .context = completionContext,
                                .operationId = operationId,
                                .result = std::move(result),
                            };

                            g_main_context_invoke_full(
                                completionContext,
                                G_PRIORITY_DEFAULT,
                                +[](gpointer data) -> gboolean {
                                    auto* task = static_cast<ConnectCompletionTask*>(data);
                                    task->self->finalizeConnectOperation(
                                        task->operationId, std::move(task->result));
                                    return G_SOURCE_REMOVE;
                                },
                                task,
                                +[](gpointer data) {
                                    auto* task = static_cast<ConnectCompletionTask*>(data);
                                    if (task->context) {
                                        g_main_context_unref(task->context);
                                        task->context = nullptr;
                                    }
                                    delete task;
                                });
                        });
                }
                catch (const std::system_error& e) {
                    g_main_context_unref(completionContext);
                    clearConnectProgress();
                    return Result<Network::WifiConnectResult, std::string>::error(e.what());
                }

                return Result<Network::WifiConnectResult, std::string>::okay(
                    Network::WifiConnectResult{ .success = true, .ssid = ssid });
            });
    }

    Result<Network::WifiDisconnectResult, std::string> disconnect(
        const std::optional<std::string>& ssid)
    {
        return invokeOnWorker<Result<Network::WifiDisconnectResult, std::string>>([this, ssid]() {
            if (!ensureClient()) {
                return Result<Network::WifiDisconnectResult, std::string>::error(lastRefreshError);
            }

            rebindWifiDevice();
            const auto result = Network::Internal::disconnect(client.get(), ssid, mainContext);
            if (!result.isError()) {
                refreshSnapshot(false);
            }
            return result;
        });
    }

    Result<Network::WifiForgetResult, std::string> forget(const std::string& ssid)
    {
        return invokeOnWorker<Result<Network::WifiForgetResult, std::string>>([this, ssid]() {
            if (!ensureClient()) {
                return Result<Network::WifiForgetResult, std::string>::error(lastRefreshError);
            }

            rebindWifiDevice();
            const auto result = Network::Internal::forget(client.get(), ssid, mainContext);
            if (!result.isError()) {
                refreshSnapshot(false);
            }
            return result;
        });
    }

    Result<std::monostate, std::string> requestScan()
    {
        return invokeOnWorker<Result<std::monostate, std::string>>([this]() {
            if (!ensureClient()) {
                return Result<std::monostate, std::string>::error(lastRefreshError);
            }

            scheduleRefresh(true);
            return Result<std::monostate, std::string>::okay(std::monostate{});
        });
    }

    void setSnapshotChangedCallback(SnapshotChangedCallback callback)
    {
        std::lock_guard<std::mutex> lock(mutex);
        snapshotChangedCallback = std::move(callback);
    }

    template <typename ResultType, typename Fn>
    ResultType invokeOnWorker(Fn&& fn)
    {
        const auto startResult = start();
        if (startResult.isError()) {
            return ResultType::error(startResult.errorValue());
        }

        if (std::this_thread::get_id() == workerThread.get_id()) {
            return fn();
        }

        auto promise = std::make_shared<std::promise<ResultType>>();
        auto future = promise->get_future();

        GMainContext* context = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            context = mainContext;
        }

        if (!context) {
            return ResultType::error("NetworkService worker unavailable");
        }

        auto* task = new std::function<void()>([promise, fn = std::forward<Fn>(fn)]() mutable {
            try {
                promise->set_value(fn());
            }
            catch (const std::exception& e) {
                promise->set_value(ResultType::error(e.what()));
            }
            catch (...) {
                promise->set_value(ResultType::error("Unhandled NetworkService worker exception"));
            }
        });

        g_main_context_invoke_full(
            context,
            G_PRIORITY_DEFAULT,
            +[](gpointer data) -> gboolean {
                auto task = std::unique_ptr<std::function<void()>>(
                    static_cast<std::function<void()>*>(data));
                (*task)();
                return G_SOURCE_REMOVE;
            },
            task,
            nullptr);

        return future.get();
    }

    void workerLoop()
    {
        mainContext = g_main_context_new();
        if (!mainContext) {
            failStart("Failed to create GLib main context");
            return;
        }

        g_main_context_push_thread_default(mainContext);
        mainLoop = g_main_loop_new(mainContext, FALSE);
        if (!mainLoop) {
            failStart("Failed to create GLib main loop");
            g_main_context_pop_thread_default(mainContext);
            g_main_context_unref(mainContext);
            mainContext = nullptr;
            return;
        }

        periodicRefreshSourceId = attachPeriodicRefresh();

        {
            std::lock_guard<std::mutex> lock(mutex);
            threadReady = true;
        }
        stateCondition.notify_all();

        scheduleRefresh(true);
        g_main_loop_run(mainLoop);

        cleanupWorkerState();
    }

    void failStart(const std::string& error)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            startError = error;
            threadReady = false;
        }
        stateCondition.notify_all();
    }

    guint attachPeriodicRefresh()
    {
        GSource* source = g_timeout_source_new_seconds(
            static_cast<guint>(
                std::chrono::duration_cast<std::chrono::seconds>(kPeriodicRefreshInterval)
                    .count()));
        g_source_set_callback(
            source,
            +[](gpointer userData) -> gboolean {
                auto* self = static_cast<Impl*>(userData);
                self->scheduleRefresh(false);
                return G_SOURCE_CONTINUE;
            },
            this,
            nullptr);
        const guint sourceId = g_source_attach(source, mainContext);
        g_source_unref(source);
        return sourceId;
    }

    void cleanupWorkerState()
    {
        disconnectSignals(deviceSignals);
        disconnectSignals(clientSignals);

        if (scheduledRefreshSourceId != 0) {
            g_source_remove(scheduledRefreshSourceId);
            scheduledRefreshSourceId = 0;
        }
        if (periodicRefreshSourceId != 0) {
            g_source_remove(periodicRefreshSourceId);
            periodicRefreshSourceId = 0;
        }

        client.reset();
        wifiDevice = nullptr;

        if (mainLoop) {
            g_main_loop_unref(mainLoop);
            mainLoop = nullptr;
        }
        if (mainContext) {
            g_main_context_pop_thread_default(mainContext);
            g_main_context_unref(mainContext);
            mainContext = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
            stopRequested = false;
            threadReady = false;
        }
        stateCondition.notify_all();
    }

    bool ensureClient()
    {
        if (client) {
            return true;
        }

        std::string errorMessage;
        client = Network::Internal::createClient(errorMessage, mainContext);
        if (!client) {
            lastRefreshError =
                errorMessage.empty() ? "Failed to initialize NetworkManager client" : errorMessage;
            LOG_WARN(Network, "NetworkService client init failed: {}", lastRefreshError);
            return false;
        }

        bindClientSignals();
        rebindWifiDevice();
        return true;
    }

    void bindClientSignals()
    {
        disconnectSignals(clientSignals);
        if (!client) {
            return;
        }

        clientSignals.push_back(connectSignal(
            client.get(),
            NM_CLIENT_DEVICE_ADDED,
            G_CALLBACK(+[](NMClient*, NMDevice*, gpointer userData) {
                static_cast<Impl*>(userData)->handleDeviceTopologyChanged();
            })));
        clientSignals.push_back(connectSignal(
            client.get(),
            NM_CLIENT_DEVICE_REMOVED,
            G_CALLBACK(+[](NMClient*, NMDevice*, gpointer userData) {
                static_cast<Impl*>(userData)->handleDeviceTopologyChanged();
            })));
        clientSignals.push_back(connectSignal(
            client.get(),
            NM_CLIENT_CONNECTION_ADDED,
            G_CALLBACK(+[](NMClient*, NMRemoteConnection*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        clientSignals.push_back(connectSignal(
            client.get(),
            NM_CLIENT_CONNECTION_REMOVED,
            G_CALLBACK(+[](NMClient*, NMRemoteConnection*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        clientSignals.push_back(connectSignal(
            client.get(),
            NM_CLIENT_ACTIVE_CONNECTION_ADDED,
            G_CALLBACK(+[](NMClient*, NMActiveConnection*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        clientSignals.push_back(connectSignal(
            client.get(),
            NM_CLIENT_ACTIVE_CONNECTION_REMOVED,
            G_CALLBACK(+[](NMClient*, NMActiveConnection*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        clientSignals.push_back(connectSignal(
            client.get(),
            "notify::" NM_CLIENT_NM_RUNNING,
            G_CALLBACK(+[](GObject*, GParamSpec*, gpointer userData) {
                auto* self = static_cast<Impl*>(userData);
                self->rebindWifiDevice();
                self->scheduleRefresh(false);
            })));
    }

    void bindDeviceSignals()
    {
        disconnectSignals(deviceSignals);
        if (!wifiDevice) {
            return;
        }

        deviceSignals.push_back(connectSignal(
            wifiDevice,
            "notify::" NM_DEVICE_WIFI_LAST_SCAN,
            G_CALLBACK(+[](GObject*, GParamSpec*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        deviceSignals.push_back(connectSignal(
            wifiDevice,
            "notify::" NM_DEVICE_WIFI_ACTIVE_ACCESS_POINT,
            G_CALLBACK(+[](GObject*, GParamSpec*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        deviceSignals.push_back(connectSignal(
            wifiDevice,
            "notify::" NM_DEVICE_ACTIVE_CONNECTION,
            G_CALLBACK(+[](GObject*, GParamSpec*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        deviceSignals.push_back(connectSignal(
            wifiDevice,
            "notify::" NM_DEVICE_WIFI_ACCESS_POINTS,
            G_CALLBACK(+[](GObject*, GParamSpec*, gpointer userData) {
                static_cast<Impl*>(userData)->scheduleRefresh(false);
            })));
        deviceSignals.push_back(connectSignal(
            wifiDevice,
            "state-changed",
            G_CALLBACK(+[](NMDevice*, guint newState, guint, guint, gpointer userData) {
                static_cast<Impl*>(userData)->handleWifiDeviceStateChanged(
                    static_cast<NMDeviceState>(newState));
            })));
    }

    void handleDeviceTopologyChanged()
    {
        rebindWifiDevice();
        scheduleRefresh(false);
    }

    void handleWifiDeviceStateChanged(NMDeviceState state)
    {
        if (!connectProgress.has_value()) {
            return;
        }
        if (connectCancelRequested) {
            return;
        }

        const auto phase = wifiConnectPhaseFromDeviceState(state);
        if (!phase.has_value()) {
            return;
        }
        if (connectProgress->phase == phase.value()) {
            return;
        }

        connectProgress->phase = phase.value();
        connectProgress->canCancel = phase.value() != Network::WifiConnectPhase::Canceling;
        updateCachedSnapshotConnectProgress();
    }

    void rebindWifiDevice()
    {
        const auto newDevice = client ? Network::Internal::findWifiDevice(client.get()) : nullptr;
        if (newDevice == wifiDevice) {
            return;
        }

        disconnectSignals(deviceSignals);
        wifiDevice = newDevice;
        bindDeviceSignals();
    }

    SignalConnection connectSignal(
        gpointer instance, const char* detailedSignal, GCallback callback)
    {
        SignalConnection connection;
        if (!instance) {
            return connection;
        }

        connection.instance = instance;
        connection.handlerId = g_signal_connect(instance, detailedSignal, callback, this);
        return connection;
    }

    void disconnectSignals(std::vector<SignalConnection>& signals)
    {
        for (auto& signal : signals) {
            signal.disconnect();
        }
        signals.clear();
    }

    void clearConnectProgress()
    {
        if (!connectProgress.has_value()) {
            return;
        }

        connectProgress.reset();
        updateCachedSnapshotConnectProgress();
    }

    void clearConnectOutcome()
    {
        if (!connectOutcome.has_value()) {
            return;
        }

        connectOutcome.reset();
        updateCachedSnapshotConnectOutcome();
    }

    void setConnectProgress(const Network::WifiConnectProgress& progress)
    {
        if (wifiConnectProgressEqual(
                connectProgress, std::optional<Network::WifiConnectProgress>(progress))) {
            return;
        }

        connectProgress = progress;
        updateCachedSnapshotConnectProgress();
    }

    void setConnectOutcome(const Network::WifiConnectOutcome& outcome)
    {
        if (wifiConnectOutcomeEqual(
                connectOutcome, std::optional<Network::WifiConnectOutcome>(outcome))) {
            return;
        }

        connectOutcome = outcome;
        updateCachedSnapshotConnectOutcome();
    }

    void updateCachedSnapshotConnectProgress()
    {
        if (!cachedSnapshot.has_value()) {
            return;
        }
        if (wifiConnectProgressEqual(cachedSnapshot->snapshot.connectProgress, connectProgress)) {
            return;
        }

        cachedSnapshot->snapshot.connectProgress = connectProgress;
        notifySnapshotChanged(cachedSnapshot->snapshot);
    }

    void updateCachedSnapshotConnectOutcome()
    {
        if (!cachedSnapshot.has_value()) {
            return;
        }
        if (wifiConnectOutcomeEqual(cachedSnapshot->snapshot.connectOutcome, connectOutcome)) {
            return;
        }

        cachedSnapshot->snapshot.connectOutcome = connectOutcome;
        notifySnapshotChanged(cachedSnapshot->snapshot);
    }

    void scheduleRefresh(bool forceScan)
    {
        refreshForceScan = refreshForceScan || forceScan;
        if (refreshScheduled || stopRequested || !mainContext) {
            return;
        }

        GSource* source = g_idle_source_new();
        g_source_set_callback(
            source,
            +[](gpointer userData) -> gboolean {
                auto* self = static_cast<Impl*>(userData);
                self->scheduledRefreshSourceId = 0;
                self->refreshScheduled = false;
                const bool force = self->refreshForceScan;
                self->refreshForceScan = false;
                self->refreshSnapshot(force);
                return G_SOURCE_REMOVE;
            },
            this,
            nullptr);
        scheduledRefreshSourceId = g_source_attach(source, mainContext);
        g_source_unref(source);
        refreshScheduled = true;
    }

    void refreshSnapshot(bool forceScan)
    {
        if (forceScan) {
            updateScanInProgress(true);
        }

        if (!ensureClient()) {
            updateScanInProgress(false);
            return;
        }

        rebindWifiDevice();

        const auto statusResult = Network::Internal::getStatus(client.get());
        const auto networksResult = Network::Internal::listNetworks(
            client.get(),
            Network::Internal::WifiListOptions{
                .forceScan = forceScan,
                .mainContext = mainContext,
            });

        if (statusResult.isError()) {
            lastRefreshError = statusResult.errorValue();
            updateScanInProgress(false);
            LOG_WARN(Network, "NetworkService refresh failed: {}", lastRefreshError);
            return;
        }
        if (networksResult.isError()) {
            lastRefreshError = networksResult.errorValue();
            updateScanInProgress(false);
            LOG_WARN(Network, "NetworkService refresh failed: {}", lastRefreshError);
            return;
        }

        scanInProgress = false;
        auto refreshedNetworks = networksResult.value();
        if (!forceScan && cachedSnapshot.has_value()) {
            refreshedNetworks =
                mergePassiveNetworkList(cachedSnapshot->snapshot.networks, refreshedNetworks);
        }
        const Snapshot refreshedSnapshot{
            .status = statusResult.value(),
            .networks = std::move(refreshedNetworks),
            .localAddresses = collectLocalAddresses(),
            .connectOutcome = connectOutcome,
            .connectProgress = connectProgress,
            .scanInProgress = false,
        };
        const bool changed = !cachedSnapshot.has_value()
            || !snapshotsEqual(cachedSnapshot->snapshot, refreshedSnapshot);
        cachedSnapshot = CachedSnapshot{ .snapshot = refreshedSnapshot, .updatedAt = Clock::now() };
        lastRefreshError.clear();
        LOG_INFO(
            Network,
            "NetworkService snapshot refreshed ({} networks).",
            cachedSnapshot->snapshot.networks.size());
        if (changed) {
            notifySnapshotChanged(refreshedSnapshot);
        }
    }

    void notifySnapshotChanged(const Snapshot& snapshot)
    {
        SnapshotChangedCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            callback = snapshotChangedCallback;
        }

        if (callback) {
            callback(snapshot);
        }
    }

    void updateScanInProgress(bool inProgress)
    {
        if (scanInProgress == inProgress) {
            return;
        }

        scanInProgress = inProgress;
        if (!cachedSnapshot.has_value()) {
            return;
        }

        cachedSnapshot->snapshot.scanInProgress = inProgress;
        notifySnapshotChanged(cachedSnapshot->snapshot);
    }
};

NetworkService::NetworkService() : pImpl_()
{}

NetworkService::~NetworkService()
{
    pImpl_->stop();
}

Result<std::monostate, std::string> NetworkService::start()
{
    return pImpl_->start();
}

void NetworkService::stop()
{
    pImpl_->stop();
}

Result<NetworkService::Snapshot, std::string> NetworkService::getSnapshot(bool forceRefresh)
{
    return pImpl_->getSnapshot(forceRefresh);
}

Result<std::monostate, std::string> NetworkService::cancelConnect()
{
    return pImpl_->cancelConnect();
}

Result<Network::WifiConnectResult, std::string> NetworkService::connectBySsid(
    const std::string& ssid, const std::optional<std::string>& password)
{
    return pImpl_->connectBySsid(ssid, password);
}

Result<Network::WifiDisconnectResult, std::string> NetworkService::disconnect(
    const std::optional<std::string>& ssid)
{
    return pImpl_->disconnect(ssid);
}

Result<Network::WifiForgetResult, std::string> NetworkService::forget(const std::string& ssid)
{
    return pImpl_->forget(ssid);
}

Result<std::monostate, std::string> NetworkService::requestScan()
{
    return pImpl_->requestScan();
}

void NetworkService::setSnapshotChangedCallback(SnapshotChangedCallback callback)
{
    pImpl_->setSnapshotChangedCallback(std::move(callback));
}

} // namespace OsManager
} // namespace DirtSim
