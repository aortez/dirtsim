#include "NexmonChannelProtocol.h"
#include <algorithm>
#include <array>

namespace DirtSim {
namespace OsManager {
namespace NexmonChannelProtocol {

namespace {

constexpr std::array<uint8_t, 8> kMagicPrefix = { 'N', 'E', 'X', 0, 0, 0, 0, 0 };
constexpr std::array<uint8_t, 9> kChanspecName = { 'c', 'h', 'a', 'n', 's', 'p', 'e', 'c', 0 };
constexpr uint32_t kGetChanspecCommandId = 0x00000106;
constexpr uint32_t kSetChanspecCommandId = 0x00000107;
constexpr uint32_t kBand24Ghz = 0x0000u;
constexpr uint32_t kBand5Ghz = 0xc000u;
constexpr uint32_t kControlSidebandLower = 0x0000u;
constexpr uint32_t kControlSidebandLowerUpper = 0x0100u;
constexpr uint32_t kControlSidebandUpperLower = 0x0200u;
constexpr uint32_t kControlSidebandUpperUpper = 0x0300u;
constexpr uint32_t kWidth20Mhz = 0x1000u;
constexpr uint32_t kWidth40Mhz = 0x1800u;
constexpr uint32_t kWidth80Mhz = 0x2000u;
constexpr size_t kGetPayloadSize = 28;
constexpr size_t kSetPayloadSize = 32;
constexpr size_t kChanspecValueOffset = 16;

void writeLe32(std::vector<uint8_t>& payload, size_t offset, uint32_t value)
{
    payload[offset + 0] = static_cast<uint8_t>(value & 0xff);
    payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
    payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
    payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

uint32_t readLe32(std::span<const uint8_t> payload, size_t offset)
{
    return static_cast<uint32_t>(payload[offset + 0])
        | (static_cast<uint32_t>(payload[offset + 1]) << 8)
        | (static_cast<uint32_t>(payload[offset + 2]) << 16)
        | (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

void writeCommonPayloadPrefix(std::vector<uint8_t>& payload, uint32_t commandId, uint32_t flag)
{
    std::copy(kMagicPrefix.begin(), kMagicPrefix.end(), payload.begin());
    writeLe32(payload, 8, commandId);
    writeLe32(payload, 12, flag);
}

bool isSupported5GHzScannerChannel(int channel)
{
    return scannerChannelListContains(scannerBandPrimaryChannels(ScannerBand::Band5Ghz), channel);
}

bool isSupported5GHzScannerChannelForWidth(const int widthMhz, const int channel)
{
    if (widthMhz == 20) {
        return isSupported5GHzScannerChannel(channel);
    }

    for (const int centerChannel : scannerManualTargetChannels(ScannerBand::Band5Ghz, widthMhz)) {
        const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
            ScannerTuning{
                .band = ScannerBand::Band5Ghz,
                .primaryChannel = centerChannel,
                .widthMhz = widthMhz,
                .centerChannel = centerChannel,
            });
        if (scannerChannelListContains(coveredChannels, channel)) {
            return true;
        }
    }

    return false;
}

Result<uint32_t, std::string> encode5GHz40MHzChanspec(const ScannerTuning& tuning)
{
    if (!isSupported5GHzScannerChannelForWidth(40, tuning.primaryChannel)) {
        return Result<uint32_t, std::string>::error(
            "Unsupported 40 MHz scanner channel " + std::to_string(tuning.primaryChannel));
    }

    if (!tuning.centerChannel.has_value()) {
        return Result<uint32_t, std::string>::error(
            "Missing 40 MHz center channel for primary channel "
            + std::to_string(tuning.primaryChannel));
    }

    const int centerChannel = tuning.centerChannel.value();
    if (!scannerChannelListContains(
            scannerManualTargetChannels(ScannerBand::Band5Ghz, 40), centerChannel)) {
        return Result<uint32_t, std::string>::error(
            "Unsupported 40 MHz center channel " + std::to_string(centerChannel));
    }

    const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
        ScannerTuning{
            .band = ScannerBand::Band5Ghz,
            .primaryChannel = tuning.primaryChannel,
            .widthMhz = 40,
            .centerChannel = centerChannel,
        });
    if (!scannerChannelListContains(coveredChannels, tuning.primaryChannel)) {
        return Result<uint32_t, std::string>::error(
            "Invalid 40 MHz center channel " + std::to_string(centerChannel)
            + " for primary channel " + std::to_string(tuning.primaryChannel));
    }

    switch (tuning.primaryChannel - centerChannel) {
        case -2:
            return Result<uint32_t, std::string>::okay(
                kBand5Ghz | kWidth40Mhz | kControlSidebandLower
                | static_cast<uint32_t>(centerChannel));
        case 2:
            return Result<uint32_t, std::string>::okay(
                kBand5Ghz | kWidth40Mhz | kControlSidebandLowerUpper
                | static_cast<uint32_t>(centerChannel));
        default:
            return Result<uint32_t, std::string>::error(
                "Invalid 40 MHz center channel " + std::to_string(centerChannel)
                + " for primary channel " + std::to_string(tuning.primaryChannel));
    }
}

Result<uint32_t, std::string> encode5GHz80MHzChanspec(const ScannerTuning& tuning)
{
    if (!isSupported5GHzScannerChannelForWidth(80, tuning.primaryChannel)) {
        return Result<uint32_t, std::string>::error(
            "Unsupported 80 MHz scanner channel " + std::to_string(tuning.primaryChannel));
    }

    if (!tuning.centerChannel.has_value()) {
        return Result<uint32_t, std::string>::error(
            "Missing 80 MHz center channel for primary channel "
            + std::to_string(tuning.primaryChannel));
    }

    const int centerChannel = tuning.centerChannel.value();
    if (!scannerChannelListContains(
            scannerManualTargetChannels(ScannerBand::Band5Ghz, 80), centerChannel)) {
        return Result<uint32_t, std::string>::error(
            "Unsupported 80 MHz center channel " + std::to_string(centerChannel));
    }

    const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
        ScannerTuning{
            .band = ScannerBand::Band5Ghz,
            .primaryChannel = tuning.primaryChannel,
            .widthMhz = 80,
            .centerChannel = centerChannel,
        });
    if (!scannerChannelListContains(coveredChannels, tuning.primaryChannel)) {
        return Result<uint32_t, std::string>::error(
            "Invalid 80 MHz center channel " + std::to_string(centerChannel)
            + " for primary channel " + std::to_string(tuning.primaryChannel));
    }

    switch (tuning.primaryChannel - centerChannel) {
        case -6:
            return Result<uint32_t, std::string>::okay(
                kBand5Ghz | kWidth80Mhz | kControlSidebandLower
                | static_cast<uint32_t>(centerChannel));
        case -2:
            return Result<uint32_t, std::string>::okay(
                kBand5Ghz | kWidth80Mhz | kControlSidebandLowerUpper
                | static_cast<uint32_t>(centerChannel));
        case 2:
            return Result<uint32_t, std::string>::okay(
                kBand5Ghz | kWidth80Mhz | kControlSidebandUpperLower
                | static_cast<uint32_t>(centerChannel));
        case 6:
            return Result<uint32_t, std::string>::okay(
                kBand5Ghz | kWidth80Mhz | kControlSidebandUpperUpper
                | static_cast<uint32_t>(centerChannel));
        default:
            return Result<uint32_t, std::string>::error(
                "Invalid 80 MHz center channel " + std::to_string(centerChannel)
                + " for primary channel " + std::to_string(tuning.primaryChannel));
    }
}

} // namespace

Result<uint32_t, std::string> encodeChanspec(const ScannerTuning& tuning)
{
    if (!scannerWidthSupported(tuning.band, tuning.widthMhz)) {
        return Result<uint32_t, std::string>::error(
            "Unsupported " + scannerBandLabel(tuning.band) + " scanner width "
            + std::to_string(tuning.widthMhz) + " MHz");
    }

    switch (tuning.widthMhz) {
        case 20:
            return encodeChanspec20MHz(tuning.primaryChannel);
        case 40:
            if (tuning.band != ScannerBand::Band5Ghz) {
                break;
            }
            return encode5GHz40MHzChanspec(tuning);
        case 80:
            if (tuning.band != ScannerBand::Band5Ghz) {
                break;
            }
            return encode5GHz80MHzChanspec(tuning);
    }

    return Result<uint32_t, std::string>::error(
        "Unsupported scanner tuning width " + std::to_string(tuning.widthMhz) + " MHz");
}

Result<uint32_t, std::string> encodeChanspec20MHz(int channel)
{
    if (channel >= 1 && channel <= 11) {
        return Result<uint32_t, std::string>::okay(
            kBand24Ghz | kWidth20Mhz | static_cast<uint32_t>(channel));
    }

    if (isSupported5GHzScannerChannel(channel)) {
        return Result<uint32_t, std::string>::okay(
            kBand5Ghz | kWidth20Mhz | static_cast<uint32_t>(channel));
    }

    return Result<uint32_t, std::string>::error(
        "Unsupported 20 MHz scanner channel " + std::to_string(channel));
}

std::vector<uint8_t> buildGetChanspecPayload()
{
    std::vector<uint8_t> payload(kGetPayloadSize, 0);
    writeCommonPayloadPrefix(payload, kGetChanspecCommandId, 0);
    std::copy(kChanspecName.begin(), kChanspecName.end(), payload.begin() + 16);
    return payload;
}

Result<std::vector<uint8_t>, std::string> buildSetChanspecPayload(const ScannerTuning& tuning)
{
    const auto chanspecResult = encodeChanspec(tuning);
    if (chanspecResult.isError()) {
        return Result<std::vector<uint8_t>, std::string>::error(chanspecResult.errorValue());
    }

    std::vector<uint8_t> payload(kSetPayloadSize, 0);
    writeCommonPayloadPrefix(payload, kSetChanspecCommandId, 1);
    std::copy(kChanspecName.begin(), kChanspecName.end(), payload.begin() + 16);
    writeLe32(payload, 25, chanspecResult.value());
    return Result<std::vector<uint8_t>, std::string>::okay(std::move(payload));
}

Result<uint32_t, std::string> parseGetChanspecPayload(std::span<const uint8_t> payload)
{
    if (payload.size() < kGetPayloadSize) {
        return Result<uint32_t, std::string>::error("Nexmon get-chanspec reply is too short");
    }

    if (!std::equal(kMagicPrefix.begin(), kMagicPrefix.end(), payload.begin())) {
        return Result<uint32_t, std::string>::error("Nexmon get-chanspec reply prefix mismatch");
    }

    if (readLe32(payload, 8) != kGetChanspecCommandId) {
        return Result<uint32_t, std::string>::error("Nexmon get-chanspec reply command mismatch");
    }

    return Result<uint32_t, std::string>::okay(readLe32(payload, kChanspecValueOffset));
}

} // namespace NexmonChannelProtocol
} // namespace OsManager
} // namespace DirtSim
