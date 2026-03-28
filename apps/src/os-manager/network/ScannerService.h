#pragma once

#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include "os-manager/network/ScanPlanner.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace OsManager {

class ScannerChannelController;

class ScannerService {
public:
    struct ObservedRadio {
        std::string bssid;
        std::string ssid;
        std::optional<int> signalDbm;
        std::optional<int> channel;
        std::chrono::steady_clock::time_point lastSeenAt;
        ScannerObservationKind observationKind = ScannerObservationKind::Direct;
    };

    struct Snapshot {
        bool running = false;
        ScannerConfig config = scannerDefaultConfig();
        std::optional<ScannerTuning> currentTuning;
        std::vector<ObservedRadio> radios;
    };

    struct ProbeDwell {
        bool sawTraffic = false;
        uint32_t radiosSeen = 0;
        uint32_t newRadiosSeen = 0;
        std::optional<int> strongestSignalDbm;
        std::vector<int> observedChannels;
    };

    struct ProbeRequest {
        ScannerTuning tuning;
        int dwellMs = 250;
        uint32_t sampleCount = 10;
    };

    struct ProbeResult {
        ScannerTuning tuning;
        int dwellMs = 0;
        std::vector<ProbeDwell> dwells;
    };

    using SnapshotChangedCallback = std::function<void()>;

    struct Config {
        std::string interfaceName = "wlan0";
        uint64_t retentionMs = 60000;
    };

    explicit ScannerService(std::shared_ptr<ScannerChannelController> channelController);
    explicit ScannerService(
        Config config, std::shared_ptr<ScannerChannelController> channelController);
    ~ScannerService();

    ScannerService(const ScannerService&) = delete;
    ScannerService& operator=(const ScannerService&) = delete;

    Result<std::monostate, std::string> start();
    void stop();

    Snapshot snapshot(uint64_t maxAgeMs, size_t maxRadios) const;
    ScannerConfig config() const;
    std::string lastError() const;
    Result<ProbeResult, std::string> runProbe(const ProbeRequest& request);
    Result<std::monostate, std::string> setConfig(const ScannerConfig& config);
    void setSnapshotChangedCallback(SnapshotChangedCallback callback);

private:
    struct RadioState {
        std::string ssid;
        std::optional<int> signalDbm;
        std::optional<int> channel;
        std::optional<int> lastIncidentalLoggedChannel;
        std::optional<std::chrono::steady_clock::time_point> lastIncidentalLoggedAt;
        std::chrono::steady_clock::time_point lastSeenAt;
        ScannerObservationKind observationKind = ScannerObservationKind::Direct;
    };

    void threadMain();
    Result<std::monostate, std::string> setChannel(const ScannerTuning& tuning);
    void pruneOldRadios(std::chrono::steady_clock::time_point now);
    void notifySnapshotChanged();
    struct PacketObservation {
        std::string bssid;
        std::optional<int> channel;
        std::optional<int> signalDbm;
        bool isNewRadio = false;
    };
    struct PendingProbe {
        ProbeRequest request;
        ProbeResult result;
        std::promise<Result<ProbeResult, std::string>> promise;
        bool completed = false;
    };

    void completeProbe(
        const std::shared_ptr<PendingProbe>& probe, Result<ProbeResult, std::string> result);
    std::shared_ptr<PendingProbe> currentProbe() const;
    void maybeLogProbeRequest(
        const uint8_t* data,
        size_t length,
        const ScannerTuning& tuning,
        std::chrono::steady_clock::time_point now);
    void maybeSampleManualReadback(
        const ScannerTuning& tuning, std::chrono::steady_clock::time_point now);
    void resetManualReadbackSampler();
    std::optional<PacketObservation> handlePacket(
        const uint8_t* data,
        size_t length,
        const ScannerTuning& tuning,
        std::chrono::steady_clock::time_point now);

    Config config_;
    std::shared_ptr<ScannerChannelController> channelController_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        probeRequestLoggedAtByKey_;
    std::unordered_map<std::string, RadioState> radiosByBssid_;
    ScannerConfig activeConfig_ = scannerDefaultConfig();
    std::optional<ScannerTuning> currentTuning_;
    std::string lastError_;
    ScannerConfig requestedConfig_ = scannerDefaultConfig();
    int socketFd_ = -1;
    mutable std::mutex probeMutex_;
    std::shared_ptr<PendingProbe> pendingProbe_;
    SnapshotChangedCallback snapshotChangedCallback_;
    std::unique_ptr<ScanPlanner> planner_;
    std::optional<std::string> localInterfaceMac_;
    std::optional<uint32_t> manualReadbackExpectedChanspec_;
    std::optional<std::chrono::steady_clock::time_point> manualReadbackLastSampleAt_;
    std::optional<uint32_t> manualReadbackLastUnexpectedChanspec_;

    std::atomic<bool> stopRequested_{ false };
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

} // namespace OsManager
} // namespace DirtSim
