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
    };

    struct Snapshot {
        bool running = false;
        std::optional<ScannerTuning> currentTuning;
        ScannerBand focusBand = ScannerBand::Band5Ghz;
        int focusWidthMhz = 20;
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
    std::string lastError() const;
    Result<ProbeResult, std::string> runProbe(const ProbeRequest& request);
    Result<std::monostate, std::string> setFocus(ScannerBand band, int widthMhz);
    void setSnapshotChangedCallback(SnapshotChangedCallback callback);

private:
    struct RadioState {
        std::string ssid;
        std::optional<int> signalDbm;
        std::optional<int> channel;
        std::chrono::steady_clock::time_point lastSeenAt;
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
    std::optional<PacketObservation> handlePacket(
        const uint8_t* data,
        size_t length,
        int channelHint,
        std::chrono::steady_clock::time_point now);

    Config config_;
    std::shared_ptr<ScannerChannelController> channelController_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RadioState> radiosByBssid_;
    std::optional<ScannerTuning> currentTuning_;
    std::string lastError_;
    int socketFd_ = -1;
    mutable std::mutex probeMutex_;
    std::shared_ptr<PendingProbe> pendingProbe_;
    SnapshotChangedCallback snapshotChangedCallback_;
    std::unique_ptr<ScanPlanner> planner_;
    std::atomic<ScannerBand> requestedFocusBand_{ ScannerBand::Band5Ghz };
    std::atomic<int> requestedFocusWidthMhz_{ 20 };

    std::atomic<bool> stopRequested_{ false };
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

} // namespace OsManager
} // namespace DirtSim
