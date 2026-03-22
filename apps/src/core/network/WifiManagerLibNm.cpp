#include "WifiManagerLibNm.h"

#include "core/Assert.h"
#include "core/LoggingChannels.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <glib-object.h>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace Network {
namespace Internal {
namespace {

struct GErrorDeleter {
    void operator()(GError* error) const
    {
        if (error) {
            g_error_free(error);
        }
    }
};

struct GSourceDeleter {
    void operator()(GSource* source) const
    {
        if (!source) {
            return;
        }

        g_source_destroy(source);
        g_source_unref(source);
    }
};

struct AsyncLoop {
    GMainContext* context = nullptr;
    GMainLoop* loop = nullptr;
    GSource* timeoutSource = nullptr;
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

    SignalHandlerGuard(SignalHandlerGuard&& other) noexcept
        : instance(other.instance), handlerId(other.handlerId)
    {
        other.instance = nullptr;
        other.handlerId = 0;
    }

    SignalHandlerGuard& operator=(SignalHandlerGuard&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        if (instance && handlerId != 0) {
            g_signal_handler_disconnect(instance, handlerId);
        }

        instance = other.instance;
        handlerId = other.handlerId;
        other.instance = nullptr;
        other.handlerId = 0;
        return *this;
    }

    ~SignalHandlerGuard()
    {
        if (instance && handlerId != 0) {
            g_signal_handler_disconnect(instance, handlerId);
        }
    }
};

class MainContextThreadDefaultGuard {
public:
    explicit MainContextThreadDefaultGuard(GMainContext* context) : context_(context)
    {
        if (context_) {
            g_main_context_push_thread_default(context_);
        }
    }

    MainContextThreadDefaultGuard(const MainContextThreadDefaultGuard&) = delete;
    MainContextThreadDefaultGuard& operator=(const MainContextThreadDefaultGuard&) = delete;

    ~MainContextThreadDefaultGuard()
    {
        if (context_) {
            g_main_context_pop_thread_default(context_);
        }
    }

private:
    GMainContext* context_ = nullptr;
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
    AsyncLoopGuard(GMainContext* context, int timeoutSeconds)
    {
        loop_ = std::make_unique<AsyncLoop>();
        loop_->context = g_main_context_ref(context ? context : g_main_context_default());
        loop_->loop = g_main_loop_new(loop_->context, FALSE);

        if (timeoutSeconds > 0) {
            loop_->timeoutSource = g_timeout_source_new_seconds(static_cast<guint>(timeoutSeconds));
            g_source_set_callback(loop_->timeoutSource, onLoopTimeout, loop_.get(), nullptr);
            g_source_attach(loop_->timeoutSource, loop_->context);
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
        if (loop_->timeoutSource) {
            g_source_destroy(loop_->timeoutSource);
            g_source_unref(loop_->timeoutSource);
            loop_->timeoutSource = nullptr;
        }
        if (loop_->loop) {
            g_main_loop_unref(loop_->loop);
            loop_->loop = nullptr;
        }
        if (loop_->context) {
            g_main_context_unref(loop_->context);
            loop_->context = nullptr;
        }
    }

    AsyncLoop* get() const { return loop_.get(); }
    GMainLoop* mainLoop() const { return loop_ ? loop_->loop : nullptr; }
    bool timedOut() const { return loop_ ? loop_->timedOut : false; }

private:
    std::unique_ptr<AsyncLoop> loop_;
};

template <typename T>
using GErrorPtr = std::unique_ptr<T, GErrorDeleter>;

using GSourcePtr = std::unique_ptr<GSource, GSourceDeleter>;

constexpr int kActivationAssociationStageTimeoutSeconds = 25;
constexpr int kActivationIpStageTimeoutSeconds = 20;
constexpr int kActivationOverallTimeoutSeconds = 45;
constexpr int kActivationPollIntervalMs = 250;
constexpr int kClientCreationTimeoutSeconds = 10;
constexpr size_t kDiagnosticAccessPointLimit = 8;
constexpr size_t kDiagnosticConnectionLimit = 8;
constexpr int kDeactivationTimeoutSeconds = 20;
constexpr int kHigherBandSignalToleranceDb = 12;
constexpr int kMultiAccessPointPasswordConnectAttempts = 2;
constexpr int kOperationTimeoutSeconds = 10;
constexpr int kScanRequestTimeoutSeconds = 5;
constexpr int kScanCompletionTimeoutSeconds = 15;

std::atomic<uint64_t> gNextWifiConnectRequestId{ 1 };

GMainContext* resolveMainContext(NMClient* client, GMainContext* mainContext)
{
    if (mainContext) {
        return mainContext;
    }
    if (client) {
        if (GMainContext* clientContext = nm_client_get_main_context(client)) {
            return clientContext;
        }
    }
    return g_main_context_default();
}

std::string formatError(GError* error, const std::string& fallback)
{
    if (error && error->message) {
        return std::string(error->message);
    }
    return fallback;
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

std::string activeSsidFromDevice(NMDevice* device)
{
    if (!device || !NM_IS_DEVICE_WIFI(device)) {
        return "";
    }

    return ssidFromAccessPoint(nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device)));
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

bool isHigherBandFrequency(guint32 frequencyMHz)
{
    return frequencyMHz >= 5000;
}

bool shouldPreferAccessPoint(NMAccessPoint* candidate, NMAccessPoint* current)
{
    if (!candidate) {
        return false;
    }
    if (!current) {
        return true;
    }

    const int candidateSignal =
        strengthToDbm(nm_access_point_get_strength(candidate)).value_or(-200);
    const int currentSignal = strengthToDbm(nm_access_point_get_strength(current)).value_or(-200);

    const bool candidateHigherBand =
        isHigherBandFrequency(nm_access_point_get_frequency(candidate));
    const bool currentHigherBand = isHigherBandFrequency(nm_access_point_get_frequency(current));
    if (candidateHigherBand != currentHigherBand) {
        if (candidateHigherBand
            && candidateSignal >= currentSignal - kHigherBandSignalToleranceDb) {
            return true;
        }
        if (currentHigherBand && currentSignal >= candidateSignal - kHigherBandSignalToleranceDb) {
            return false;
        }
    }

    if (candidateSignal != currentSignal) {
        return candidateSignal > currentSignal;
    }

    return nm_access_point_get_frequency(candidate) > nm_access_point_get_frequency(current);
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

bool isCancelableWifiDeviceState(const NMDeviceState state)
{
    switch (state) {
        case NM_DEVICE_STATE_PREPARE:
        case NM_DEVICE_STATE_CONFIG:
        case NM_DEVICE_STATE_NEED_AUTH:
        case NM_DEVICE_STATE_IP_CONFIG:
        case NM_DEVICE_STATE_IP_CHECK:
        case NM_DEVICE_STATE_SECONDARIES:
            return true;
        case NM_DEVICE_STATE_UNKNOWN:
        case NM_DEVICE_STATE_UNMANAGED:
        case NM_DEVICE_STATE_UNAVAILABLE:
        case NM_DEVICE_STATE_DISCONNECTED:
        case NM_DEVICE_STATE_DEACTIVATING:
        case NM_DEVICE_STATE_FAILED:
        case NM_DEVICE_STATE_ACTIVATED:
            return false;
    }

    return false;
}

std::string deviceStateReasonToString(NMDeviceStateReason reason)
{
    GEnumClass* enumClass = static_cast<GEnumClass*>(g_type_class_ref(NM_TYPE_DEVICE_STATE_REASON));
    const GEnumValue* value =
        enumClass ? g_enum_get_value(enumClass, static_cast<int>(reason)) : nullptr;
    const char* name = (value && value->value_nick) ? value->value_nick : "unknown";
    const std::string result(name);
    if (enumClass) {
        g_type_class_unref(enumClass);
    }
    return result;
}

std::string formatAccessPointSummary(NMAccessPoint* accessPoint)
{
    if (!accessPoint) {
        return "none";
    }

    std::ostringstream summary;
    summary << "bssid="
            << (nm_access_point_get_bssid(accessPoint) ? nm_access_point_get_bssid(accessPoint)
                                                       : "unknown");

    const std::string ssid = ssidFromAccessPoint(accessPoint);
    if (!ssid.empty()) {
        summary << ", ssid='" << ssid << "'";
    }

    const guint32 frequency = nm_access_point_get_frequency(accessPoint);
    if (frequency > 0) {
        summary << ", frequency_mhz=" << frequency
                << ", channel=" << nm_utils_wifi_freq_to_channel(frequency);
    }

    const auto signal = strengthToDbm(nm_access_point_get_strength(accessPoint));
    if (signal.has_value()) {
        summary << ", signal_dbm=" << signal.value();
    }

    summary << ", security=" << securityFromAccessPoint(accessPoint);
    return summary.str();
}

std::string makeWifiConnectLogContext(
    uint64_t requestId,
    const std::string& ssid,
    const std::string& mode,
    int attemptNumber = 0,
    int attemptCount = 0)
{
    std::string context =
        "WiFi connect request #" + std::to_string(requestId) + " for '" + ssid + "' via " + mode;
    if (attemptCount > 0) {
        context += " attempt " + std::to_string(attemptNumber) + "/" + std::to_string(attemptCount);
    }
    return context;
}

int64_t elapsedMilliseconds(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end = std::chrono::steady_clock::now())
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

enum class ActivationStage {
    Association,
    IpConfiguration,
};

std::optional<ActivationStage> activationStageFromDeviceState(NMDeviceState state)
{
    switch (state) {
        case NM_DEVICE_STATE_UNKNOWN:
        case NM_DEVICE_STATE_UNMANAGED:
        case NM_DEVICE_STATE_UNAVAILABLE:
        case NM_DEVICE_STATE_DISCONNECTED:
        case NM_DEVICE_STATE_ACTIVATED:
        case NM_DEVICE_STATE_DEACTIVATING:
        case NM_DEVICE_STATE_FAILED:
            return std::nullopt;
        case NM_DEVICE_STATE_PREPARE:
        case NM_DEVICE_STATE_CONFIG:
        case NM_DEVICE_STATE_NEED_AUTH:
            return ActivationStage::Association;
        case NM_DEVICE_STATE_IP_CONFIG:
        case NM_DEVICE_STATE_IP_CHECK:
        case NM_DEVICE_STATE_SECONDARIES:
            return ActivationStage::IpConfiguration;
    }

    return std::nullopt;
}

const char* activationStageToString(ActivationStage stage)
{
    switch (stage) {
        case ActivationStage::Association:
            return "association";
        case ActivationStage::IpConfiguration:
            return "ip-configuration";
    }

    DIRTSIM_ASSERT(false, "Unhandled ActivationStage");
    return "unknown";
}

const char* activationStageLabel(const std::optional<ActivationStage>& stage)
{
    if (!stage.has_value()) {
        return "none";
    }

    return activationStageToString(stage.value());
}

int activationStageTimeoutSeconds(ActivationStage stage)
{
    switch (stage) {
        case ActivationStage::Association:
            return kActivationAssociationStageTimeoutSeconds;
        case ActivationStage::IpConfiguration:
            return kActivationIpStageTimeoutSeconds;
    }

    DIRTSIM_ASSERT(false, "Unhandled ActivationStage");
    return kActivationOverallTimeoutSeconds;
}

std::string formatActivationTimeoutMessage(
    const std::optional<ActivationStage>& stage,
    NMDeviceState state,
    NMDeviceStateReason reason,
    bool overallTimeout,
    int overallTimeoutSeconds)
{
    std::string result = "WiFi activation timed out";
    if (overallTimeout) {
        result += " after " + std::to_string(overallTimeoutSeconds) + "s";
    }
    else if (stage.has_value()) {
        result += " in ";
        result += activationStageToString(stage.value());
        result +=
            " stage after " + std::to_string(activationStageTimeoutSeconds(stage.value())) + "s";
    }

    result += " (state=";
    result += deviceStateToString(state);
    result += ", reason=";
    result += deviceStateReasonToString(reason);
    result += ")";
    return result;
}

bool shouldRetryPasswordConnectAttempt(
    const std::optional<std::string>& password,
    size_t visibleAccessPointCount,
    const std::string& errorMessage,
    int attemptNumber,
    int maxAttempts)
{
    if (!password.has_value() || visibleAccessPointCount < 2 || attemptNumber >= maxAttempts) {
        return false;
    }

    return errorMessage.find("reason=no-secrets") != std::string::npos
        || errorMessage.find("timed out") != std::string::npos
        || errorMessage.find("reason=disconnected") != std::string::npos;
}

bool waitForDeviceActivation(
    NMClient* client,
    NMDevice* device,
    const std::string& expectedSsid,
    GMainContext* mainContext,
    int timeoutSeconds,
    const std::string& logContext,
    std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    const auto initialState = nm_device_get_state(device);
    if (initialState == NM_DEVICE_STATE_ACTIVATED
        && (expectedSsid.empty() || activeSsidFromDevice(device) == expectedSsid)) {
        return true;
    }
    if (initialState == NM_DEVICE_STATE_FAILED) {
        errorMessage = std::string("WiFi activation failed (state=")
            + deviceStateToString(initialState)
            + ", reason=" + deviceStateReasonToString(nm_device_get_state_reason(device)) + ")";
        return false;
    }

    const int overallTimeoutSeconds =
        timeoutSeconds > 0 ? timeoutSeconds : kActivationOverallTimeoutSeconds;

    struct ActivationContext {
        AsyncLoop* loop = nullptr;
        std::string* error = nullptr;
        const std::string* expectedSsid = nullptr;
        bool activated = false;
        bool overallTimedOut = false;
        bool stageTimedOut = false;
        int overallTimeoutSeconds = 0;
        const std::string* logContext = nullptr;
        NMDevice* device = nullptr;
        NMDeviceState lastState = NM_DEVICE_STATE_UNKNOWN;
        NMDeviceStateReason lastReason = NM_DEVICE_STATE_REASON_NONE;
        std::optional<ActivationStage> currentStage;
        std::chrono::steady_clock::time_point overallStart;
        std::chrono::steady_clock::time_point lastStateChangeTime;
        std::chrono::steady_clock::time_point stageStartTime;
        std::chrono::steady_clock::time_point lastStageProgressTime;
    };

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), 0);
    ActivationContext context{
        .loop = loop.get(),
        .error = &errorMessage,
        .expectedSsid = &expectedSsid,
        .activated = false,
        .overallTimedOut = false,
        .stageTimedOut = false,
        .overallTimeoutSeconds = overallTimeoutSeconds,
        .logContext = &logContext,
        .device = device,
        .lastState = initialState,
        .lastReason = nm_device_get_state_reason(device),
        .currentStage = activationStageFromDeviceState(initialState),
        .overallStart = std::chrono::steady_clock::now(),
        .lastStateChangeTime = std::chrono::steady_clock::now(),
        .stageStartTime = std::chrono::steady_clock::now(),
        .lastStageProgressTime = std::chrono::steady_clock::now(),
    };

    LOG_INFO(
        Network,
        "{}: waiting for activation (expected_ssid='{}', initial_state={}, initial_reason={}).",
        logContext,
        expectedSsid,
        deviceStateToString(initialState),
        deviceStateReasonToString(context.lastReason));

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
                const auto now = std::chrono::steady_clock::now();
                const int64_t totalElapsedMs = elapsedMilliseconds(ctx->overallStart, now);
                const int64_t sinceLastStateChangeMs =
                    elapsedMilliseconds(ctx->lastStateChangeTime, now);
                const int64_t stageElapsedMs = elapsedMilliseconds(ctx->stageStartTime, now);
                const auto previousStage = ctx->currentStage;

                LOG_INFO(
                    Network,
                    "{}: WiFi device {} state change after {} ms total ({} ms since last change, "
                    "{} stage for {} ms): {} -> {} (reason: {}).",
                    *ctx->logContext,
                    iface && *iface ? iface : "unknown",
                    totalElapsedMs,
                    sinceLastStateChangeMs,
                    activationStageLabel(previousStage),
                    stageElapsedMs,
                    deviceStateToString(oldStateValue),
                    deviceStateToString(newStateValue),
                    deviceStateReasonToString(reasonValue));

                ctx->lastState = newStateValue;
                ctx->lastReason = reasonValue;
                const auto nextStage = activationStageFromDeviceState(newStateValue);
                ctx->lastStateChangeTime = now;
                if (nextStage != previousStage) {
                    ctx->stageStartTime = now;
                }
                if (nextStage.has_value() || ctx->currentStage.has_value()) {
                    ctx->lastStageProgressTime = now;
                }
                ctx->currentStage = nextStage;

                const bool expectedSsidMatches = !ctx->expectedSsid || ctx->expectedSsid->empty()
                    || activeSsidFromDevice(device) == *ctx->expectedSsid;

                if (newStateValue == NM_DEVICE_STATE_ACTIVATED && expectedSsidMatches) {
                    ctx->activated = true;
                    g_main_loop_quit(ctx->loop->loop);
                    return;
                }

                if (newStateValue == NM_DEVICE_STATE_DISCONNECTED
                    && reasonValue == NM_DEVICE_STATE_REASON_NEW_ACTIVATION) {
                    return;
                }

                if (newStateValue == NM_DEVICE_STATE_DISCONNECTED) {
                    if (ctx->error && ctx->error->empty()) {
                        *ctx->error = std::string("WiFi activation failed (state=")
                            + deviceStateToString(newStateValue)
                            + ", reason=" + deviceStateReasonToString(reasonValue) + ")";
                    }
                    g_main_loop_quit(ctx->loop->loop);
                    return;
                }

                if (newStateValue == NM_DEVICE_STATE_FAILED
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

    GSourcePtr progressTimeoutSource(g_timeout_source_new(kActivationPollIntervalMs));
    g_source_set_callback(
        progressTimeoutSource.get(),
        +[](gpointer userData) -> gboolean {
            auto* ctx = static_cast<ActivationContext*>(userData);
            const auto now = std::chrono::steady_clock::now();
            const int64_t totalElapsedMs = elapsedMilliseconds(ctx->overallStart, now);
            const int64_t stageElapsedMs = elapsedMilliseconds(ctx->stageStartTime, now);

            if (now - ctx->overallStart >= std::chrono::seconds(ctx->overallTimeoutSeconds)) {
                ctx->overallTimedOut = true;
                LOG_WARN(
                    Network,
                    "{}: WiFi activation hit overall timeout after {} ms (stage={}, state={}, "
                    "reason={}).",
                    *ctx->logContext,
                    totalElapsedMs,
                    activationStageLabel(ctx->currentStage),
                    deviceStateToString(ctx->lastState),
                    deviceStateReasonToString(ctx->lastReason));
                g_main_loop_quit(ctx->loop->loop);
                return G_SOURCE_REMOVE;
            }

            if (ctx->currentStage.has_value()
                && now - ctx->lastStageProgressTime >= std::chrono::seconds(
                       activationStageTimeoutSeconds(ctx->currentStage.value()))) {
                ctx->stageTimedOut = true;
                LOG_WARN(
                    Network,
                    "{}: WiFi activation stalled in {} stage after {} ms total and {} ms in "
                    "stage (state={}, reason={}).",
                    *ctx->logContext,
                    activationStageLabel(ctx->currentStage),
                    totalElapsedMs,
                    stageElapsedMs,
                    deviceStateToString(ctx->lastState),
                    deviceStateReasonToString(ctx->lastReason));
                g_main_loop_quit(ctx->loop->loop);
                return G_SOURCE_REMOVE;
            }

            return G_SOURCE_CONTINUE;
        },
        &context,
        nullptr);
    g_source_attach(progressTimeoutSource.get(), loop.get()->context);

    g_main_loop_run(loop.mainLoop());

    const auto finalState = nm_device_get_state(device);
    const auto finalReason = nm_device_get_state_reason(device);
    const bool expectedSsidMatches =
        expectedSsid.empty() || activeSsidFromDevice(device) == expectedSsid;
    if (finalState == NM_DEVICE_STATE_ACTIVATED && expectedSsidMatches) {
        LOG_INFO(
            Network,
            "{}: WiFi activation completed after {} ms (final_state={}, final_reason={}).",
            logContext,
            elapsedMilliseconds(context.overallStart),
            deviceStateToString(finalState),
            deviceStateReasonToString(finalReason));
        return true;
    }

    context.lastState = finalState;
    context.lastReason = finalReason;
    context.currentStage = activationStageFromDeviceState(finalState);

    if (!context.activated && errorMessage.empty()) {
        if (context.stageTimedOut || context.overallTimedOut) {
            errorMessage = formatActivationTimeoutMessage(
                context.currentStage,
                context.lastState,
                context.lastReason,
                context.overallTimedOut,
                overallTimeoutSeconds);
        }
        else {
            errorMessage = std::string("WiFi activation failed (state=")
                + deviceStateToString(context.lastState)
                + ", reason=" + deviceStateReasonToString(context.lastReason) + ")";
        }
    }

    LOG_WARN(
        Network,
        "{}: WiFi activation ended after {} ms without success: {}.",
        logContext,
        elapsedMilliseconds(context.overallStart),
        errorMessage.empty() ? "unknown error" : errorMessage);

    return context.activated;
}

bool waitForDeviceDeactivation(
    NMClient* client,
    NMDevice* device,
    GMainContext* mainContext,
    int timeoutSeconds,
    std::string& errorMessage)
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

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), timeoutSeconds);
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

bool requestWifiScanAsync(
    NMClient* client, NMDeviceWifi* device, GMainContext* mainContext, std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    struct ScanContext {
        AsyncLoop* loop = nullptr;
        std::string* error = nullptr;
        bool completed = false;
    };

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kScanRequestTimeoutSeconds);
    ScanContext context{ loop.get(), &errorMessage, false };

    {
        MainContextThreadDefaultGuard contextGuard(resolveMainContext(client, mainContext));
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
                else {
                    ctx->completed = true;
                }
                g_main_loop_quit(ctx->loop->loop);
            },
            &context);
    }

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi scan request timed out";
    }

    return context.completed && errorMessage.empty();
}

bool waitForFreshWifiScan(
    NMClient* client,
    NMDeviceWifi* device,
    GMainContext* mainContext,
    gint64 previousLastScan,
    std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    if (nm_device_wifi_get_last_scan(device) > previousLastScan) {
        return true;
    }

    struct ScanCompletionContext {
        AsyncLoop* loop = nullptr;
        gint64 previousLastScan = 0;
        bool completed = false;
    };

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kScanCompletionTimeoutSeconds);
    ScanCompletionContext context{ loop.get(), previousLastScan, false };

    const gulong handlerId = g_signal_connect(
        device,
        "notify::last-scan",
        G_CALLBACK(+[](GObject* object, GParamSpec*, gpointer userData) {
            auto* ctx = static_cast<ScanCompletionContext*>(userData);
            auto* device = NM_DEVICE_WIFI(object);
            if (nm_device_wifi_get_last_scan(device) > ctx->previousLastScan) {
                ctx->completed = true;
                g_main_loop_quit(ctx->loop->loop);
            }
        }),
        &context);

    SignalHandlerGuard signalGuard(device, handlerId);
    if (nm_device_wifi_get_last_scan(device) > previousLastScan) {
        return true;
    }

    g_main_loop_run(loop.mainLoop());
    if (!context.completed && errorMessage.empty()) {
        errorMessage = loop.timedOut() ? "WiFi scan timed out" : "WiFi scan failed";
    }

    return context.completed;
}

bool forceWifiScan(
    NMClient* client, NMDeviceWifi* device, GMainContext* mainContext, std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    const gint64 previousLastScan = nm_device_wifi_get_last_scan(device);
    if (!requestWifiScanAsync(client, device, mainContext, errorMessage)) {
        return false;
    }

    return waitForFreshWifiScan(client, device, mainContext, previousLastScan, errorMessage);
}

std::vector<WifiAccessPointInfo> collectAccessPoints(NMDeviceWifi* device)
{
    std::vector<WifiAccessPointInfo> results;
    if (!device) {
        return results;
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

        WifiAccessPointInfo info{
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
        [](const WifiAccessPointInfo& a, const WifiAccessPointInfo& b) {
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

std::string formatVisibleAccessPointsForSsid(NMDeviceWifi* device, const std::string& ssid)
{
    if (!device || ssid.empty()) {
        return "none";
    }

    const auto accessPoints = collectAccessPoints(device);
    std::ostringstream summary;
    size_t count = 0;
    for (const auto& accessPoint : accessPoints) {
        if (accessPoint.ssid != ssid) {
            continue;
        }

        if (count > 0) {
            summary << "; ";
        }
        if (count >= kDiagnosticAccessPointLimit) {
            summary << "...";
            break;
        }

        summary << accessPoint.bssid;
        if (accessPoint.frequencyMhz.has_value()) {
            summary << "@" << accessPoint.frequencyMhz.value() << "MHz";
        }
        if (accessPoint.channel.has_value()) {
            summary << "/ch" << accessPoint.channel.value();
        }
        if (accessPoint.signalDbm.has_value()) {
            summary << "/" << accessPoint.signalDbm.value() << "dBm";
        }
        summary << "/" << accessPoint.security;
        if (accessPoint.active) {
            summary << "/active";
        }
        ++count;
    }

    if (count == 0) {
        return "none";
    }

    return summary.str();
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

std::string formatConnectionCandidateSummary(const WifiConnectionCandidate& candidate)
{
    std::ostringstream summary;
    summary << "id='" << (candidate.id.empty() ? candidate.ssid : candidate.id) << "'"
            << ", uuid=" << (candidate.uuid.empty() ? "unknown" : candidate.uuid)
            << ", last_used=" << formatRelative(candidate.timestamp)
            << ", auto_connect=" << (candidate.autoConnect ? "true" : "false")
            << ", has_credentials=" << (candidate.hasCredentials ? "true" : "false");
    if (candidate.active) {
        summary << ", active=true";
    }
    return summary.str();
}

std::string formatConnectionCandidates(const std::vector<WifiConnectionCandidate>& candidates)
{
    if (candidates.empty()) {
        return "none";
    }

    std::ostringstream summary;
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index > 0) {
            summary << "; ";
        }
        if (index >= kDiagnosticConnectionLimit) {
            summary << "...";
            break;
        }
        summary << "[" << index + 1 << "] " << formatConnectionCandidateSummary(candidates[index]);
    }

    return summary.str();
}

enum class WifiConnectionMatch { ById, BySsid };

bool deleteRemoteConnection(
    NMClient* client,
    NMRemoteConnection* connection,
    GMainContext* mainContext,
    std::string& errorMessage);
bool deleteRemoteConnectionByUuid(
    NMClient* client,
    const std::string& uuid,
    GMainContext* mainContext,
    std::string& errorMessage);
bool connectionHasCredentials(NMConnection* connection);

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

bool deleteRemoteConnectionByUuid(
    NMClient* client, const std::string& uuid, GMainContext* mainContext, std::string& errorMessage)
{
    if (!client) {
        errorMessage = "No WiFi client available";
        return false;
    }
    if (uuid.empty()) {
        errorMessage = "WiFi profile UUID is required";
        return false;
    }

    auto* connection = nm_client_get_connection_by_uuid(client, uuid.c_str());
    if (!connection) {
        return true;
    }

    std::string deleteError;
    if (!deleteRemoteConnection(client, connection, mainContext, deleteError)) {
        errorMessage = deleteError;
        return false;
    }

    return true;
}

bool deleteWifiConnectionsBySsid(
    NMClient* client,
    const std::string& ssid,
    const std::string& keepUuid,
    GMainContext* mainContext,
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
        if (!deleteRemoteConnection(client, connection.get(), mainContext, deleteError)) {
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

    for (guint i = 0; i < accessPoints->len; ++i) {
        auto* ap = static_cast<NMAccessPoint*>(g_ptr_array_index(accessPoints, i));
        if (!ap) {
            continue;
        }

        if (ssidFromAccessPoint(ap) != ssid) {
            continue;
        }

        if (shouldPreferAccessPoint(ap, best)) {
            best = ap;
        }
    }

    return best;
}

size_t countAccessPointsForSsid(NMDeviceWifi* device, const std::string& ssid)
{
    if (!device || ssid.empty()) {
        return 0;
    }

    const GPtrArray* accessPoints = nm_device_wifi_get_access_points(device);
    if (!accessPoints) {
        return 0;
    }

    size_t count = 0;
    for (guint i = 0; i < accessPoints->len; ++i) {
        auto* ap = static_cast<NMAccessPoint*>(g_ptr_array_index(accessPoints, i));
        if (!ap || ssidFromAccessPoint(ap) != ssid) {
            continue;
        }
        ++count;
    }

    return count;
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
    GMainContext* mainContext,
    const std::string& logContext,
    std::string& errorMessage)
{
    if (!client || !connection || !device) {
        errorMessage = "WiFi connection activation failed";
        return false;
    }

    const char* connectionId = nm_connection_get_id(connection);
    LOG_INFO(
        Network,
        "{}: activating WiFi connection id={} uuid={} target_ap={}.",
        logContext,
        connectionId && *connectionId ? connectionId : "unknown",
        nm_connection_get_uuid(connection) ? nm_connection_get_uuid(connection) : "unknown",
        formatAccessPointSummary(accessPoint));

    struct ActivateContext {
        AsyncLoop* loop = nullptr;
        NMActiveConnection* active = nullptr;
        std::string* error = nullptr;
    };

    const char* specificObject = accessPoint ? nm_object_get_path(NM_OBJECT(accessPoint)) : nullptr;

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kOperationTimeoutSeconds);
    ActivateContext context{ loop.get(), nullptr, &errorMessage };

    {
        MainContextThreadDefaultGuard contextGuard(resolveMainContext(client, mainContext));
        nm_client_activate_connection_async(
            client,
            connection,
            device,
            specificObject,
            nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer userData) {
                auto* ctx = static_cast<ActivateContext*>(userData);
                GError* error = nullptr;
                ctx->active =
                    nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
                if (!ctx->active) {
                    *ctx->error = formatError(error, "WiFi connection activation failed");
                    g_clear_error(&error);
                }
                g_main_loop_quit(ctx->loop->loop);
            },
            &context);
    }

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi connection activation timed out";
    }

    if (!context.active) {
        LOG_WARN(Network, "{}: activation request failed: {}.", logContext, errorMessage);
        return false;
    }

    LOG_INFO(Network, "{}: activation request accepted by NetworkManager.", logContext);
    g_object_unref(context.active);
    return true;
}

bool addAndActivateConnection(
    NMClient* client,
    NMConnection* connection,
    NMDevice* device,
    NMAccessPoint* accessPoint,
    GMainContext* mainContext,
    const std::string& logContext,
    std::string& errorMessage)
{
    if (!client || !connection || !device) {
        errorMessage = "WiFi connection failed";
        return false;
    }

    const char* connectionId = nm_connection_get_id(connection);
    LOG_INFO(
        Network,
        "{}: adding WiFi connection id={} uuid={} target_ap={}.",
        logContext,
        connectionId && *connectionId ? connectionId : "unknown",
        nm_connection_get_uuid(connection) ? nm_connection_get_uuid(connection) : "unknown",
        formatAccessPointSummary(accessPoint));

    struct AddContext {
        AsyncLoop* loop = nullptr;
        NMActiveConnection* active = nullptr;
        std::string* error = nullptr;
    };

    const char* specificObject = accessPoint ? nm_object_get_path(NM_OBJECT(accessPoint)) : nullptr;

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kOperationTimeoutSeconds);
    AddContext context{ loop.get(), nullptr, &errorMessage };

    {
        MainContextThreadDefaultGuard contextGuard(resolveMainContext(client, mainContext));
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
    }

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi connection timed out";
    }

    if (!context.active) {
        LOG_WARN(Network, "{}: add-and-activate request failed: {}.", logContext, errorMessage);
        return false;
    }

    LOG_INFO(Network, "{}: add-and-activate request accepted by NetworkManager.", logContext);
    g_object_unref(context.active);
    return true;
}

bool disconnectDevice(
    NMClient* client, NMDevice* device, GMainContext* mainContext, std::string& errorMessage)
{
    if (!device) {
        errorMessage = "No WiFi device found";
        return false;
    }

    struct DisconnectContext {
        AsyncLoop* loop = nullptr;
        bool success = false;
        std::string* error = nullptr;
    };

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kOperationTimeoutSeconds);
    DisconnectContext context{ loop.get(), false, &errorMessage };

    {
        MainContextThreadDefaultGuard contextGuard(resolveMainContext(client, mainContext));
        nm_device_disconnect_async(
            device,
            nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer userData) {
                auto* ctx = static_cast<DisconnectContext*>(userData);
                GError* error = nullptr;
                ctx->success = nm_device_disconnect_finish(NM_DEVICE(source), result, &error);
                if (!ctx->success) {
                    *ctx->error = formatError(error, "WiFi disconnect failed");
                    g_clear_error(&error);
                }
                g_main_loop_quit(ctx->loop->loop);
            },
            &context);
    }

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi disconnect timed out";
    }

    return context.success;
}

bool activeConnectionUsesDevice(NMActiveConnection* activeConnection, NMDevice* device)
{
    if (!activeConnection || !device) {
        return false;
    }

    const GPtrArray* devices = nm_active_connection_get_devices(activeConnection);
    if (!devices) {
        return false;
    }

    for (guint i = 0; i < devices->len; ++i) {
        if (g_ptr_array_index(devices, i) == device) {
            return true;
        }
    }

    return false;
}

bool deactivateActiveConnection(
    NMClient* client,
    NMActiveConnection* activeConnection,
    GMainContext* mainContext,
    std::string& errorMessage)
{
    if (!client) {
        errorMessage = "No WiFi client available";
        return false;
    }
    if (!activeConnection) {
        errorMessage = "No active WiFi connection available";
        return false;
    }

    struct DeactivateContext {
        AsyncLoop* loop = nullptr;
        bool success = false;
        std::string* error = nullptr;
    };

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kOperationTimeoutSeconds);
    DeactivateContext context{ loop.get(), false, &errorMessage };

    {
        MainContextThreadDefaultGuard contextGuard(resolveMainContext(client, mainContext));
        nm_client_deactivate_connection_async(
            client,
            activeConnection,
            nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer userData) {
                auto* ctx = static_cast<DeactivateContext*>(userData);
                GError* error = nullptr;
                ctx->success =
                    nm_client_deactivate_connection_finish(NM_CLIENT(source), result, &error);
                if (!ctx->success) {
                    *ctx->error = formatError(error, "WiFi cancel failed");
                    g_clear_error(&error);
                }
                g_main_loop_quit(ctx->loop->loop);
            },
            &context);
    }

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "WiFi cancel timed out";
    }

    return context.success;
}

bool deleteRemoteConnection(
    NMClient* client,
    NMRemoteConnection* connection,
    GMainContext* mainContext,
    std::string& errorMessage)
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

    AsyncLoopGuard loop(resolveMainContext(client, mainContext), kOperationTimeoutSeconds);
    DeleteContext context{ loop.get(), false, &errorMessage };

    {
        MainContextThreadDefaultGuard contextGuard(resolveMainContext(client, mainContext));
        nm_remote_connection_delete_async(
            connection,
            nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer userData) {
                auto* ctx = static_cast<DeleteContext*>(userData);
                GError* error = nullptr;
                ctx->success = nm_remote_connection_delete_finish(
                    NM_REMOTE_CONNECTION(source), result, &error);
                if (!ctx->success) {
                    *ctx->error = formatError(error, "WiFi forget failed");
                    g_clear_error(&error);
                }
                g_main_loop_quit(ctx->loop->loop);
            },
            &context);
    }

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

} // namespace

void GObjectDeleter::operator()(gpointer object) const
{
    if (object) {
        g_object_unref(object);
    }
}

GObjectPtr<NMClient> createClient(std::string& errorMessage, GMainContext* mainContext)
{
    if (!mainContext) {
        GError* error = nullptr;
        NMClient* client = nm_client_new(nullptr, &error);
        if (!client) {
            errorMessage = formatError(error, "Failed to initialize NetworkManager client");
            g_clear_error(&error);
            return nullptr;
        }

        return GObjectPtr<NMClient>(client);
    }

    struct CreateContext {
        AsyncLoop* loop = nullptr;
        NMClient* client = nullptr;
        std::string* error = nullptr;
    };

    AsyncLoopGuard loop(mainContext, kClientCreationTimeoutSeconds);
    CreateContext context{ loop.get(), nullptr, &errorMessage };

    {
        MainContextThreadDefaultGuard contextGuard(mainContext);
        nm_client_new_async(
            nullptr,
            +[](GObject*, GAsyncResult* result, gpointer userData) {
                auto* ctx = static_cast<CreateContext*>(userData);
                GError* error = nullptr;
                ctx->client = nm_client_new_finish(result, &error);
                if (!ctx->client) {
                    *ctx->error = formatError(error, "Failed to initialize NetworkManager client");
                    g_clear_error(&error);
                }
                g_main_loop_quit(ctx->loop->loop);
            },
            &context);
    }

    g_main_loop_run(loop.mainLoop());
    if (loop.timedOut() && errorMessage.empty()) {
        errorMessage = "Timed out initializing NetworkManager client";
    }
    if (!context.client) {
        return nullptr;
    }

    return GObjectPtr<NMClient>(context.client);
}

NMDeviceWifi* findWifiDevice(NMClient* client)
{
    if (!client) {
        return nullptr;
    }

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

Result<WifiStatus, std::string> getStatus(NMClient* client)
{
    if (!client) {
        return Result<WifiStatus, std::string>::error("No WiFi client available");
    }

    NMDeviceWifi* device = findWifiDevice(client);
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

Result<std::vector<WifiNetworkInfo>, std::string> listNetworks(
    NMClient* client, const WifiListOptions& options)
{
    if (!client) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error("No WiFi client available");
    }

    NMDeviceWifi* device = findWifiDevice(client);
    if (!device) {
        return Result<std::vector<WifiNetworkInfo>, std::string>::error("No WiFi device found");
    }

    std::optional<std::string> scanError;
    if (options.forceScan) {
        std::string errorMessage;
        if (!forceWifiScan(client, device, options.mainContext, errorMessage)) {
            scanError = errorMessage;
        }
    }

    const auto accessPoints = collectAccessPoints(device);

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

    const GPtrArray* connections = nm_client_get_connections(client);
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

    std::vector<WifiNetworkInfo> availableEntries;
    for (const auto& [ssid, ap] : bestBySsid) {
        if (savedIndexBySsid.find(ssid) != savedIndexBySsid.end()) {
            continue;
        }

        WifiNetworkInfo info{
            .ssid = ssid,
            .status =
                ap.security == "open" ? WifiNetworkStatus::Open : WifiNetworkStatus::Available,
            .signalDbm = ap.signalDbm,
            .security = ap.security,
            .autoConnect = false,
            .hasCredentials = false,
            .lastUsedDate = std::nullopt,
            .lastUsedRelative = "not saved",
            .connectionId = std::string{},
        };

        availableEntries.push_back(info);
    }

    std::sort(
        availableEntries.begin(),
        availableEntries.end(),
        [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
            const int signalA = a.signalDbm.has_value() ? a.signalDbm.value() : -200;
            const int signalB = b.signalDbm.has_value() ? b.signalDbm.value() : -200;
            if (signalA != signalB) {
                return signalA > signalB;
            }
            return a.ssid < b.ssid;
        });

    std::vector<WifiNetworkInfo> networks;
    networks.reserve(savedEntries.size() + availableEntries.size());
    for (const auto& entry : savedEntries) {
        networks.push_back(entry.info);
    }
    for (const auto& entry : availableEntries) {
        networks.push_back(entry);
    }

    return Result<std::vector<WifiNetworkInfo>, std::string>::okay(networks);
}

Result<std::vector<WifiAccessPointInfo>, std::string> listAccessPoints(
    NMClient* client, const WifiListOptions& options)
{
    if (!client) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error(
            "No WiFi client available");
    }

    NMDeviceWifi* device = findWifiDevice(client);
    if (!device) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error("No WiFi device found");
    }

    std::optional<std::string> scanError;
    if (options.forceScan) {
        std::string errorMessage;
        if (!forceWifiScan(client, device, options.mainContext, errorMessage)) {
            scanError = errorMessage;
        }
    }

    const auto accessPoints = collectAccessPoints(device);
    if (scanError.has_value() && accessPoints.empty()) {
        return Result<std::vector<WifiAccessPointInfo>, std::string>::error(scanError.value());
    }

    return Result<std::vector<WifiAccessPointInfo>, std::string>::okay(accessPoints);
}

Result<WifiConnectResult, std::string> connect(
    NMClient* client, const WifiNetworkInfo& network, GMainContext* mainContext)
{
    if (network.ssid.empty()) {
        return Result<WifiConnectResult, std::string>::error("SSID is required");
    }
    if (!client) {
        return Result<WifiConnectResult, std::string>::error("No WiFi client available");
    }

    if (network.status == WifiNetworkStatus::Connected) {
        return Result<WifiConnectResult, std::string>::okay(
            WifiConnectResult{ .success = true, .ssid = network.ssid });
    }

    NMDeviceWifi* device = findWifiDevice(client);
    if (!device) {
        return Result<WifiConnectResult, std::string>::error("No WiFi device found");
    }

    const uint64_t requestId = gNextWifiConnectRequestId.fetch_add(1, std::memory_order_relaxed);
    const std::string requestLogContext =
        makeWifiConnectLogContext(requestId, network.ssid, "network-info");

    LOG_INFO(
        Network,
        "{}: starting (connection_id='{}', visible_aps={}).",
        requestLogContext,
        network.connectionId.empty() ? "none" : network.connectionId,
        formatVisibleAccessPointsForSsid(device, network.ssid));

    if (!network.connectionId.empty()) {
        auto candidates =
            collectWifiConnections(client, device, network.connectionId, WifiConnectionMatch::ById);
        LOG_INFO(
            Network,
            "{}: saved profile candidates for connection id '{}': {}.",
            requestLogContext,
            network.connectionId,
            formatConnectionCandidates(candidates));
        const auto* chosen =
            selectBestWifiConnection("connection id", network.connectionId, candidates);
        if (chosen && chosen->connection) {
            const std::string activationLogContext = requestLogContext
                + " using saved profile uuid="
                + (chosen->uuid.empty() ? std::string("unknown") : chosen->uuid);
            std::string activationError;
            if (!activateConnection(
                    client,
                    chosen->connection,
                    NM_DEVICE(device),
                    findBestAccessPoint(device, network.ssid),
                    mainContext,
                    activationLogContext,
                    activationError)) {
                return Result<WifiConnectResult, std::string>::error(activationError);
            }

            std::string waitError;
            if (!waitForDeviceActivation(
                    client,
                    NM_DEVICE(device),
                    network.ssid,
                    mainContext,
                    kActivationOverallTimeoutSeconds,
                    activationLogContext,
                    waitError)) {
                return Result<WifiConnectResult, std::string>::error(waitError);
            }

            LOG_INFO(Network, "{}: connected successfully.", activationLogContext);
            return Result<WifiConnectResult, std::string>::okay(
                WifiConnectResult{ .success = true, .ssid = network.ssid });
        }
    }

    return connectBySsid(client, network.ssid, std::nullopt, mainContext);
}

Result<WifiConnectResult, std::string> connectBySsid(
    NMClient* client,
    const std::string& ssid,
    const std::optional<std::string>& password,
    GMainContext* mainContext)
{
    if (ssid.empty()) {
        return Result<WifiConnectResult, std::string>::error("SSID is required");
    }
    if (!client) {
        return Result<WifiConnectResult, std::string>::error("No WiFi client available");
    }

    NMDeviceWifi* device = findWifiDevice(client);
    if (!device) {
        return Result<WifiConnectResult, std::string>::error("No WiFi device found");
    }

    const uint64_t requestId = gNextWifiConnectRequestId.fetch_add(1, std::memory_order_relaxed);
    const std::string requestLogContext = makeWifiConnectLogContext(
        requestId, ssid, password.has_value() ? "ephemeral-profile" : "saved-profile");

    LOG_INFO(
        Network,
        "{}: starting (password_provided={}, visible_aps={}).",
        requestLogContext,
        password.has_value(),
        formatVisibleAccessPointsForSsid(device, ssid));

    NMAccessPoint* activeAp = nm_device_wifi_get_active_access_point(device);
    if (activeAp && ssidFromAccessPoint(activeAp) == ssid) {
        LOG_INFO(
            Network,
            "{}: already connected to target SSID on {}.",
            requestLogContext,
            formatAccessPointSummary(activeAp));
        return Result<WifiConnectResult, std::string>::okay(
            WifiConnectResult{ .success = true, .ssid = ssid });
    }

    auto candidates = collectWifiConnections(client, device, ssid, WifiConnectionMatch::BySsid);
    NMAccessPoint* ap = findBestAccessPoint(device, ssid);
    const size_t visibleAccessPointCount = countAccessPointsForSsid(device, ssid);

    LOG_INFO(
        Network,
        "{}: saved profile candidates: {}.",
        requestLogContext,
        formatConnectionCandidates(candidates));

    if (visibleAccessPointCount > 1) {
        const char* preferredBssid = ap ? nm_access_point_get_bssid(ap) : nullptr;
        const auto preferredSignal =
            ap ? strengthToDbm(nm_access_point_get_strength(ap)) : std::nullopt;
        LOG_INFO(
            Network,
            "{}: {} visible access points; preferring BSSID {} ({} MHz, {} dBm).",
            requestLogContext,
            visibleAccessPointCount,
            preferredBssid && *preferredBssid ? preferredBssid : "unknown",
            ap ? nm_access_point_get_frequency(ap) : 0,
            preferredSignal.value_or(-200));
    }

    if (password.has_value() && !candidates.empty()) {
        LOG_INFO(
            Network,
            "Found {} existing WiFi profile(s) for '{}'; will replace after successful connect.",
            candidates.size(),
            ssid);
    }

    if (!password.has_value()) {
        if (!candidates.empty()) {
            selectBestWifiConnection("SSID", ssid, candidates);
        }

        std::string lastError;
        for (size_t index = 0; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            if (!candidate.connection) {
                continue;
            }

            const std::string activationLogContext = makeWifiConnectLogContext(
                requestId, ssid, "saved-profile", static_cast<int>(index + 1), candidates.size());
            LOG_INFO(
                Network,
                "{}: trying candidate {} with target_ap={}.",
                activationLogContext,
                formatConnectionCandidateSummary(candidate),
                formatAccessPointSummary(ap));
            std::string activationError;
            if (!activateConnection(
                    client,
                    candidate.connection,
                    NM_DEVICE(device),
                    ap,
                    mainContext,
                    activationLogContext,
                    activationError)) {
                LOG_WARN(
                    Network,
                    "{}: saved profile activation failed: {}",
                    activationLogContext,
                    activationError);
                lastError = activationError;
                continue;
            }

            std::string waitError;
            if (!waitForDeviceActivation(
                    client,
                    NM_DEVICE(device),
                    ssid,
                    mainContext,
                    kActivationOverallTimeoutSeconds,
                    activationLogContext,
                    waitError)) {
                LOG_WARN(
                    Network, "{}: saved profile wait failed: {}", activationLogContext, waitError);
                lastError = waitError;
                continue;
            }

            LOG_INFO(Network, "{}: connected successfully.", activationLogContext);
            return Result<WifiConnectResult, std::string>::okay(
                WifiConnectResult{ .success = true, .ssid = ssid });
        }

        if (!lastError.empty()) {
            return Result<WifiConnectResult, std::string>::error(lastError);
        }
    }

    if (!password.has_value() && ap && securityFromAccessPoint(ap) != "open") {
        return Result<WifiConnectResult, std::string>::error(
            "Password required for secured network");
    }

    const int maxConnectAttempts = password.has_value() && visibleAccessPointCount > 1
        ? kMultiAccessPointPasswordConnectAttempts
        : 1;
    std::string lastConnectError;
    std::string successfulUuid;

    for (int attempt = 1; attempt <= maxConnectAttempts; ++attempt) {
        const std::string activationLogContext = makeWifiConnectLogContext(
            requestId, ssid, "ephemeral-profile", attempt, maxConnectAttempts);
        if (attempt > 1) {
            std::string scanError;
            if (!forceWifiScan(client, device, mainContext, scanError)) {
                LOG_WARN(
                    Network, "{}: scan failed before retry: {}", activationLogContext, scanError);
            }
            ap = findBestAccessPoint(device, ssid);
            LOG_INFO(
                Network,
                "{}: refreshed visible access points: {}.",
                activationLogContext,
                formatVisibleAccessPointsForSsid(device, ssid));
        }

        LOG_INFO(Network, "{}: target_ap={}.", activationLogContext, formatAccessPointSummary(ap));

        auto connection = buildConnectionForSsid(ssid, password, ap);
        if (!connection) {
            return Result<WifiConnectResult, std::string>::error("Failed to build WiFi connection");
        }

        const std::string createdUuid = nm_connection_get_uuid(connection.get())
            ? nm_connection_get_uuid(connection.get())
            : "";

        std::string attemptError;
        if (!addAndActivateConnection(
                client,
                connection.get(),
                NM_DEVICE(device),
                ap,
                mainContext,
                activationLogContext,
                attemptError)) {
            if (password.has_value() && !createdUuid.empty()) {
                std::string deleteError;
                if (!deleteRemoteConnectionByUuid(client, createdUuid, mainContext, deleteError)) {
                    LOG_WARN(
                        Network, "Failed to delete unsuccessful WiFi profile: {}", deleteError);
                }
            }

            lastConnectError = attemptError;
            if (shouldRetryPasswordConnectAttempt(
                    password,
                    visibleAccessPointCount,
                    lastConnectError,
                    attempt,
                    maxConnectAttempts)) {
                LOG_WARN(
                    Network,
                    "{}: failed: {}. Retrying after scan.",
                    activationLogContext,
                    lastConnectError);
                continue;
            }

            return Result<WifiConnectResult, std::string>::error(lastConnectError);
        }

        std::string waitError;
        if (!waitForDeviceActivation(
                client,
                NM_DEVICE(device),
                ssid,
                mainContext,
                kActivationOverallTimeoutSeconds,
                activationLogContext,
                waitError)) {
            if (password.has_value() && !createdUuid.empty()) {
                std::string deleteError;
                if (!deleteRemoteConnectionByUuid(client, createdUuid, mainContext, deleteError)) {
                    LOG_WARN(
                        Network, "Failed to delete unsuccessful WiFi profile: {}", deleteError);
                }
            }

            lastConnectError = waitError;
            if (shouldRetryPasswordConnectAttempt(
                    password,
                    visibleAccessPointCount,
                    lastConnectError,
                    attempt,
                    maxConnectAttempts)) {
                LOG_WARN(
                    Network,
                    "{}: failed: {}. Retrying after scan.",
                    activationLogContext,
                    lastConnectError);
                continue;
            }

            return Result<WifiConnectResult, std::string>::error(lastConnectError);
        }

        successfulUuid = createdUuid;
        lastConnectError.clear();
        break;
    }

    if (!lastConnectError.empty()) {
        return Result<WifiConnectResult, std::string>::error(lastConnectError);
    }

    if (password.has_value()) {
        if (successfulUuid.empty()) {
            LOG_WARN(Network, "New WiFi profile UUID unavailable; skipping cleanup.");
        }
        else {
            int removed = 0;
            std::string deleteError;
            if (!deleteWifiConnectionsBySsid(
                    client, ssid, successfulUuid, mainContext, removed, deleteError)) {
                LOG_WARN(Network, "Failed to clean up old WiFi profiles: {}", deleteError);
            }
            else if (removed > 0) {
                LOG_INFO(Network, "Removed {} stale WiFi profile(s) for '{}'.", removed, ssid);
            }
        }
    }

    LOG_INFO(Network, "{}: connected successfully.", requestLogContext);
    return Result<WifiConnectResult, std::string>::okay(
        WifiConnectResult{ .success = true, .ssid = ssid });
}

Result<std::monostate, std::string> requestConnectCancel(
    NMClient* client, GMainContext* mainContext)
{
    if (!client) {
        return Result<std::monostate, std::string>::error("No WiFi client available");
    }

    NMDeviceWifi* device = findWifiDevice(client);
    if (!device) {
        return Result<std::monostate, std::string>::error("No WiFi device found");
    }

    NMActiveConnection* activatingConnection = nm_client_get_activating_connection(client);
    if (activeConnectionUsesDevice(activatingConnection, NM_DEVICE(device))) {
        std::string cancelError;
        if (!deactivateActiveConnection(client, activatingConnection, mainContext, cancelError)) {
            return Result<std::monostate, std::string>::error(cancelError);
        }

        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    const NMDeviceState deviceState = nm_device_get_state(NM_DEVICE(device));
    if (deviceState == NM_DEVICE_STATE_ACTIVATED) {
        LOG_INFO(Network, "Ignoring WiFi cancel request because the device is already activated.");
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    if (!isCancelableWifiDeviceState(deviceState)) {
        return Result<std::monostate, std::string>::error("No WiFi connect in progress");
    }

    std::string disconnectError;
    if (!disconnectDevice(client, NM_DEVICE(device), mainContext, disconnectError)) {
        return Result<std::monostate, std::string>::error(disconnectError);
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<WifiDisconnectResult, std::string> disconnect(
    NMClient* client, const std::optional<std::string>& ssid, GMainContext* mainContext)
{
    if (!client) {
        return Result<WifiDisconnectResult, std::string>::error("No WiFi client available");
    }

    LOG_INFO(
        Network,
        "Disconnecting WiFi {}.",
        ssid.has_value() && !ssid->empty() ? *ssid : "active connection");

    NMDeviceWifi* device = findWifiDevice(client);
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
    if (!disconnectDevice(client, NM_DEVICE(device), mainContext, disconnectError)) {
        return Result<WifiDisconnectResult, std::string>::error(disconnectError);
    }

    std::string waitError;
    if (!waitForDeviceDeactivation(
            client, NM_DEVICE(device), mainContext, kDeactivationTimeoutSeconds, waitError)) {
        return Result<WifiDisconnectResult, std::string>::error(waitError);
    }

    LOG_INFO(Network, "WiFi disconnected from '{}'.", activeSsid);
    return Result<WifiDisconnectResult, std::string>::okay(
        WifiDisconnectResult{ .success = true, .ssid = activeSsid });
}

Result<WifiForgetResult, std::string> forget(
    NMClient* client, const std::string& ssid, GMainContext* mainContext)
{
    if (ssid.empty()) {
        return Result<WifiForgetResult, std::string>::error("SSID is required");
    }
    if (!client) {
        return Result<WifiForgetResult, std::string>::error("No WiFi client available");
    }

    LOG_INFO(Network, "Forgetting WiFi profiles for '{}'.", ssid);

    NMDeviceWifi* device = findWifiDevice(client);
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
                if (!disconnectDevice(client, NM_DEVICE(device), mainContext, disconnectError)) {
                    return Result<WifiForgetResult, std::string>::error(disconnectError);
                }
                std::string waitError;
                if (!waitForDeviceDeactivation(
                        client,
                        NM_DEVICE(device),
                        mainContext,
                        kDeactivationTimeoutSeconds,
                        waitError)) {
                    return Result<WifiForgetResult, std::string>::error(waitError);
                }
            }
        }
    }

    const GPtrArray* connections = nm_client_get_connections(client);
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
        if (!deleteRemoteConnection(client, connection.get(), mainContext, deleteError)) {
            return Result<WifiForgetResult, std::string>::error(deleteError);
        }
        ++removed;
    }

    return Result<WifiForgetResult, std::string>::okay(
        WifiForgetResult{ .success = true, .ssid = ssid, .removed = removed });
}

} // namespace Internal
} // namespace Network
} // namespace DirtSim
