#include "ScannerService.h"
#include "core/LoggingChannels.h"
#include "os-manager/network/AdaptiveScanPlanner.h"
#include "os-manager/network/NexmonChannelProtocol.h"
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
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kPollTimeoutMs = 50;
constexpr int kMinDwellMs = 100;
constexpr int kManualDwellMs = 250;
constexpr int kProbeTimeoutPaddingMs = 5000;
constexpr auto kIncidentalObservationLogCooldown = std::chrono::seconds(5);
constexpr auto kManualRetuneDrainWindow = std::chrono::milliseconds(25);
constexpr auto kManualRetuneReadbackPollInterval = std::chrono::milliseconds(10);
constexpr auto kManualRetuneReadbackTimeout = std::chrono::milliseconds(150);
constexpr auto kManualReadbackSampleInterval = std::chrono::milliseconds(100);

enum class ParsedChannelSource { DsParameterSet = 0, HtOperation, Radiotap, Hint };
enum class ParsedManagementSubtype { ProbeRequest = 4, ProbeResponse = 5, Beacon = 8 };

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

#ifdef __linux__
std::optional<std::string> interfaceMacAddress(const std::string& interfaceName)
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return std::nullopt;
    }

    ifreq request{};
    std::snprintf(request.ifr_name, sizeof(request.ifr_name), "%s", interfaceName.c_str());
    if (::ioctl(fd, SIOCGIFHWADDR, &request) < 0) {
        ::close(fd);
        return std::nullopt;
    }

    ::close(fd);
    const auto* mac = reinterpret_cast<const uint8_t*>(request.ifr_hwaddr.sa_data);
    return formatMac(mac);
}
#endif

bool isBroadcastMac(const std::string& mac)
{
    return mac == "FF:FF:FF:FF:FF:FF";
}

std::string receiverRoleLabel(
    const std::string& receiverMac, const std::optional<std::string>& localInterfaceMac)
{
    if (isBroadcastMac(receiverMac)) {
        return "Broadcast";
    }
    if (localInterfaceMac == receiverMac) {
        return "Local";
    }
    if (localInterfaceMac.has_value()) {
        return "Foreign";
    }
    return "Unknown";
}

std::string transmitterRoleLabel(
    const std::string& transmitterMac, const std::optional<std::string>& localInterfaceMac)
{
    if (localInterfaceMac == transmitterMac) {
        return "Local";
    }
    if (localInterfaceMac.has_value()) {
        return "Foreign";
    }
    return "Unknown";
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

bool scannerTuningsEqual(const ScannerTuning& lhs, const ScannerTuning& rhs)
{
    return lhs.band == rhs.band && lhs.primaryChannel == rhs.primaryChannel
        && lhs.widthMhz == rhs.widthMhz && lhs.centerChannel == rhs.centerChannel;
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

Result<std::monostate, std::string> waitForManualReadbackMatch(
    ScannerChannelController& channelController, const ScannerTuning& tuning)
{
    const auto expectedChanspecResult = NexmonChannelProtocol::encodeChanspec(tuning);
    if (expectedChanspecResult.isError()) {
        return Result<std::monostate, std::string>::error(expectedChanspecResult.errorValue());
    }

    const uint32_t expectedChanspec = expectedChanspecResult.value();
    const auto deadline = std::chrono::steady_clock::now() + kManualRetuneReadbackTimeout;
    std::optional<uint32_t> lastActualChanspec;
    std::optional<std::string> lastReadbackError;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto readbackResult = channelController.readbackChanspec();
        if (readbackResult.isValue()) {
            if (readbackResult.value() == expectedChanspec) {
                return Result<std::monostate, std::string>::okay(std::monostate{});
            }

            lastActualChanspec = readbackResult.value();
            lastReadbackError.reset();
        }
        else {
            lastReadbackError = readbackResult.errorValue();
        }

        std::this_thread::sleep_for(kManualRetuneReadbackPollInterval);
    }

    std::string error = "Manual tuning readback did not settle to "
        + NexmonChannelProtocol::describeChanspec(expectedChanspec);
    if (lastActualChanspec.has_value()) {
        error +=
            "; last actual=" + NexmonChannelProtocol::describeChanspec(lastActualChanspec.value());
    }
    else if (lastReadbackError.has_value()) {
        error += "; last readback error=" + lastReadbackError.value();
    }

    return Result<std::monostate, std::string>::error(error);
}

void drainPendingPackets(int fd, std::array<uint8_t, 8192>& buffer)
{
    const auto deadline = std::chrono::steady_clock::now() + kManualRetuneDrainWindow;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const int timeoutMs = std::max(0, static_cast<int>(remaining.count()));

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;

        const int pollResult = ::poll(&pfd, 1, timeoutMs);
        if (pollResult <= 0 || (pfd.revents & POLLIN) == 0) {
            return;
        }

        const ssize_t received = ::recvfrom(fd, buffer.data(), buffer.size(), 0, nullptr, nullptr);
        if (received <= 0) {
            return;
        }
    }
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
    std::string receiverMac;
    std::string ssid;
    std::optional<int> signalDbm;
    std::optional<int> channel;
    std::optional<int> radiotapChannel;
    std::optional<int> radiotapFreqMhz;
    ParsedChannelSource channelSource = ParsedChannelSource::Hint;
    ParsedManagementSubtype subtype = ParsedManagementSubtype::Beacon;
};

struct ParsedProbeRequest {
    std::string receiverMac;
    std::string transmitterMac;
    std::string ssid;
    std::optional<int> signalDbm;
    std::optional<int> radiotapChannel;
    std::optional<int> radiotapFreqMhz;
};

std::string parsedChannelSourceLabel(const ParsedChannelSource source)
{
    switch (source) {
        case ParsedChannelSource::DsParameterSet:
            return "DS";
        case ParsedChannelSource::HtOperation:
            return "HT";
        case ParsedChannelSource::Radiotap:
            return "Radiotap";
        case ParsedChannelSource::Hint:
            return "Hint";
    }

    return "Unknown";
}

std::string parsedManagementSubtypeLabel(const ParsedManagementSubtype subtype)
{
    switch (subtype) {
        case ParsedManagementSubtype::Beacon:
            return "Beacon";
        case ParsedManagementSubtype::ProbeRequest:
            return "ProbeRequest";
        case ParsedManagementSubtype::ProbeResponse:
            return "ProbeResponse";
    }

    return "Unknown";
}

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

    // Management header: addr1 at offset 4 and addr3 at offset 16.
    const uint8_t* receiverMac = frame + 4;
    const uint8_t* bssidMac = frame + 16;

    std::string ssid;
    std::optional<int> channel;
    std::optional<int> radiotapChannel;

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

    if (rt.channelFreqMhz.has_value()) {
        radiotapChannel = frequencyToChannel(*rt.channelFreqMhz);
    }

    ParsedChannelSource channelSource = ParsedChannelSource::Hint;
    if (dsChannel.has_value()) {
        channel = dsChannel;
        channelSource = ParsedChannelSource::DsParameterSet;
    }
    else if (htPrimaryChannel.has_value()) {
        channel = htPrimaryChannel;
        channelSource = ParsedChannelSource::HtOperation;
    }
    else if (radiotapChannel.has_value()) {
        channel = radiotapChannel;
        channelSource = ParsedChannelSource::Radiotap;
    }
    else if (channelHint > 0) {
        channel = channelHint;
        channelSource = ParsedChannelSource::Hint;
    }

    ParsedBeacon parsed;
    parsed.bssid = formatMac(reinterpret_cast<const uint8_t (*)[6]>(bssidMac)[0]);
    parsed.receiverMac = formatMac(reinterpret_cast<const uint8_t (*)[6]>(receiverMac)[0]);
    parsed.ssid = std::move(ssid);
    parsed.signalDbm = rt.signalDbm;
    parsed.channel = channel;
    parsed.radiotapChannel = radiotapChannel;
    parsed.radiotapFreqMhz = rt.channelFreqMhz;
    parsed.channelSource = channelSource;
    parsed.subtype =
        subtype == 5 ? ParsedManagementSubtype::ProbeResponse : ParsedManagementSubtype::Beacon;
    return parsed;
}

std::optional<ParsedProbeRequest> parseProbeRequest(const uint8_t* data, size_t length)
{
    const RadiotapInfo rt = parseRadiotap(data, length);
    if (rt.headerLength == 0 || rt.headerLength > length) {
        return std::nullopt;
    }

    const uint8_t* frame = data + rt.headerLength;
    const size_t frameLength = length - rt.headerLength;
    if (frameLength < 24) {
        return std::nullopt;
    }

    const uint16_t fc = readLe16(frame);
    const int type = (fc >> 2) & 0x3;
    const int subtype = (fc >> 4) & 0xF;
    if (type != 0 || subtype != 4) {
        return std::nullopt;
    }

    ParsedProbeRequest parsed;
    parsed.receiverMac = formatMac(frame + 4);
    parsed.transmitterMac = formatMac(frame + 10);
    parsed.signalDbm = rt.signalDbm;
    parsed.radiotapFreqMhz = rt.channelFreqMhz;
    if (rt.channelFreqMhz.has_value()) {
        parsed.radiotapChannel = frequencyToChannel(*rt.channelFreqMhz);
    }

    size_t offset = 24;
    while (offset + 2 <= frameLength) {
        const uint8_t id = frame[offset];
        const uint8_t len = frame[offset + 1];
        offset += 2;
        if (offset + len > frameLength) {
            break;
        }

        if (id == 0) {
            parsed.ssid = sanitizeSsid(frame + offset, len);
            break;
        }

        offset += len;
    }

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

    localInterfaceMac_ = interfaceMacAddress(config_.interfaceName);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        appliedConfig_ = requestedConfig_;
        currentTuning_.reset();
        lastError_.clear();
        manualReadbackExpectedChanspec_.reset();
        manualReadbackLastSampleAt_.reset();
        manualReadbackLastUnexpectedChanspec_.reset();
        probeRequestLoggedAtByKey_.clear();
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
        appliedConfig_ = requestedConfig_;
        fd = socketFd_;
        socketFd_ = -1;
        currentTuning_.reset();
        lastError_.clear();
        manualReadbackExpectedChanspec_.reset();
        manualReadbackLastSampleAt_.reset();
        manualReadbackLastUnexpectedChanspec_.reset();
        probeRequestLoggedAtByKey_.clear();
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
        snapshot.requestedConfig = requestedConfig_;
        snapshot.appliedConfig = appliedConfig_;
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
        if (!running_ || !currentTuning_.has_value()) {
            appliedConfig_ = config;
        }
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

void ScannerService::resetManualReadbackSampler()
{
    manualReadbackExpectedChanspec_.reset();
    manualReadbackLastSampleAt_.reset();
    manualReadbackLastUnexpectedChanspec_.reset();
}

void ScannerService::maybeSampleManualReadback(
    const ScannerTuning& tuning, const std::chrono::steady_clock::time_point now)
{
    if (!channelController_) {
        return;
    }

    const auto expectedChanspecResult = NexmonChannelProtocol::encodeChanspec(tuning);
    if (expectedChanspecResult.isError()) {
        return;
    }

    const uint32_t expectedChanspec = expectedChanspecResult.value();
    if (!manualReadbackExpectedChanspec_.has_value()
        || manualReadbackExpectedChanspec_.value() != expectedChanspec) {
        manualReadbackExpectedChanspec_ = expectedChanspec;
        manualReadbackLastSampleAt_.reset();
        manualReadbackLastUnexpectedChanspec_.reset();
    }

    if (manualReadbackLastSampleAt_.has_value()
        && now - manualReadbackLastSampleAt_.value() < kManualReadbackSampleInterval) {
        return;
    }
    manualReadbackLastSampleAt_ = now;

    const auto readbackResult = channelController_->readbackChanspec();
    if (readbackResult.isError()) {
        return;
    }

    const uint32_t actualChanspec = readbackResult.value();
    if (actualChanspec == manualReadbackExpectedChanspec_.value()) {
        if (manualReadbackLastUnexpectedChanspec_.has_value()) {
            LOG_INFO(
                Network,
                "Scanner manual tuning readback recovered: requested={}, actual={}",
                NexmonChannelProtocol::describeChanspec(manualReadbackExpectedChanspec_.value()),
                NexmonChannelProtocol::describeChanspec(actualChanspec));
            manualReadbackLastUnexpectedChanspec_.reset();
        }
        return;
    }

    if (manualReadbackLastUnexpectedChanspec_ == actualChanspec) {
        return;
    }

    manualReadbackLastUnexpectedChanspec_ = actualChanspec;
    LOG_INFO(
        Network,
        "Scanner manual tuning readback mismatch: requested={}, actual={}",
        NexmonChannelProtocol::describeChanspec(manualReadbackExpectedChanspec_.value()),
        NexmonChannelProtocol::describeChanspec(actualChanspec));
}

void ScannerService::maybeLogProbeRequest(
    const uint8_t* data,
    size_t length,
    const ScannerTuning& tuning,
    std::chrono::steady_clock::time_point now)
{
    const auto parsed = parseProbeRequest(data, length);
    if (!parsed.has_value() || parsed->transmitterMac.empty() || !parsed->radiotapChannel) {
        return;
    }

    if (scannerObservationKindForObservedChannel(tuning, parsed->radiotapChannel.value())
        != ScannerObservationKind::Incidental) {
        return;
    }

    const std::string logKey = parsed->transmitterMac + "|" + parsed->receiverMac + "|"
        + std::to_string(parsed->radiotapChannel.value());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = probeRequestLoggedAtByKey_.find(logKey);
        if (it != probeRequestLoggedAtByKey_.end()
            && now - it->second < kIncidentalObservationLogCooldown) {
            return;
        }
        probeRequestLoggedAtByKey_[logKey] = now;
    }

    const auto tuningCoveredChannels = scannerTuningCoveredPrimaryChannels(tuning);
    const std::string tuningSpanLabel =
        scannerChannelSpanLabel(tuningCoveredChannels, tuning.widthMhz);
    const std::string ssidLabel = !parsed->ssid.empty() ? parsed->ssid : "<wildcard>";
    const std::string signalLabel =
        parsed->signalDbm.has_value() ? std::to_string(parsed->signalDbm.value()) + " dBm" : "n/a";
    const std::string radiotapFreqLabel = parsed->radiotapFreqMhz.has_value()
        ? std::to_string(parsed->radiotapFreqMhz.value()) + " MHz"
        : "n/a";
    const std::string radiotapChannelLabel = std::to_string(parsed->radiotapChannel.value());
    const std::string receiverRole = receiverRoleLabel(parsed->receiverMac, localInterfaceMac_);
    const std::string transmitterRole =
        transmitterRoleLabel(parsed->transmitterMac, localInterfaceMac_);
    const std::string localMacLabel =
        localInterfaceMac_.has_value() ? localInterfaceMac_.value() : "n/a";

    LOG_INFO(
        Network,
        "Scanner incidental observation: subtype={}, transmitter_mac={}, transmitter_role={}, "
        "receiver_mac={}, receiver_role={}, local_mac={}, ssid={}, radiotap_freq={}, "
        "radiotap_channel={}, tuned_primary={}, tuned_width={} MHz, tuned_span={}, signal={}",
        parsedManagementSubtypeLabel(ParsedManagementSubtype::ProbeRequest),
        parsed->transmitterMac,
        transmitterRole,
        parsed->receiverMac,
        receiverRole,
        localMacLabel,
        ssidLabel,
        radiotapFreqLabel,
        radiotapChannelLabel,
        tuning.primaryChannel,
        tuning.widthMhz,
        tuningSpanLabel,
        signalLabel);
}

std::optional<ScannerService::PacketObservation> ScannerService::handlePacket(
    const uint8_t* data,
    size_t length,
    const ScannerTuning& tuning,
    std::chrono::steady_clock::time_point now)
{
    maybeLogProbeRequest(data, length, tuning, now);

    const auto parsed = parseBeaconOrProbeResponse(data, length, tuning.primaryChannel);
    if (!parsed.has_value() || parsed->bssid.empty()) {
        return std::nullopt;
    }

    std::optional<std::string> incidentalObservationLog;
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
        if (state.observationKind == ScannerObservationKind::Incidental) {
            const bool shouldLog = !state.lastIncidentalLoggedAt.has_value()
                || now - state.lastIncidentalLoggedAt.value() >= kIncidentalObservationLogCooldown
                || state.lastIncidentalLoggedChannel != parsed->channel;
            if (shouldLog) {
                const auto tuningCoveredChannels = scannerTuningCoveredPrimaryChannels(tuning);
                const std::string tuningSpanLabel =
                    scannerChannelSpanLabel(tuningCoveredChannels, tuning.widthMhz);
                const std::string ssidLabel = !parsed->ssid.empty() ? parsed->ssid : "<hidden>";
                const std::string signalLabel = parsed->signalDbm.has_value()
                    ? std::to_string(parsed->signalDbm.value()) + " dBm"
                    : "n/a";
                const std::string radiotapFreqLabel = parsed->radiotapFreqMhz.has_value()
                    ? std::to_string(parsed->radiotapFreqMhz.value()) + " MHz"
                    : "n/a";
                const std::string radiotapChannelLabel = parsed->radiotapChannel.has_value()
                    ? std::to_string(parsed->radiotapChannel.value())
                    : "n/a";
                const std::string receiverRole =
                    receiverRoleLabel(parsed->receiverMac, localInterfaceMac_);
                const std::string localMacLabel =
                    localInterfaceMac_.has_value() ? localInterfaceMac_.value() : "n/a";

                incidentalObservationLog = "Scanner incidental observation: subtype="
                    + parsedManagementSubtypeLabel(parsed->subtype) + ", bssid=" + parsed->bssid
                    + ", receiver_mac=" + parsed->receiverMac + ", receiver_role=" + receiverRole
                    + ", local_mac=" + localMacLabel + ", ssid=" + ssidLabel
                    + ", parsed_channel=" + std::to_string(parsed->channel.value()) + ", source="
                    + parsedChannelSourceLabel(parsed->channelSource) + ", radiotap_freq="
                    + radiotapFreqLabel + ", radiotap_channel=" + radiotapChannelLabel
                    + ", tuned_primary=" + std::to_string(tuning.primaryChannel)
                    + ", tuned_width=" + std::to_string(tuning.widthMhz) + " MHz"
                    + ", tuned_span=" + tuningSpanLabel + ", signal=" + signalLabel;
                state.lastIncidentalLoggedAt = now;
                state.lastIncidentalLoggedChannel = parsed->channel;
            }
        }
    }
    else {
        state.observationKind = ScannerObservationKind::Direct;
    }
    state.lastSeenAt = now;

    if (incidentalObservationLog.has_value()) {
        LOG_INFO(Network, "{}", incidentalObservationLog.value());
    }

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
        std::optional<ScannerConfig> stepConfig;
        bool manualMode = false;
        if (probe) {
            resetManualReadbackSampler();
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
            stepConfig = configCopy;

            if (configCopy.mode == ScannerConfigMode::Manual) {
                manualMode = true;
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
                resetManualReadbackSampler();
                planner_->setAutoConfig(configCopy.autoConfig);
                step = planner_->nextStep(std::chrono::steady_clock::now());
            }
        }

        std::optional<ScannerTuning> previousTuning;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previousTuning = currentTuning_;
        }

        const bool tuningChanged = !previousTuning.has_value()
            || !scannerTuningsEqual(previousTuning.value(), step.tuning);
        if (tuningChanged) {
            const auto setChannelResult = setChannel(step.tuning);
            if (setChannelResult.isError()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    currentTuning_.reset();
                    lastError_ = "Failed to set channel "
                        + std::to_string(step.tuning.primaryChannel) + ": "
                        + setChannelResult.errorValue();
                }
                if (probe) {
                    completeProbe(
                        probe,
                        Result<ProbeResult, std::string>::error(
                            "Failed to set probe tuning "
                            + std::to_string(step.tuning.primaryChannel) + ": "
                            + setChannelResult.errorValue()));
                }
                notifySnapshotChanged();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (manualMode) {
                const auto settleResult =
                    waitForManualReadbackMatch(*channelController_, step.tuning);
                if (settleResult.isError()) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        currentTuning_.reset();
                        lastError_ = settleResult.errorValue();
                    }
                    notifySnapshotChanged();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                resetManualReadbackSampler();

                int fd = -1;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    fd = socketFd_;
                }
                if (fd >= 0) {
                    drainPendingPackets(fd, buffer);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stepConfig.has_value()) {
                appliedConfig_ = stepConfig.value();
            }
            currentTuning_ = step.tuning;
            lastError_.clear();
        }
        if (manualMode && tuningChanged) {
            notifySnapshotChanged();
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
            if (manualMode) {
                maybeSampleManualReadback(step.tuning, std::chrono::steady_clock::now());
            }

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
