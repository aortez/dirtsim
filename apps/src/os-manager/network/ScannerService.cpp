#include "ScannerService.h"
#include "core/LoggingChannels.h"
#include "os-manager/network/AdaptiveScanPlanner.h"
#include "os-manager/network/ScannerChannelController.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <unordered_set>

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <unistd.h>
#endif

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kPollTimeoutMs = 50;
constexpr int kMinDwellMs = 100;
constexpr int kManualDwellMs = 250;
constexpr int kProbeTimeoutPaddingMs = 5000;

uint16_t readLe16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

size_t alignTo(size_t offset, size_t alignment)
{
    if (alignment <= 1) {
        return offset;
    }
    const size_t remainder = offset % alignment;
    if (remainder == 0) {
        return offset;
    }
    return offset + (alignment - remainder);
}

std::string formatMac(const uint8_t mac[6])
{
    char buf[18];
    std::snprintf(
        buf,
        sizeof(buf),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
    return std::string(buf);
}

std::string sanitizeSsid(const uint8_t* data, size_t length)
{
    std::string ssid;
    ssid.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = data[i];
        if (c >= 32 && c <= 126) {
            ssid.push_back(static_cast<char>(c));
        }
        else {
            ssid.push_back('?');
        }
    }
    return ssid;
}

std::optional<int> frequencyToChannel(int freqMhz)
{
    if (freqMhz == 2484) {
        return 14;
    }
    if (freqMhz >= 2412 && freqMhz <= 2472) {
        return (freqMhz - 2407) / 5;
    }
    if (freqMhz >= 5000 && freqMhz <= 5895) {
        return (freqMhz - 5000) / 5;
    }
    if (freqMhz >= 5955 && freqMhz <= 7115) {
        return (freqMhz - 5950) / 5;
    }
    return std::nullopt;
}

struct RadiotapInfo {
    uint16_t headerLength = 0;
    std::optional<int> signalDbm;
    std::optional<int> channelFreqMhz;
};

ScannerService::ProbeDwell probeDwellFromObservation(const StepObservation& observation)
{
    return ScannerService::ProbeDwell{
        .sawTraffic = observation.sawTraffic,
        .radiosSeen = static_cast<uint32_t>(observation.radiosSeen),
        .newRadiosSeen = static_cast<uint32_t>(observation.newRadiosSeen),
        .strongestSignalDbm = observation.strongestSignalDbm,
        .observedChannels = observation.observedChannels,
    };
}

RadiotapInfo parseRadiotap(const uint8_t* data, size_t length)
{
    RadiotapInfo info;
    if (length < 8) {
        return info;
    }

    const uint16_t headerLength = readLe16(data + 2);
    if (headerLength < 8 || headerLength > length) {
        return info;
    }

    std::vector<uint32_t> presentWords;
    presentWords.reserve(2);

    size_t presentOffset = 4;
    uint32_t word = readLe32(data + presentOffset);
    presentWords.push_back(word);
    presentOffset += 4;

    while (word & 0x80000000U) {
        if (presentOffset + 4 > headerLength) {
            return info;
        }

        word = readLe32(data + presentOffset);
        presentWords.push_back(word);
        presentOffset += 4;
    }

    const auto hasBit = [&](int field) -> bool {
        if (field < 0) {
            return false;
        }

        const size_t wordIndex = static_cast<size_t>(field / 32);
        const int bitIndex = field % 32;
        if (wordIndex >= presentWords.size()) {
            return false;
        }

        return (presentWords[wordIndex] & (1U << bitIndex)) != 0;
    };

    size_t fieldOffset = presentOffset;

    // Fields 0..5 cover TSFT, FLAGS, RATE, CHANNEL, FHSS, and DBM_ANTSIGNAL.
    for (int field = 0; field <= 5; ++field) {
        if (!hasBit(field)) {
            continue;
        }

        size_t align = 1;
        size_t size = 0;

        switch (field) {
            case 0: // TSFT: u64
                align = 8;
                size = 8;
                break;
            case 1: // FLAGS: u8
                align = 1;
                size = 1;
                break;
            case 2: // RATE: u8
                align = 1;
                size = 1;
                break;
            case 3: // CHANNEL: u16 freq, u16 flags
                align = 2;
                size = 4;
                break;
            case 4: // FHSS: u16
                align = 2;
                size = 2;
                break;
            case 5: // DBM_ANTSIGNAL: s8
                align = 1;
                size = 1;
                break;
            default:
                break;
        }

        fieldOffset = alignTo(fieldOffset, align);
        if (fieldOffset + size > headerLength) {
            return info;
        }

        if (field == 3) {
            info.channelFreqMhz = static_cast<int>(readLe16(data + fieldOffset));
        }
        else if (field == 5) {
            info.signalDbm = static_cast<int>(static_cast<int8_t>(data[fieldOffset]));
        }

        fieldOffset += size;
    }

    info.headerLength = headerLength;
    return info;
}

struct ParsedBeacon {
    std::string bssid;
    std::string ssid;
    std::optional<int> signalDbm;
    std::optional<int> channel;
};

std::optional<ParsedBeacon> parseBeaconOrProbeResponse(
    const uint8_t* data, size_t length, int channelHint)
{
    const RadiotapInfo rt = parseRadiotap(data, length);
    if (rt.headerLength == 0 || rt.headerLength > length) {
        return std::nullopt;
    }

    const uint8_t* frame = data + rt.headerLength;
    const size_t frameLength = length - rt.headerLength;
    if (frameLength < 36) {
        return std::nullopt;
    }

    const uint16_t fc = readLe16(frame);
    const int type = (fc >> 2) & 0x3;
    const int subtype = (fc >> 4) & 0xF;
    if (type != 0 || (subtype != 8 && subtype != 5)) {
        return std::nullopt;
    }

    // Management header: addr3 at offset 16.
    const uint8_t* bssidMac = frame + 16;

    std::string ssid;
    std::optional<int> channel;

    std::optional<int> dsChannel;
    std::optional<int> htPrimaryChannel;

    size_t offset = 36;
    while (offset + 2 <= frameLength) {
        const uint8_t id = frame[offset];
        const uint8_t len = frame[offset + 1];
        offset += 2;
        if (offset + len > frameLength) {
            break;
        }

        const uint8_t* payload = frame + offset;
        if (id == 0) {
            ssid = sanitizeSsid(payload, len);
        }
        else if (id == 3 && len >= 1) {
            dsChannel = static_cast<int>(payload[0]);
        }
        else if (id == 61 && len >= 1) {
            htPrimaryChannel = static_cast<int>(payload[0]);
        }

        offset += len;
    }

    if (dsChannel.has_value()) {
        channel = dsChannel;
    }
    else if (htPrimaryChannel.has_value()) {
        channel = htPrimaryChannel;
    }
    else if (rt.channelFreqMhz.has_value()) {
        channel = frequencyToChannel(*rt.channelFreqMhz);
    }
    else if (channelHint > 0) {
        channel = channelHint;
    }

    ParsedBeacon parsed;
    parsed.bssid = formatMac(reinterpret_cast<const uint8_t (*)[6]>(bssidMac)[0]);
    parsed.ssid = std::move(ssid);
    parsed.signalDbm = rt.signalDbm;
    parsed.channel = channel;
    return parsed;
}

} // namespace

ScannerService::ScannerService(std::shared_ptr<ScannerChannelController> channelController)
    : config_(),
      channelController_(std::move(channelController)),
      planner_(std::make_unique<AdaptiveScanPlanner>())
{}

ScannerService::ScannerService(
    Config config, std::shared_ptr<ScannerChannelController> channelController)
    : config_(std::move(config)),
      channelController_(std::move(channelController)),
      planner_(std::make_unique<AdaptiveScanPlanner>())
{}

ScannerService::~ScannerService()
{
    stop();
}

Result<std::monostate, std::string> ScannerService::start()
{
#ifndef __linux__
    return Result<std::monostate, std::string>::error("Scanner capture is only supported on Linux");
#else
    if (running_) {
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    stopRequested_ = false;

    const unsigned int ifindex = if_nametoindex(config_.interfaceName.c_str());
    if (ifindex == 0) {
        return Result<std::monostate, std::string>::error(
            "Unknown interface: " + config_.interfaceName);
    }

    const int fd = ::socket(AF_PACKET, SOCK_RAW, htons(0x0003)); // ETH_P_ALL
    if (fd < 0) {
        return Result<std::monostate, std::string>::error(
            std::string("Failed to open raw socket: ") + std::strerror(errno));
    }

    sockaddr_ll sll{};
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(0x0003); // ETH_P_ALL
    sll.sll_ifindex = static_cast<int>(ifindex);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&sll), sizeof(sll)) < 0) {
        const std::string error = std::string("Failed to bind raw socket: ") + std::strerror(errno);
        ::close(fd);
        return Result<std::monostate, std::string>::error(error);
    }

    if (!channelController_) {
        ::close(fd);
        return Result<std::monostate, std::string>::error(
            "Missing dependency for scannerChannelController");
    }

    const auto channelControllerStartResult = channelController_->start();
    if (channelControllerStartResult.isError()) {
        ::close(fd);
        return Result<std::monostate, std::string>::error(
            channelControllerStartResult.errorValue());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        currentTuning_.reset();
        lastError_.clear();
        radiosByBssid_.clear();
        socketFd_ = fd;
    }

    if (planner_) {
        planner_->reset();
        std::lock_guard<std::mutex> lock(mutex_);
        if (requestedConfig_.mode == ScannerConfigMode::Auto) {
            planner_->setAutoConfig(requestedConfig_.autoConfig);
        }
    }

    running_ = true;
    thread_ = std::thread(&ScannerService::threadMain, this);
    notifySnapshotChanged();
    return Result<std::monostate, std::string>::okay(std::monostate{});
#endif
}

void ScannerService::stop()
{
    const bool wasRunning = running_;

#ifdef __linux__
    stopRequested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }

    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fd = socketFd_;
        socketFd_ = -1;
        currentTuning_.reset();
        lastError_.clear();
        radiosByBssid_.clear();
    }
    if (fd >= 0) {
        ::close(fd);
    }
    if (channelController_) {
        channelController_->stop();
    }
#endif

    running_ = false;
    if (wasRunning) {
        notifySnapshotChanged();
    }
}

ScannerService::Snapshot ScannerService::snapshot(uint64_t maxAgeMs, size_t maxRadios) const
{
    Snapshot snapshot;
    snapshot.running = running_;

    const auto now = std::chrono::steady_clock::now();
    const auto maxAge = std::chrono::milliseconds(maxAgeMs);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.config = requestedConfig_;
        snapshot.currentTuning = currentTuning_;
        snapshot.radios.reserve(radiosByBssid_.size());
        for (const auto& [bssid, state] : radiosByBssid_) {
            if (maxAgeMs > 0 && now - state.lastSeenAt > maxAge) {
                continue;
            }

            ObservedRadio radio;
            radio.bssid = bssid;
            radio.ssid = state.ssid;
            radio.signalDbm = state.signalDbm;
            radio.channel = state.channel;
            radio.lastSeenAt = state.lastSeenAt;
            radio.observationKind = state.observationKind;
            snapshot.radios.push_back(std::move(radio));
        }
    }

    std::sort(
        snapshot.radios.begin(),
        snapshot.radios.end(),
        [](const ObservedRadio& a, const ObservedRadio& b) {
            const bool aHasSignal = a.signalDbm.has_value();
            const bool bHasSignal = b.signalDbm.has_value();
            if (aHasSignal && bHasSignal) {
                if (a.signalDbm.value() != b.signalDbm.value()) {
                    return a.signalDbm.value() > b.signalDbm.value();
                }
            }
            else if (aHasSignal != bHasSignal) {
                return aHasSignal;
            }

            return a.bssid < b.bssid;
        });

    if (maxRadios > 0 && snapshot.radios.size() > maxRadios) {
        snapshot.radios.resize(maxRadios);
    }

    return snapshot;
}

ScannerConfig ScannerService::config() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return requestedConfig_;
}

std::string ScannerService::lastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

Result<ScannerService::ProbeResult, std::string> ScannerService::runProbe(
    const ProbeRequest& request)
{
    if (!running_) {
        return Result<ProbeResult, std::string>::error("Scanner service is not running.");
    }
    if (request.sampleCount == 0) {
        return Result<ProbeResult, std::string>::error(
            "Scanner probe sampleCount must be at least 1.");
    }
    if (!scannerWidthSupported(request.tuning.band, request.tuning.widthMhz)) {
        return Result<ProbeResult, std::string>::error(
            "Unsupported " + scannerBandLabel(request.tuning.band) + " scanner width "
            + std::to_string(request.tuning.widthMhz) + " MHz");
    }

    auto probe = std::make_shared<PendingProbe>();
    probe->request = request;
    probe->result = ProbeResult{
        .tuning = request.tuning,
        .dwellMs = std::max(kMinDwellMs, request.dwellMs),
        .dwells = {},
    };
    probe->result.dwells.reserve(request.sampleCount);

    auto future = probe->promise.get_future();
    {
        std::lock_guard<std::mutex> lock(probeMutex_);
        if (pendingProbe_ && !pendingProbe_->completed) {
            return Result<ProbeResult, std::string>::error("Scanner probe already running.");
        }
        pendingProbe_ = probe;
    }

    const int totalDwellMs =
        std::max(kMinDwellMs, request.dwellMs) * static_cast<int>(request.sampleCount);
    const auto timeout = std::chrono::milliseconds(totalDwellMs + kProbeTimeoutPaddingMs);
    if (future.wait_for(timeout) != std::future_status::ready) {
        return Result<ProbeResult, std::string>::error("Scanner probe timed out.");
    }

    return future.get();
}

Result<std::monostate, std::string> ScannerService::setConfig(const ScannerConfig& config)
{
    if (!scannerWidthSupported(config.autoConfig.band, config.autoConfig.widthMhz)) {
        return Result<std::monostate, std::string>::error(
            "Unsupported " + scannerBandLabel(config.autoConfig.band) + " scanner width "
            + std::to_string(config.autoConfig.widthMhz) + " MHz");
    }

    const auto manualTuningResult = scannerManualTargetToTuning(config.manualConfig);
    if (manualTuningResult.isError()) {
        return Result<std::monostate, std::string>::error(manualTuningResult.errorValue());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        requestedConfig_ = config;
    }
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void ScannerService::setSnapshotChangedCallback(SnapshotChangedCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshotChangedCallback_ = std::move(callback);
}

Result<std::monostate, std::string> ScannerService::setChannel(const ScannerTuning& tuning)
{
    if (!channelController_) {
        return Result<std::monostate, std::string>::error(
            "Missing dependency for scannerChannelController");
    }

    return channelController_->setTuning(tuning);
}

void ScannerService::pruneOldRadios(std::chrono::steady_clock::time_point now)
{
    const auto retention = std::chrono::milliseconds(config_.retentionMs);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = radiosByBssid_.begin(); it != radiosByBssid_.end();) {
        if (now - it->second.lastSeenAt > retention) {
            it = radiosByBssid_.erase(it);
        }
        else {
            ++it;
        }
    }
}

void ScannerService::notifySnapshotChanged()
{
    SnapshotChangedCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = snapshotChangedCallback_;
    }

    if (callback) {
        callback();
    }
}

void ScannerService::completeProbe(
    const std::shared_ptr<PendingProbe>& probe, Result<ProbeResult, std::string> result)
{
    if (!probe) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(probeMutex_);
        if (probe->completed) {
            return;
        }
        probe->completed = true;
        if (pendingProbe_ == probe) {
            pendingProbe_.reset();
        }
    }

    probe->promise.set_value(std::move(result));
}

std::shared_ptr<ScannerService::PendingProbe> ScannerService::currentProbe() const
{
    std::lock_guard<std::mutex> lock(probeMutex_);
    return pendingProbe_;
}

std::optional<ScannerService::PacketObservation> ScannerService::handlePacket(
    const uint8_t* data,
    size_t length,
    const ScannerTuning& tuning,
    std::chrono::steady_clock::time_point now)
{
    const auto parsed = parseBeaconOrProbeResponse(data, length, tuning.primaryChannel);
    if (!parsed.has_value() || parsed->bssid.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto existingIt = radiosByBssid_.find(parsed->bssid);
    const bool isNewRadio = existingIt == radiosByBssid_.end();
    auto& state = radiosByBssid_[parsed->bssid];
    if (!parsed->ssid.empty()) {
        state.ssid = parsed->ssid;
    }
    if (parsed->signalDbm.has_value()) {
        state.signalDbm = parsed->signalDbm;
    }
    if (parsed->channel.has_value()) {
        state.channel = parsed->channel;
        state.observationKind =
            scannerObservationKindForPrimaryChannel(tuning, parsed->channel.value());
    }
    else {
        state.observationKind = ScannerObservationKind::Direct;
    }
    state.lastSeenAt = now;
    return PacketObservation{
        .bssid = parsed->bssid,
        .channel = parsed->channel,
        .signalDbm = parsed->signalDbm,
        .isNewRadio = isNewRadio,
    };
}

void ScannerService::threadMain()
{
#ifndef __linux__
    return;
#else
    LOG_INFO(Network, "ScannerService thread started");

    std::array<uint8_t, 8192> buffer{};

    while (!stopRequested_) {
        const auto probe = currentProbe();
        if (!probe && !planner_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        ScanStep step;
        if (probe) {
            step = ScanStep{
                .tuning = probe->request.tuning,
                .dwellMs = std::max(kMinDwellMs, probe->request.dwellMs),
            };
        }
        else {
            ScannerConfig configCopy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                configCopy = requestedConfig_;
            }

            if (configCopy.mode == ScannerConfigMode::Manual) {
                const auto manualTuningResult =
                    scannerManualTargetToTuning(configCopy.manualConfig);
                if (manualTuningResult.isError()) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        currentTuning_.reset();
                        lastError_ = manualTuningResult.errorValue();
                    }
                    notifySnapshotChanged();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                step = ScanStep{
                    .tuning = manualTuningResult.value(),
                    .dwellMs = kManualDwellMs,
                };
            }
            else {
                planner_->setAutoConfig(configCopy.autoConfig);
                step = planner_->nextStep(std::chrono::steady_clock::now());
            }
        }

        const auto setChannelResult = setChannel(step.tuning);
        if (setChannelResult.isError()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentTuning_.reset();
                lastError_ = "Failed to set channel " + std::to_string(step.tuning.primaryChannel)
                    + ": " + setChannelResult.errorValue();
            }
            if (probe) {
                completeProbe(
                    probe,
                    Result<ProbeResult, std::string>::error(
                        "Failed to set probe tuning " + std::to_string(step.tuning.primaryChannel)
                        + ": " + setChannelResult.errorValue()));
            }
            notifySnapshotChanged();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentTuning_ = step.tuning;
            lastError_.clear();
        }

        StepObservation observation{
            .step = step,
            .sawTraffic = false,
            .radiosSeen = 0,
            .newRadiosSeen = 0,
            .strongestSignalDbm = std::nullopt,
            .observedChannels = {},
        };
        std::unordered_set<std::string> observedBssids;
        std::unordered_set<int> observedChannels;
        const auto dwell = std::chrono::milliseconds(std::max(kMinDwellMs, step.dwellMs));
        const auto start = std::chrono::steady_clock::now();

        while (!stopRequested_ && std::chrono::steady_clock::now() - start < dwell) {
            int fd = -1;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                fd = socketFd_;
            }
            if (fd < 0) {
                break;
            }

            pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;

            const int pollResult = ::poll(&pfd, 1, kPollTimeoutMs);
            if (pollResult <= 0) {
                continue;
            }
            if ((pfd.revents & POLLIN) == 0) {
                continue;
            }

            const ssize_t received =
                ::recvfrom(fd, buffer.data(), buffer.size(), 0, nullptr, nullptr);
            if (received <= 0) {
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            const auto packetObservation =
                handlePacket(buffer.data(), static_cast<size_t>(received), step.tuning, now);
            if (!packetObservation.has_value()) {
                continue;
            }

            observation.sawTraffic = true;
            if (observedBssids.insert(packetObservation->bssid).second) {
                ++observation.radiosSeen;
                if (packetObservation->isNewRadio) {
                    ++observation.newRadiosSeen;
                }
            }
            if (packetObservation->signalDbm.has_value()
                && (!observation.strongestSignalDbm.has_value()
                    || packetObservation->signalDbm.value()
                        > observation.strongestSignalDbm.value())) {
                observation.strongestSignalDbm = packetObservation->signalDbm;
            }
            if (packetObservation->channel.has_value()) {
                observedChannels.insert(packetObservation->channel.value());
            }
        }

        observation.observedChannels.assign(observedChannels.begin(), observedChannels.end());
        std::sort(observation.observedChannels.begin(), observation.observedChannels.end());
        if (probe) {
            probe->result.dwells.push_back(probeDwellFromObservation(observation));
            if (probe->result.dwells.size() >= probe->request.sampleCount) {
                completeProbe(
                    probe,
                    Result<ProbeResult, std::string>::okay(
                        ProbeResult{
                            .tuning = probe->result.tuning,
                            .dwellMs = probe->result.dwellMs,
                            .dwells = probe->result.dwells,
                        }));
            }
        }
        else {
            ScannerConfig configCopy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                configCopy = requestedConfig_;
            }
            if (configCopy.mode != ScannerConfigMode::Auto) {
                pruneOldRadios(std::chrono::steady_clock::now());
                notifySnapshotChanged();
                continue;
            }
            planner_->recordObservation(observation, std::chrono::steady_clock::now());
        }
        pruneOldRadios(std::chrono::steady_clock::now());
        notifySnapshotChanged();
    }

    completeProbe(
        currentProbe(), Result<ProbeResult, std::string>::error("Scanner thread stopped."));
    LOG_INFO(Network, "ScannerService thread exiting");
#endif
}

} // namespace OsManager
} // namespace DirtSim
