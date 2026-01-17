#include "WifiManager.h"
#include "core/LoggingChannels.h"

#include <NetworkManager.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <glib-object.h>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace {

struct GErrorDeleter {
    void operator()(GError* error) const
    {
        if (error) {
            g_error_free(error);
        }
    }
};

struct GObjectDeleter {
    void operator()(gpointer object) const
    {
        if (object) {
            g_object_unref(object);
        }
    }
};

bool deleteRemoteConnection(NMRemoteConnection* connection, std::string& errorMessage);
bool connectionHasCredentials(NMConnection* connection);

template <typename T>
using GObjectPtr = std::unique_ptr<T, GObjectDeleter>;

struct AsyncLoop {
    GMainLoop* loop = nullptr;
    guint timeoutId = 0;
    bool timedOut = false;
};

struct SignalHandlerGuard {
    gpointer instance = nullptr;
    gulong handlerId = 0;

    SignalHandlerGuard() = default;
    SignalHandlerGuard(gpointer instanceIn, gulong handlerIdIn)
        : instance(instanceIn), handlerId(handlerIdIn)
    {}

    SignalHandlerGuard(const SignalHandlerGuard&) = delete;
    SignalHandlerGuard& operator=(const SignalHandlerGuard&) = delete;
    SignalHandlerGuard(SignalHandlerGuard&&) = default;
    SignalHandlerGuard& operator=(SignalHandlerGuard&&) = default;

    ~SignalHandlerGuard()
    {
        if (instance && handlerId != 0) {
            g_signal_handler_disconnect(instance, handlerId);
        }
    }
};

gboolean onLoopTimeout(gpointer data)
{
    auto* loop = static_cast<AsyncLoop*>(data);
    loop->timedOut = true;
    g_main_loop_quit(loop->loop);
    return G_SOURCE_REMOVE;
}

class AsyncLoopGuard {
public:
    explicit AsyncLoopGuard(int timeoutSeconds)
    {
        loop_ = std::make_unique<AsyncLoop>();
        loop_->loop = g_main_loop_new(nullptr, FALSE);
        if (timeoutSeconds > 0) {
            loop_->timeoutId = g_timeout_add_seconds(
                static_cast<guint>(timeoutSeconds), onLoopTimeout, loop_.get());
        }
    }

    AsyncLoopGuard(const AsyncLoopGuard&) = delete;
    AsyncLoopGuard& operator=(const AsyncLoopGuard&) = delete;
    AsyncLoopGuard(AsyncLoopGuard&&) = default;
    AsyncLoopGuard& operator=(AsyncLoopGuard&&) = default;

    ~AsyncLoopGuard()
    {
        if (!loop_) {
            return;
        }
        if (loop_->timeoutId != 0) {
            g_source_remove(loop_->timeoutId);
            loop_->timeoutId = 0;
        }
        if (loop_->loop) {
            g_main_loop_unref(loop_->loop);
            loop_->loop = nullptr;
        }
    }

    AsyncLoop* get() const { return loop_.get(); }
    GMainLoop* mainLoop() const { return loop_ ? loop_->loop : nullptr; }
    bool timedOut() const { return loop_ ? loop_->timedOut : false; }

private:
    std::unique_ptr<AsyncLoop> loop_;
};

std::string formatError(GError* error, const std::string& fallback)
{
    if (error && error->message) {
        return std::string(error->message);
    }
    return fallback;
}

GObjectPtr<NMClient> createClient(std::string& errorMessage)
{
    GError* error = nullptr;
    NMClient* client = nm_client_new(nullptr, &error);
    if (!client) {
        errorMessage = formatError(error, "Failed to initialize NetworkManager client");
        g_clear_error(&error);
        return nullptr;
    }

    return GObjectPtr<NMClient>(client);
}

NMDeviceWifi* findWifiDevice(NMClient* client)
{
    const GPtrArray* devices = nm_client_get_devices(client);
    if (!devices) {
        return nullptr;
    }

    for (guint i = 0; i < devices->len; ++i) {
        auto* device = static_cast<NMDevice*>(g_ptr_array_index(devices, i));
        if (device && NM_IS_DEVICE_WIFI(device)) {
            return NM_DEVICE_WIFI(device);
        }
    }

    return nullptr;
}

std::string ssidFromBytes(GBytes* bytes)
{
    if (!bytes) {
        return "";
    }

    gsize length = 0;
    const auto* data = static_cast<const guint8*>(g_bytes_get_data(bytes, &length));
    if (!data || length == 0) {
        return "";
    }

    char* ssid = nm_utils_ssid_to_utf8(data, length);
    if (!ssid) {
        return "";
    }

    std::string result(ssid);
    g_free(ssid);
    return result;
}

std::string ssidFromAccessPoint(NMAccessPoint* accessPoint)
{
    if (!accessPoint) {
        return "";
    }

    return ssidFromBytes(nm_access_point_get_ssid(accessPoint));
}

std::string ssidFromConnection(NMConnection* connection)
{
    if (!connection) {
        return "";
    }

    auto* wireless = nm_connection_get_setting_wireless(connection);
    if (!wireless) {
        return "";
    }

    return ssidFromBytes(nm_setting_wireless_get_ssid(wireless));
}

std::optional<int> strengthToDbm(guint8 strength)
{
    if (strength == 0) {
        return std::nullopt;
    }

    return static_cast<int>(strength) - 100;
}

std::string securityFromAccessPoint(NMAccessPoint* accessPoint)
{
    if (!accessPoint) {
        return "unknown";
    }

    const auto flags = nm_access_point_get_flags(accessPoint);
    const auto wpaFlags = nm_access_point_get_wpa_flags(accessPoint);
    const auto rsnFlags = nm_access_point_get_rsn_flags(accessPoint);

#ifdef NM_802_11_AP_SEC_KEY_MGMT_SAE
    if (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_SAE) {
        return "wpa3";
    }
#endif
    if (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_PSK) {
        return "wpa2";
    }
    if (wpaFlags & NM_802_11_AP_SEC_KEY_MGMT_PSK) {
        return "wpa";
    }
    if (flags & NM_802_11_AP_FLAGS_PRIVACY) {
        return "wep";
    }

    return "open";
}

bool isHexString(const std::string& value)
{
    if (value.empty()) {
        return false;
    }

    return std::all_of(
        value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

NMWepKeyType wepKeyTypeForPassword(const std::string& password)
{
    const size_t length = password.size();
    if (length == 5 || length == 13) {
        return NM_WEP_KEY_TYPE_KEY;
    }
    if ((length == 10 || length == 26) && isHexString(password)) {
        return NM_WEP_KEY_TYPE_KEY;
    }

    return NM_WEP_KEY_TYPE_PASSPHRASE;
}

std::string securityFromConnection(NMConnection* connection)
{
    if (!connection) {
        return "unknown";
    }

    auto* security = nm_connection_get_setting_wireless_security(connection);
    if (!security) {
        return "open";
    }

    const char* keyMgmt = nm_setting_wireless_security_get_key_mgmt(security);
    if (!keyMgmt || *keyMgmt == '\0') {
        return "unknown";
    }

    if (std::strcmp(keyMgmt, "sae") == 0) {
        return "wpa3";
    }
    if (std::strcmp(keyMgmt, "wpa-psk") == 0) {
        return "wpa2";
    }
    if (std::strcmp(keyMgmt, "none") == 0) {
        return "wep";
    }

    return keyMgmt;
}

int64_t connectionTimestamp(NMConnection* connection)
{
    if (!connection) {
        return 0;
    }

    auto* setting = nm_connection_get_setting_connection(connection);
    if (!setting) {
        return 0;
    }

    return static_cast<int64_t>(nm_setting_connection_get_timestamp(setting));
}

std::optional<std::string> formatDate(int64_t timestamp)
{
    if (timestamp <= 0) {
        return std::nullopt;
    }

    const std::time_t timeValue = static_cast<std::time_t>(timestamp);
    std::tm localTime{};
    if (localtime_r(&timeValue, &localTime) == nullptr) {
        return std::nullopt;
    }

    char buffer[16];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &localTime) == 0) {
        return std::nullopt;
    }

    return std::string(buffer);
}

std::string formatRelative(int64_t timestamp)
{
    if (timestamp <= 0) {
        return "never";
    }

    const auto now = std::chrono::system_clock::now();
    const auto then = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(timestamp));

    auto delta = now - then;
    if (delta < std::chrono::seconds(0)) {
        delta = std::chrono::seconds(0);
    }

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(delta).count();

    if (seconds < 60) {
        return "just now";
    }
    if (seconds < 3600) {
        return std::to_string(seconds / 60) + "m ago";
    }
    if (seconds < 86400) {
        return std::to_string(seconds / 3600) + "h ago";
    }
    return std::to_string(seconds / 86400) + "d ago";
}

const char* deviceStateToString(NMDeviceState state)
{
    switch (state) {
        case NM_DEVICE_STATE_UNKNOWN:
            return "unknown";
        case NM_DEVICE_STATE_UNMANAGED:
            return "unmanaged";
        case NM_DEVICE_STATE_UNAVAILABLE:
            return "unavailable";
        case NM_DEVICE_STATE_DISCONNECTED:
            return "disconnected";
        case NM_DEVICE_STATE_PREPARE:
            return "prepare";
        case NM_DEVICE_STATE_CONFIG:
            return "config";
        case NM_DEVICE_STATE_NEED_AUTH:
            return "need-auth";
        case NM_DEVICE_STATE_IP_CONFIG:
            return "ip-config";
        case NM_DEVICE_STATE_IP_CHECK:
            return "ip-check";
        case NM_DEVICE_STATE_SECONDARIES:
            return "secondaries";
        case NM_DEVICE_STATE_ACTIVATED:
            return "activated";
        case NM_DEVICE_STATE_DEACTIVATING:
            return "deactivating";
        case NM_DEVICE_STATE_FAILED:
            return "failed";
    }
    return "unknown";
}

const char* deviceStateReasonToString(NMDeviceStateReason reason)
{
    GEnumClass* enumClass = static_cast<GEnumClass*>(g_type_class_ref(NM_TYPE_DEVICE_STATE_REASON));
    const GEnumValue* value =
        enumClass ? g_enum_get_value(enumClass, static_cast<int>(reason)) : nullptr;
    const char* name = (value && value->value_nick) ? value->value_nick : "unknown";
    if (enumClass) {
        g_type_class_unref(enumClass);
    }
    return name;
}

bool waitForDeviceActivation(NMDevice* device, int timeoutSeconds, std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    const auto initialState = nm_device_get_state(device);
    if (initialState == NM_DEVICE_STATE_ACTIVATED) {
        return true;
    }
    if (initialState == NM_DEVICE_STATE_FAILED || initialState == NM_DEVICE_STATE_NEED_AUTH) {
        errorMessage = std::string("WiFi activation failed (state=")
            + deviceStateToString(initialState)
            + ", reason=" + deviceStateReasonToString(nm_device_get_state_reason(device)) + ")";
        return false;
    }

    struct ActivationContext {
        AsyncLoop* loop = nullptr;
        std::string* error = nullptr;
        bool activated = false;
        NMDeviceState lastState = NM_DEVICE_STATE_UNKNOWN;
        NMDeviceStateReason lastReason = NM_DEVICE_STATE_REASON_NONE;
    };

    AsyncLoopGuard loop(timeoutSeconds);
    ActivationContext context{
        loop.get(), &errorMessage, false, initialState, nm_device_get_state_reason(device)
    };

    const gulong handlerId = g_signal_connect(
        device,
        "state-changed",
        G_CALLBACK(
            +[](NMDevice* device, guint newState, guint oldState, guint reason, gpointer userData) {
                auto* ctx = static_cast<ActivationContext*>(userData);
                const auto newStateValue = static_cast<NMDeviceState>(newState);
                const auto oldStateValue = static_cast<NMDeviceState>(oldState);
                const auto reasonValue = static_cast<NMDeviceStateReason>(reason);
                const char* iface = device ? nm_device_get_iface(device) : nullptr;

                LOG_INFO(
                    Network,
                    "WiFi device {} state change: {} -> {} (reason: {}).",
                    iface && *iface ? iface : "unknown",
                    deviceStateToString(oldStateValue),
                    deviceStateToString(newStateValue),
                    deviceStateReasonToString(reasonValue));

                ctx->lastState = newStateValue;
                ctx->lastReason = reasonValue;

                if (newStateValue == NM_DEVICE_STATE_ACTIVATED) {
                    ctx->activated = true;
                    g_main_loop_quit(ctx->loop->loop);
                    return;
                }

                if (newStateValue == NM_DEVICE_STATE_FAILED
                    || newStateValue == NM_DEVICE_STATE_NEED_AUTH
                    || newStateValue == NM_DEVICE_STATE_DISCONNECTED
                    || newStateValue == NM_DEVICE_STATE_UNAVAILABLE
                    || newStateValue == NM_DEVICE_STATE_UNMANAGED) {
                    if (ctx->error && ctx->error->empty()) {
                        *ctx->error = std::string("WiFi activation failed (state=")
                            + deviceStateToString(newStateValue)
                            + ", reason=" + deviceStateReasonToString(reasonValue) + ")";
                    }
                    g_main_loop_quit(ctx->loop->loop);
                }
            }),
        &context);

    SignalHandlerGuard signalGuard(device, handlerId);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi activation timed out";
    }
    if (!context.activated && errorMessage.empty()) {
        errorMessage = std::string("WiFi activation failed (state=")
            + deviceStateToString(context.lastState)
            + ", reason=" + deviceStateReasonToString(context.lastReason) + ")";
    }

    return context.activated;
}

bool waitForDeviceDeactivation(NMDevice* device, int timeoutSeconds, std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    const auto initialState = nm_device_get_state(device);
    if (initialState == NM_DEVICE_STATE_DISCONNECTED || initialState == NM_DEVICE_STATE_UNAVAILABLE
        || initialState == NM_DEVICE_STATE_UNMANAGED) {
        return true;
    }
    if (initialState == NM_DEVICE_STATE_FAILED) {
        errorMessage = std::string("WiFi disconnect failed (state=")
            + deviceStateToString(initialState)
            + ", reason=" + deviceStateReasonToString(nm_device_get_state_reason(device)) + ")";
        return false;
    }

    struct DeactivationContext {
        AsyncLoop* loop = nullptr;
        std::string* error = nullptr;
        bool deactivated = false;
        NMDeviceState lastState = NM_DEVICE_STATE_UNKNOWN;
        NMDeviceStateReason lastReason = NM_DEVICE_STATE_REASON_NONE;
    };

    AsyncLoopGuard loop(timeoutSeconds);
    DeactivationContext context{
        loop.get(), &errorMessage, false, initialState, nm_device_get_state_reason(device)
    };

    const gulong handlerId = g_signal_connect(
        device,
        "state-changed",
        G_CALLBACK(
            +[](NMDevice* device, guint newState, guint oldState, guint reason, gpointer userData) {
                auto* ctx = static_cast<DeactivationContext*>(userData);
                const auto newStateValue = static_cast<NMDeviceState>(newState);
                const auto oldStateValue = static_cast<NMDeviceState>(oldState);
                const auto reasonValue = static_cast<NMDeviceStateReason>(reason);
                const char* iface = device ? nm_device_get_iface(device) : nullptr;

                LOG_INFO(
                    Network,
                    "WiFi device {} state change: {} -> {} (reason: {}).",
                    iface && *iface ? iface : "unknown",
                    deviceStateToString(oldStateValue),
                    deviceStateToString(newStateValue),
                    deviceStateReasonToString(reasonValue));

                ctx->lastState = newStateValue;
                ctx->lastReason = reasonValue;

                if (newStateValue == NM_DEVICE_STATE_DISCONNECTED
                    || newStateValue == NM_DEVICE_STATE_UNAVAILABLE
                    || newStateValue == NM_DEVICE_STATE_UNMANAGED) {
                    ctx->deactivated = true;
                    g_main_loop_quit(ctx->loop->loop);
                    return;
                }

                if (newStateValue == NM_DEVICE_STATE_FAILED) {
                    if (ctx->error && ctx->error->empty()) {
                        *ctx->error = std::string("WiFi disconnect failed (state=")
                            + deviceStateToString(newStateValue)
                            + ", reason=" + deviceStateReasonToString(reasonValue) + ")";
                    }
                    g_main_loop_quit(ctx->loop->loop);
                }
            }),
        &context);

    SignalHandlerGuard signalGuard(device, handlerId);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi disconnect timed out";
    }
    if (!context.deactivated && errorMessage.empty()) {
        errorMessage = std::string("WiFi disconnect failed (state=")
            + deviceStateToString(context.lastState)
            + ", reason=" + deviceStateReasonToString(context.lastReason) + ")";
    }

    return context.deactivated;
}

bool requestWifiScan(NMDeviceWifi* device, std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    struct ScanContext {
        AsyncLoop* loop = nullptr;
        std::string* error = nullptr;
    };

    AsyncLoopGuard loop(5);
    ScanContext context{ loop.get(), &errorMessage };

    nm_device_wifi_request_scan_async(
        device,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
            auto* ctx = static_cast<ScanContext*>(userData);
            GError* error = nullptr;
            if (!nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(source), result, &error)) {
                *ctx->error = formatError(error, "WiFi scan failed");
                g_clear_error(&error);
            }
            g_main_loop_quit(ctx->loop->loop);
        },
        &context);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi scan timed out";
    }

    return errorMessage.empty();
}

std::vector<DirtSim::Network::WifiAccessPointInfo> collectAccessPoints(
    NMDeviceWifi* device, std::optional<std::string>& scanError)
{
    std::vector<DirtSim::Network::WifiAccessPointInfo> results;
    if (!device) {
        return results;
    }

    {
        std::string errorMessage;
        if (!requestWifiScan(device, errorMessage)) {
            scanError = errorMessage;
        }
    }

    NMAccessPoint* activeAp = nm_device_wifi_get_active_access_point(device);
    const GPtrArray* accessPoints = nm_device_wifi_get_access_points(device);
    if (!accessPoints) {
        return results;
    }

    results.reserve(accessPoints->len);
    for (guint i = 0; i < accessPoints->len; ++i) {
        auto* ap = static_cast<NMAccessPoint*>(g_ptr_array_index(accessPoints, i));
        if (!ap) {
            continue;
        }

        const std::string ssid = ssidFromAccessPoint(ap);
        if (ssid.empty()) {
            continue;
        }

        DirtSim::Network::WifiAccessPointInfo info{
            .ssid = ssid,
            .bssid = nm_access_point_get_bssid(ap) ? nm_access_point_get_bssid(ap) : "",
            .signalDbm = strengthToDbm(nm_access_point_get_strength(ap)),
            .frequencyMhz = std::nullopt,
            .channel = std::nullopt,
            .security = securityFromAccessPoint(ap),
            .active = (activeAp == ap),
        };

        const auto frequency = nm_access_point_get_frequency(ap);
        if (frequency > 0) {
            info.frequencyMhz = static_cast<int>(frequency);
            info.channel = static_cast<int>(nm_utils_wifi_freq_to_channel(frequency));
        }

        results.push_back(info);
    }

    std::sort(
        results.begin(),
        results.end(),
        [](const DirtSim::Network::WifiAccessPointInfo& a,
           const DirtSim::Network::WifiAccessPointInfo& b) {
            if (a.ssid != b.ssid) {
                return a.ssid < b.ssid;
            }
            const int signalA = a.signalDbm.has_value() ? a.signalDbm.value() : -200;
            const int signalB = b.signalDbm.has_value() ? b.signalDbm.value() : -200;
            if (signalA != signalB) {
                return signalA > signalB;
            }
            return a.bssid < b.bssid;
        });

    return results;
}

struct WifiConnectionCandidate {
    NMConnection* connection = nullptr;
    std::string id;
    std::string ssid;
    std::string uuid;
    int64_t timestamp = 0;
    bool active = false;
    bool autoConnect = false;
    bool hasCredentials = false;
};

enum class WifiConnectionMatch { ById, BySsid };

std::vector<WifiConnectionCandidate> collectWifiConnections(
    NMClient* client, NMDeviceWifi* device, const std::string& value, WifiConnectionMatch match)
{
    std::vector<WifiConnectionCandidate> candidates;
    if (!client || value.empty()) {
        return candidates;
    }

    std::string activeUuid;
    if (device) {
        NMActiveConnection* activeConnection = nm_device_get_active_connection(NM_DEVICE(device));
        const char* uuid =
            activeConnection ? nm_active_connection_get_uuid(activeConnection) : nullptr;
        if (uuid) {
            activeUuid = uuid;
        }
    }

    const GPtrArray* connections = nm_client_get_connections(client);
    if (!connections) {
        return candidates;
    }

    for (guint i = 0; i < connections->len; ++i) {
        auto* connection = static_cast<NMConnection*>(g_ptr_array_index(connections, i));
        if (!connection) {
            continue;
        }

        auto* setting = nm_connection_get_setting_connection(connection);
        const char* type = setting ? nm_setting_connection_get_connection_type(setting) : nullptr;
        if (!type || std::strcmp(type, NM_SETTING_WIRELESS_SETTING_NAME) != 0) {
            continue;
        }

        const char* id = nm_connection_get_id(connection);
        std::string connectionId = id ? id : "";
        std::string connectionSsid = ssidFromConnection(connection);
        if (connectionSsid.empty()) {
            connectionSsid = connectionId;
        }

        const bool matches = (match == WifiConnectionMatch::ById)
            ? (connectionId == value)
            : (!connectionSsid.empty() && connectionSsid == value);
        if (!matches) {
            continue;
        }

        WifiConnectionCandidate candidate{
            .connection = connection,
            .id = connectionId,
            .ssid = connectionSsid,
            .uuid = nm_connection_get_uuid(connection) ? nm_connection_get_uuid(connection) : "",
            .timestamp = connectionTimestamp(connection),
            .active = false,
            .autoConnect =
                setting ? nm_setting_connection_get_autoconnect(setting) != FALSE : false,
            .hasCredentials = connectionHasCredentials(connection),
        };
        candidate.active =
            (!activeUuid.empty() && !candidate.uuid.empty() && candidate.uuid == activeUuid);

        candidates.push_back(candidate);
    }

    return candidates;
}

const WifiConnectionCandidate* selectBestWifiConnection(
    const std::string& matchLabel,
    const std::string& matchValue,
    std::vector<WifiConnectionCandidate>& candidates)
{
    if (candidates.empty()) {
        return nullptr;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const WifiConnectionCandidate& a, const WifiConnectionCandidate& b) {
            if (a.active != b.active) {
                return a.active;
            }
            if (a.hasCredentials != b.hasCredentials) {
                return a.hasCredentials;
            }
            if (a.timestamp != b.timestamp) {
                return a.timestamp > b.timestamp;
            }
            if (a.autoConnect != b.autoConnect) {
                return a.autoConnect;
            }
            return a.id < b.id;
        });

    if (candidates.size() > 1) {
        const auto& chosen = candidates.front();
        const std::string lastUsed = formatRelative(chosen.timestamp);
        LOG_INFO(
            Network,
            "Multiple saved WiFi connections for {} '{}' (count={}, chosen_uuid={}, last_used={}, "
            "has_credentials={}, auto_connect={}).",
            matchLabel,
            matchValue,
            candidates.size(),
            chosen.uuid.empty() ? "unknown" : chosen.uuid,
            lastUsed,
            chosen.hasCredentials,
            chosen.autoConnect);
    }

    return &candidates.front();
}

bool deleteWifiConnectionsBySsid(
    NMClient* client,
    const std::string& ssid,
    const std::string& keepUuid,
    int& removed,
    std::string& errorMessage)
{
    removed = 0;
    if (!client) {
        errorMessage = "No WiFi client available";
        return false;
    }

    const GPtrArray* connections = nm_client_get_connections(client);
    if (!connections) {
        errorMessage = "No saved WiFi connections";
        return false;
    }

    std::vector<GObjectPtr<NMRemoteConnection>> toDelete;
    for (guint i = 0; i < connections->len; ++i) {
        auto* connection = static_cast<NMConnection*>(g_ptr_array_index(connections, i));
        if (!connection) {
            continue;
        }

        auto* setting = nm_connection_get_setting_connection(connection);
        const char* type = setting ? nm_setting_connection_get_connection_type(setting) : nullptr;
        if (!type || std::strcmp(type, NM_SETTING_WIRELESS_SETTING_NAME) != 0) {
            continue;
        }

        std::string connectionSsid = ssidFromConnection(connection);
        const char* id = nm_connection_get_id(connection);
        if (connectionSsid.empty()) {
            connectionSsid = id ? id : "";
        }

        if (connectionSsid != ssid && (!id || ssid != id)) {
            continue;
        }

        const char* uuid = nm_connection_get_uuid(connection);
        if (!keepUuid.empty() && uuid && keepUuid == uuid) {
            continue;
        }

        if (!NM_IS_REMOTE_CONNECTION(connection)) {
            errorMessage = "WiFi connection cannot be removed";
            return false;
        }

        auto* remote = NM_REMOTE_CONNECTION(g_object_ref(connection));
        toDelete.emplace_back(remote);
    }

    for (const auto& connection : toDelete) {
        std::string deleteError;
        if (!deleteRemoteConnection(connection.get(), deleteError)) {
            errorMessage = deleteError;
            return false;
        }
        ++removed;
    }

    return true;
}

NMAccessPoint* findBestAccessPoint(NMDeviceWifi* device, const std::string& ssid)
{
    if (!device || ssid.empty()) {
        return nullptr;
    }

    const GPtrArray* accessPoints = nm_device_wifi_get_access_points(device);
    if (!accessPoints) {
        return nullptr;
    }

    NMAccessPoint* best = nullptr;
    int bestSignal = -200;

    for (guint i = 0; i < accessPoints->len; ++i) {
        auto* ap = static_cast<NMAccessPoint*>(g_ptr_array_index(accessPoints, i));
        if (!ap) {
            continue;
        }

        if (ssidFromAccessPoint(ap) != ssid) {
            continue;
        }

        const auto signal = strengthToDbm(nm_access_point_get_strength(ap));
        const int signalValue = signal.has_value() ? signal.value() : -200;
        if (!best || signalValue > bestSignal) {
            best = ap;
            bestSignal = signalValue;
        }
    }

    return best;
}

GObjectPtr<NMConnection> buildConnectionForSsid(
    const std::string& ssid, const std::optional<std::string>& password, NMAccessPoint* accessPoint)
{
    auto* connection = nm_simple_connection_new();
    if (!connection) {
        return nullptr;
    }

    auto* settingConnection = nm_setting_connection_new();
    char* uuid = nm_utils_uuid_generate();
    g_object_set(
        G_OBJECT(settingConnection),
        NM_SETTING_CONNECTION_ID,
        ssid.c_str(),
        NM_SETTING_CONNECTION_UUID,
        uuid,
        NM_SETTING_CONNECTION_TYPE,
        NM_SETTING_WIRELESS_SETTING_NAME,
        NM_SETTING_CONNECTION_AUTOCONNECT,
        TRUE,
        nullptr);
    g_free(uuid);
    nm_connection_add_setting(connection, NM_SETTING(settingConnection));

    auto* settingWireless = nm_setting_wireless_new();
    GBytes* ssidBytes = g_bytes_new(ssid.data(), ssid.size());
    g_object_set(G_OBJECT(settingWireless), NM_SETTING_WIRELESS_SSID, ssidBytes, nullptr);
    g_bytes_unref(ssidBytes);

    nm_connection_add_setting(connection, NM_SETTING(settingWireless));

    if (password.has_value()) {
        auto* settingSecurity = nm_setting_wireless_security_new();
        const std::string security = securityFromAccessPoint(accessPoint);
        if (security == "wep") {
            g_object_set(
                G_OBJECT(settingSecurity),
                NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
                "none",
                NM_SETTING_WIRELESS_SECURITY_WEP_KEY0,
                password->c_str(),
                NM_SETTING_WIRELESS_SECURITY_WEP_KEY_TYPE,
                wepKeyTypeForPassword(*password),
                nullptr);
        }
        else {
            const char* keyMgmt = (security == "wpa3") ? "sae" : "wpa-psk";
            g_object_set(
                G_OBJECT(settingSecurity),
                NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
                keyMgmt,
                NM_SETTING_WIRELESS_SECURITY_PSK,
                password->c_str(),
                nullptr);
        }
        nm_connection_add_setting(connection, NM_SETTING(settingSecurity));
    }

    auto* settingIp4 = nm_setting_ip4_config_new();
    g_object_set(
        G_OBJECT(settingIp4),
        NM_SETTING_IP_CONFIG_METHOD,
        NM_SETTING_IP4_CONFIG_METHOD_AUTO,
        nullptr);
    nm_connection_add_setting(connection, NM_SETTING(settingIp4));

    auto* settingIp6 = nm_setting_ip6_config_new();
    g_object_set(
        G_OBJECT(settingIp6),
        NM_SETTING_IP_CONFIG_METHOD,
        NM_SETTING_IP6_CONFIG_METHOD_AUTO,
        nullptr);
    nm_connection_add_setting(connection, NM_SETTING(settingIp6));

    return GObjectPtr<NMConnection>(connection);
}

bool activateConnection(
    NMClient* client,
    NMConnection* connection,
    NMDevice* device,
    NMAccessPoint* accessPoint,
    std::string& errorMessage)
{
    if (!client || !connection || !device) {
        errorMessage = "WiFi connection activation failed";
        return false;
    }

    const char* connectionId = nm_connection_get_id(connection);
    LOG_INFO(
        Network,
        "Activating WiFi connection {}.",
        connectionId && *connectionId ? connectionId : "unknown");

    struct ActivateContext {
        AsyncLoop* loop = nullptr;
        NMActiveConnection* active = nullptr;
        std::string* error = nullptr;
    };

    const char* specificObject = accessPoint ? nm_object_get_path(NM_OBJECT(accessPoint)) : nullptr;

    AsyncLoopGuard loop(10);
    ActivateContext context{ loop.get(), nullptr, &errorMessage };

    nm_client_activate_connection_async(
        client,
        connection,
        device,
        specificObject,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
            auto* ctx = static_cast<ActivateContext*>(userData);
            GError* error = nullptr;
            ctx->active = nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
            if (!ctx->active) {
                *ctx->error = formatError(error, "WiFi connection activation failed");
                g_clear_error(&error);
            }
            g_main_loop_quit(ctx->loop->loop);
        },
        &context);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi connection activation timed out";
    }

    if (!context.active) {
        return false;
    }

    g_object_unref(context.active);
    return true;
}

bool addAndActivateConnection(
    NMClient* client,
    NMConnection* connection,
    NMDevice* device,
    NMAccessPoint* accessPoint,
    std::string& errorMessage)
{
    if (!client || !connection || !device) {
        errorMessage = "WiFi connection failed";
        return false;
    }

    const char* connectionId = nm_connection_get_id(connection);
    LOG_INFO(
        Network,
        "Adding WiFi connection {}.",
        connectionId && *connectionId ? connectionId : "unknown");

    struct AddContext {
        AsyncLoop* loop = nullptr;
        NMActiveConnection* active = nullptr;
        std::string* error = nullptr;
    };

    const char* specificObject = accessPoint ? nm_object_get_path(NM_OBJECT(accessPoint)) : nullptr;

    AsyncLoopGuard loop(10);
    AddContext context{ loop.get(), nullptr, &errorMessage };

    nm_client_add_and_activate_connection_async(
        client,
        connection,
        device,
        specificObject,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
            auto* ctx = static_cast<AddContext*>(userData);
            GError* error = nullptr;
            ctx->active =
                nm_client_add_and_activate_connection_finish(NM_CLIENT(source), result, &error);
            if (!ctx->active) {
                *ctx->error = formatError(error, "WiFi connection failed");
                g_clear_error(&error);
            }
            g_main_loop_quit(ctx->loop->loop);
        },
        &context);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi connection timed out";
    }

    if (!context.active) {
        return false;
    }

    g_object_unref(context.active);
    return true;
}

bool deactivateActiveConnection(
    NMClient* client, NMActiveConnection* active, std::string& errorMessage)
{
    if (!client || !active) {
        errorMessage = "No active WiFi connection";
        return false;
    }

    struct DeactivateContext {
        AsyncLoop* loop = nullptr;
        bool success = false;
        std::string* error = nullptr;
    };

    AsyncLoopGuard loop(10);
    DeactivateContext context{ loop.get(), false, &errorMessage };

    nm_client_deactivate_connection_async(
        client,
        active,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
            auto* ctx = static_cast<DeactivateContext*>(userData);
            GError* error = nullptr;
            ctx->success =
                nm_client_deactivate_connection_finish(NM_CLIENT(source), result, &error);
            if (!ctx->success) {
                *ctx->error = formatError(error, "WiFi disconnect failed");
                g_clear_error(&error);
            }
            g_main_loop_quit(ctx->loop->loop);
        },
        &context);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi disconnect timed out";
    }

    return context.success;
}

bool deleteRemoteConnection(NMRemoteConnection* connection, std::string& errorMessage)
{
    if (!connection) {
        errorMessage = "No WiFi connection found";
        return false;
    }

    struct DeleteContext {
        AsyncLoop* loop = nullptr;
        bool success = false;
        std::string* error = nullptr;
    };

    AsyncLoopGuard loop(10);
    DeleteContext context{ loop.get(), false, &errorMessage };

    nm_remote_connection_delete_async(
        connection,
        nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer userData) {
            auto* ctx = static_cast<DeleteContext*>(userData);
            GError* error = nullptr;
            ctx->success =
                nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(source), result, &error);
            if (!ctx->success) {
                *ctx->error = formatError(error, "WiFi forget failed");
                g_clear_error(&error);
            }
            g_main_loop_quit(ctx->loop->loop);
        },
        &context);

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi forget timed out";
    }

    return context.success;
}

bool hasSecretsForSetting(
    NMRemoteConnection* connection, const char* settingName, std::string& errorMessage)
{
    if (!connection || !settingName || *settingName == '\0') {
        errorMessage = "WiFi secrets unavailable";
        return false;
    }

    GError* error = nullptr;
    GVariant* secrets = nm_remote_connection_get_secrets(connection, settingName, nullptr, &error);
    if (!secrets) {
        errorMessage = formatError(error, "WiFi secrets unavailable");
        g_clear_error(&error);
        return false;
    }

    const bool hasSecrets = g_variant_n_children(secrets) > 0;
    g_variant_unref(secrets);
    return hasSecrets;
}

bool connectionHasCredentials(NMConnection* connection)
{
    if (!connection || !NM_IS_REMOTE_CONNECTION(connection)) {
        return false;
    }

    bool hasCredentials = false;
    std::string errorMessage;

    if (nm_connection_get_setting_wireless_security(connection)) {
        hasCredentials = hasSecretsForSetting(
            NM_REMOTE_CONNECTION(connection),
            NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
            errorMessage);
    }

    if (!hasCredentials && nm_connection_get_setting_802_1x(connection)) {
        hasCredentials = hasSecretsForSetting(
            NM_REMOTE_CONNECTION(connection), NM_SETTING_802_1X_SETTING_NAME, errorMessage);
    }

    return hasCredentials;
}

constexpr int kActivationTimeoutSeconds = 20;

} // namespace
} // namespace DirtSim

namespace DirtSim {
namespace Network {

Result<WifiStatus, std::string> WifiManager::getStatus() const
{
    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<WifiStatus, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (!device) {
        return Result<WifiStatus, std::string>::error("No WiFi device found");
    }

    WifiStatus status;
    NMAccessPoint* activeAp = nm_device_wifi_get_active_access_point(device);
    if (activeAp) {
        status.connected = true;
        status.ssid = ssidFromAccessPoint(activeAp);
        return Result<WifiStatus, std::string>::okay(status);
    }

    NMActiveConnection* activeConnection = nm_device_get_active_connection(NM_DEVICE(device));
    if (!activeConnection) {
        return Result<WifiStatus, std::string>::okay(status);
    }

    status.connected = true;
    const char* id = nm_active_connection_get_id(activeConnection);
    if (id) {
        status.ssid = id;
    }

    return Result<WifiStatus, std::string>::okay(status);
}

Result<std::vector<WifiNetworkInfo>, std::string> WifiManager::listNetworks() const
{
    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (!device) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error("No WiFi device found");
    }

    std::optional<std::string> scanError;
    const auto accessPoints = collectAccessPoints(device, scanError);

    std::unordered_map<std::string, WifiAccessPointInfo> bestBySsid;
    for (const auto& ap : accessPoints) {
        auto it = bestBySsid.find(ap.ssid);
        const int apSignal = ap.signalDbm.has_value() ? ap.signalDbm.value() : -200;
        if (it == bestBySsid.end()) {
            bestBySsid.emplace(ap.ssid, ap);
            continue;
        }

        const int existingSignal =
            it->second.signalDbm.has_value() ? it->second.signalDbm.value() : -200;
        if (apSignal > existingSignal) {
            it->second = ap;
        }
    }

    NMActiveConnection* activeConnection = nm_device_get_active_connection(NM_DEVICE(device));
    const char* activeUuid =
        activeConnection ? nm_active_connection_get_uuid(activeConnection) : nullptr;

    struct SavedEntry {
        WifiNetworkInfo info;
        int64_t timestamp = 0;
        bool active = false;
    };

    std::vector<SavedEntry> savedEntries;
    std::unordered_map<std::string, size_t> savedIndexBySsid;

    const GPtrArray* connections = nm_client_get_connections(client.get());
    if (connections) {
        for (guint i = 0; i < connections->len; ++i) {
            auto* connection = static_cast<NMConnection*>(g_ptr_array_index(connections, i));
            if (!connection) {
                continue;
            }

            auto* setting = nm_connection_get_setting_connection(connection);
            const char* type =
                setting ? nm_setting_connection_get_connection_type(setting) : nullptr;
            if (!type || std::strcmp(type, NM_SETTING_WIRELESS_SETTING_NAME) != 0) {
                continue;
            }

            const char* id = nm_connection_get_id(connection);
            const std::string connectionId = id ? id : "";
            std::string ssid = ssidFromConnection(connection);
            if (ssid.empty()) {
                ssid = connectionId;
            }

            if (ssid.empty()) {
                continue;
            }

            const int64_t timestamp = connectionTimestamp(connection);
            const bool isActive = activeUuid && nm_connection_get_uuid(connection)
                && std::strcmp(activeUuid, nm_connection_get_uuid(connection)) == 0;

            const bool autoConnect =
                setting ? nm_setting_connection_get_autoconnect(setting) != FALSE : false;
            const bool hasCredentials = connectionHasCredentials(connection);

            WifiNetworkInfo info{
                .ssid = ssid,
                .status = isActive ? WifiNetworkStatus::Connected : WifiNetworkStatus::Saved,
                .signalDbm = std::nullopt,
                .security = securityFromConnection(connection),
                .autoConnect = autoConnect,
                .hasCredentials = hasCredentials,
                .lastUsedDate = formatDate(timestamp),
                .lastUsedRelative = formatRelative(timestamp),
                .connectionId = connectionId,
            };

            auto apIt = bestBySsid.find(ssid);
            if (apIt != bestBySsid.end()) {
                info.signalDbm = apIt->second.signalDbm;
                if (info.security == "unknown" || info.security.empty()) {
                    info.security = apIt->second.security;
                }
            }

            auto existingIt = savedIndexBySsid.find(ssid);
            if (existingIt == savedIndexBySsid.end()) {
                savedIndexBySsid.emplace(ssid, savedEntries.size());
                savedEntries.push_back({ info, timestamp, isActive });
                continue;
            }

            SavedEntry& existing = savedEntries[existingIt->second];
            const bool replace = (isActive && !existing.active)
                || (isActive == existing.active && timestamp > existing.timestamp);
            if (replace) {
                existing = { info, timestamp, isActive };
            }
        }
    }

    if (scanError.has_value() && savedEntries.empty()) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error(scanError.value());
    }

    std::sort(
        savedEntries.begin(), savedEntries.end(), [](const SavedEntry& a, const SavedEntry& b) {
            if (a.active != b.active) {
                return a.active;
            }
            if (a.timestamp != b.timestamp) {
                return a.timestamp > b.timestamp;
            }
            return a.info.ssid < b.info.ssid;
        });

    std::vector<WifiNetworkInfo> openEntries;
    for (const auto& [ssid, ap] : bestBySsid) {
        if (ap.security != "open") {
            continue;
        }
        if (savedIndexBySsid.find(ssid) != savedIndexBySsid.end()) {
            continue;
        }

        WifiNetworkInfo info{
            .ssid = ssid,
            .status = WifiNetworkStatus::Open,
            .signalDbm = ap.signalDbm,
            .security = ap.security,
            .autoConnect = false,
            .hasCredentials = false,
            .lastUsedDate = std::nullopt,
            .lastUsedRelative = "n/a",
            .connectionId = std::string{},
        };

        openEntries.push_back(info);
    }

    std::sort(
        openEntries.begin(),
        openEntries.end(),
        [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
            const int signalA = a.signalDbm.has_value() ? a.signalDbm.value() : -200;
            const int signalB = b.signalDbm.has_value() ? b.signalDbm.value() : -200;
            if (signalA != signalB) {
                return signalA > signalB;
            }
            return a.ssid < b.ssid;
        });

    std::vector<WifiNetworkInfo> networks;
    networks.reserve(savedEntries.size() + openEntries.size());
    for (const auto& entry : savedEntries) {
        networks.push_back(entry.info);
    }
    for (const auto& entry : openEntries) {
        networks.push_back(entry);
    }

    return Result<std::vector<WifiNetworkInfo>, std::string>::okay(networks);
}

Result<std::vector<WifiAccessPointInfo>, std::string> WifiManager::listAccessPoints() const
{
    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (!device) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error("No WiFi device found");
    }

    std::optional<std::string> scanError;
    const auto accessPoints = collectAccessPoints(device, scanError);
    if (scanError.has_value() && accessPoints.empty()) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error(scanError.value());
    }

    return Result<std::vector<WifiAccessPointInfo>, std::string>::okay(accessPoints);
}

Result<WifiConnectResult, std::string> WifiManager::connect(const WifiNetworkInfo& network) const
{
    if (network.ssid.empty()) {
        return Result<WifiConnectResult, std::string>::error("SSID is required");
    }

    if (network.status == WifiNetworkStatus::Connected) {
        return Result<WifiConnectResult, std::string>::okay(
            WifiConnectResult{ .success = true, .ssid = network.ssid });
    }

    LOG_INFO(Network, "Connecting to WiFi network '{}'.", network.ssid);

    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<WifiConnectResult, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (!device) {
        return Result<WifiConnectResult, std::string>::error("No WiFi device found");
    }

    if (!network.connectionId.empty()) {
        auto candidates = collectWifiConnections(
            client.get(), device, network.connectionId, WifiConnectionMatch::ById);
        const auto* chosen =
            selectBestWifiConnection("connection id", network.connectionId, candidates);
        if (chosen && chosen->connection) {
            std::string activationError;
            if (!activateConnection(
                    client.get(),
                    chosen->connection,
                    NM_DEVICE(device),
                    findBestAccessPoint(device, network.ssid),
                    activationError)) {
                return Result<WifiConnectResult, std::string>::error(activationError);
            }

            std::string waitError;
            if (!waitForDeviceActivation(NM_DEVICE(device), kActivationTimeoutSeconds, waitError)) {
                return Result<WifiConnectResult, std::string>::error(waitError);
            }

            LOG_INFO(Network, "WiFi connected to '{}'.", network.ssid);
            return Result<WifiConnectResult, std::string>::okay(
                WifiConnectResult{ .success = true, .ssid = network.ssid });
        }
    }

    return connectBySsid(network.ssid, std::nullopt);
}

Result<WifiConnectResult, std::string> WifiManager::connectBySsid(
    const std::string& ssid, const std::optional<std::string>& password) const
{
    if (ssid.empty()) {
        return Result<WifiConnectResult, std::string>::error("SSID is required");
    }

    LOG_INFO(Network, "Connecting to WiFi SSID '{}'.", ssid);

    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<WifiConnectResult, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (!device) {
        return Result<WifiConnectResult, std::string>::error("No WiFi device found");
    }

    NMAccessPoint* activeAp = nm_device_wifi_get_active_access_point(device);
    if (activeAp && ssidFromAccessPoint(activeAp) == ssid) {
        return Result<WifiConnectResult, std::string>::okay(
            WifiConnectResult{ .success = true, .ssid = ssid });
    }

    auto candidates =
        collectWifiConnections(client.get(), device, ssid, WifiConnectionMatch::BySsid);

    if (password.has_value() && !candidates.empty()) {
        LOG_INFO(
            Network,
            "Found {} existing WiFi profile(s) for '{}'; will replace after successful connect.",
            candidates.size(),
            ssid);
    }

    if (!password.has_value()) {
        const auto* chosen = selectBestWifiConnection("SSID", ssid, candidates);
        if (chosen && chosen->connection) {
            std::string activationError;
            if (!activateConnection(
                    client.get(),
                    chosen->connection,
                    NM_DEVICE(device),
                    findBestAccessPoint(device, ssid),
                    activationError)) {
                return Result<WifiConnectResult, std::string>::error(activationError);
            }

            std::string waitError;
            if (!waitForDeviceActivation(NM_DEVICE(device), kActivationTimeoutSeconds, waitError)) {
                return Result<WifiConnectResult, std::string>::error(waitError);
            }

            LOG_INFO(Network, "WiFi connected to '{}'.", ssid);
            return Result<WifiConnectResult, std::string>::okay(
                WifiConnectResult{ .success = true, .ssid = ssid });
        }
    }

    NMAccessPoint* ap = findBestAccessPoint(device, ssid);
    if (!password.has_value() && ap && securityFromAccessPoint(ap) != "open") {
        return Result<WifiConnectResult, std::string>::error(
            "Password required for secured network");
    }

    auto connection = buildConnectionForSsid(ssid, password, ap);
    if (!connection) {
        return Result<WifiConnectResult, std::string>::error("Failed to build WiFi connection");
    }

    std::string activationError;
    if (!addAndActivateConnection(
            client.get(), connection.get(), NM_DEVICE(device), ap, activationError)) {
        return Result<WifiConnectResult, std::string>::error(activationError);
    }

    std::string waitError;
    if (!waitForDeviceActivation(NM_DEVICE(device), kActivationTimeoutSeconds, waitError)) {
        return Result<WifiConnectResult, std::string>::error(waitError);
    }

    if (password.has_value()) {
        const char* uuid = nm_connection_get_uuid(connection.get());
        if (!uuid || !*uuid) {
            LOG_WARN(Network, "New WiFi profile UUID unavailable; skipping cleanup.");
        }
        else {
            const std::string keepUuid = uuid;
            int removed = 0;
            std::string deleteError;
            if (!deleteWifiConnectionsBySsid(client.get(), ssid, keepUuid, removed, deleteError)) {
                LOG_WARN(Network, "Failed to clean up old WiFi profiles: {}", deleteError);
            }
            else if (removed > 0) {
                LOG_INFO(Network, "Removed {} stale WiFi profile(s) for '{}'.", removed, ssid);
            }
        }
    }

    LOG_INFO(Network, "WiFi connected to '{}'.", ssid);
    return Result<WifiConnectResult, std::string>::okay(
        WifiConnectResult{ .success = true, .ssid = ssid });
}

Result<WifiDisconnectResult, std::string> WifiManager::disconnect(
    const std::optional<std::string>& ssid) const
{
    LOG_INFO(
        Network,
        "Disconnecting WiFi {}.",
        ssid.has_value() && !ssid->empty() ? *ssid : "active connection");

    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<WifiDisconnectResult, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (!device) {
        return Result<WifiDisconnectResult, std::string>::error("No WiFi device found");
    }

    NMActiveConnection* activeConnection = nm_device_get_active_connection(NM_DEVICE(device));
    if (!activeConnection) {
        return Result<WifiDisconnectResult, std::string>::error("No active WiFi connection");
    }

    NMAccessPoint* activeAp = nm_device_wifi_get_active_access_point(device);
    std::string activeSsid = activeAp ? ssidFromAccessPoint(activeAp) : "";
    if (activeSsid.empty()) {
        const char* activeId = nm_active_connection_get_id(activeConnection);
        activeSsid = activeId ? activeId : "";
    }
    if (ssid.has_value() && !ssid->empty() && activeSsid != *ssid) {
        return Result<WifiDisconnectResult, std::string>::error(
            "Active WiFi does not match requested SSID");
    }

    std::string disconnectError;
    if (!deactivateActiveConnection(client.get(), activeConnection, disconnectError)) {
        return Result<WifiDisconnectResult, std::string>::error(disconnectError);
    }

    std::string waitError;
    if (!waitForDeviceDeactivation(NM_DEVICE(device), kActivationTimeoutSeconds, waitError)) {
        return Result<WifiDisconnectResult, std::string>::error(waitError);
    }

    LOG_INFO(Network, "WiFi disconnected from '{}'.", activeSsid);
    return Result<WifiDisconnectResult, std::string>::okay(
        WifiDisconnectResult{ .success = true, .ssid = activeSsid });
}

Result<WifiForgetResult, std::string> WifiManager::forget(const std::string& ssid) const
{
    if (ssid.empty()) {
        return Result<WifiForgetResult, std::string>::error("SSID is required");
    }

    LOG_INFO(Network, "Forgetting WiFi profiles for '{}'.", ssid);

    std::string errorMessage;
    auto client = createClient(errorMessage);
    if (!client) {
        return Result<WifiForgetResult, std::string>::error(errorMessage);
    }

    NMDeviceWifi* device = findWifiDevice(client.get());
    if (device) {
        NMActiveConnection* activeConnection = nm_device_get_active_connection(NM_DEVICE(device));
        if (activeConnection) {
            NMAccessPoint* activeAp = nm_device_wifi_get_active_access_point(device);
            std::string activeSsid = activeAp ? ssidFromAccessPoint(activeAp) : "";
            if (activeSsid.empty()) {
                const char* activeId = nm_active_connection_get_id(activeConnection);
                activeSsid = activeId ? activeId : "";
            }

            if (!activeSsid.empty() && activeSsid == ssid) {
                std::string disconnectError;
                if (!deactivateActiveConnection(client.get(), activeConnection, disconnectError)) {
                    return Result<WifiForgetResult, std::string>::error(disconnectError);
                }
            }
        }
    }

    const GPtrArray* connections = nm_client_get_connections(client.get());
    if (!connections) {
        return Result<WifiForgetResult, std::string>::error("No saved WiFi connections");
    }

    std::vector<GObjectPtr<NMRemoteConnection>> toDelete;
    for (guint i = 0; i < connections->len; ++i) {
        auto* connection = static_cast<NMConnection*>(g_ptr_array_index(connections, i));
        if (!connection) {
            continue;
        }

        auto* setting = nm_connection_get_setting_connection(connection);
        const char* type = setting ? nm_setting_connection_get_connection_type(setting) : nullptr;
        if (!type || std::strcmp(type, NM_SETTING_WIRELESS_SETTING_NAME) != 0) {
            continue;
        }

        const std::string connectionSsid = ssidFromConnection(connection);
        const char* id = nm_connection_get_id(connection);
        const std::string connectionId = id ? id : "";

        if (connectionSsid != ssid && connectionId != ssid) {
            continue;
        }

        if (!NM_IS_REMOTE_CONNECTION(connection)) {
            return Result<WifiForgetResult, std::string>::error(
                "WiFi connection cannot be removed");
        }

        auto* remote = NM_REMOTE_CONNECTION(g_object_ref(connection));
        toDelete.emplace_back(remote);
    }

    if (toDelete.empty()) {
        return Result<WifiForgetResult, std::string>::error(
            "No saved WiFi connection found for SSID");
    }

    int removed = 0;
    for (const auto& connection : toDelete) {
        std::string deleteError;
        if (!deleteRemoteConnection(connection.get(), deleteError)) {
            return Result<WifiForgetResult, std::string>::error(deleteError);
        }
        ++removed;
    }

    return Result<WifiForgetResult, std::string>::okay(
        WifiForgetResult{ .success = true, .ssid = ssid, .removed = removed });
}

} // namespace Network
} // namespace DirtSim
