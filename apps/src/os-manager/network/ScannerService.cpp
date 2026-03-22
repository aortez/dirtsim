#include "ScannerService.h"
#include "core/LoggingChannels.h"
#include "os-manager/network/ScannerChannelController.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>

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
    : config_(), channelController_(std::move(channelController))
{}

ScannerService::ScannerService(
    Config config, std::shared_ptr<ScannerChannelController> channelController)
    : config_(std::move(config)), channelController_(std::move(channelController))
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
        currentChannel_.reset();
        lastError_.clear();
        radiosByBssid_.clear();
        socketFd_ = fd;
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
        currentChannel_.reset();
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
        snapshot.currentChannel = currentChannel_;
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

std::string ScannerService::lastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void ScannerService::setSnapshotChangedCallback(SnapshotChangedCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshotChangedCallback_ = std::move(callback);
}

std::vector<int> ScannerService::buildChannelPlan() const
{
    std::vector<int> channels;
    channels.reserve(24);

    // 2.4 GHz: 1-11.
    for (int ch = 1; ch <= 11; ++ch) {
        channels.push_back(ch);
    }

    // 5 GHz: common non-DFS ranges.
    for (int ch : { 36, 40, 44, 48, 149, 153, 157, 161, 165 }) {
        channels.push_back(ch);
    }

    return channels;
}

Result<std::monostate, std::string> ScannerService::setChannel(int channel)
{
    if (!channelController_) {
        return Result<std::monostate, std::string>::error(
            "Missing dependency for scannerChannelController");
    }

    return channelController_->setChannel20MHz(channel);
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

void ScannerService::handlePacket(
    const uint8_t* data, size_t length, int channelHint, std::chrono::steady_clock::time_point now)
{
    const auto parsed = parseBeaconOrProbeResponse(data, length, channelHint);
    if (!parsed.has_value() || parsed->bssid.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& state = radiosByBssid_[parsed->bssid];
    if (!parsed->ssid.empty()) {
        state.ssid = parsed->ssid;
    }
    if (parsed->signalDbm.has_value()) {
        state.signalDbm = parsed->signalDbm;
    }
    if (parsed->channel.has_value()) {
        state.channel = parsed->channel;
    }
    state.lastSeenAt = now;
}

void ScannerService::threadMain()
{
#ifndef __linux__
    return;
#else
    LOG_INFO(Network, "ScannerService thread started");

    const auto channels = buildChannelPlan();
    const auto dwell = std::chrono::milliseconds(std::max(50, config_.dwellMs));

    std::array<uint8_t, 8192> buffer{};

    while (!stopRequested_) {
        for (const int channel : channels) {
            if (stopRequested_) {
                break;
            }

            const auto setChannelResult = setChannel(channel);
            if (setChannelResult.isError()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    lastError_ = "Failed to set channel " + std::to_string(channel) + ": "
                        + setChannelResult.errorValue();
                }
                notifySnapshotChanged();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentChannel_ = channel;
                lastError_.clear();
            }

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
                handlePacket(buffer.data(), static_cast<size_t>(received), channel, now);
            }

            pruneOldRadios(std::chrono::steady_clock::now());
            notifySnapshotChanged();
        }
    }

    LOG_INFO(Network, "ScannerService thread exiting");
#endif
}

} // namespace OsManager
} // namespace DirtSim
