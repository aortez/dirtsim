#pragma once

#include "core/Result.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace OsManager {

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
        std::optional<int> currentChannel;
        std::vector<ObservedRadio> radios;
    };

    struct Config {
        std::string interfaceName = "wlan0";
        int dwellMs = 250;
        uint64_t retentionMs = 60000;
    };

    explicit ScannerService(std::function<int(const std::string&)> systemCommand);
    explicit ScannerService(Config config, std::function<int(const std::string&)> systemCommand);
    ~ScannerService();

    ScannerService(const ScannerService&) = delete;
    ScannerService& operator=(const ScannerService&) = delete;

    Result<std::monostate, std::string> start();
    void stop();

    Snapshot snapshot(uint64_t maxAgeMs, size_t maxRadios) const;
    std::string lastError() const;

private:
    struct RadioState {
        std::string ssid;
        std::optional<int> signalDbm;
        std::optional<int> channel;
        std::chrono::steady_clock::time_point lastSeenAt;
    };

    void threadMain();
    std::vector<int> buildChannelPlan() const;
    bool setChannel(int channel) const;
    void pruneOldRadios(std::chrono::steady_clock::time_point now);
    void handlePacket(
        const uint8_t* data,
        size_t length,
        int channelHint,
        std::chrono::steady_clock::time_point now);

    Config config_;
    std::function<int(const std::string&)> systemCommand_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RadioState> radiosByBssid_;
    std::optional<int> currentChannel_;
    std::string lastError_;
    int socketFd_ = -1;

    std::atomic<bool> stopRequested_{ false };
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

} // namespace OsManager
} // namespace DirtSim
