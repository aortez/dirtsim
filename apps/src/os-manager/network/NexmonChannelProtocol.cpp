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
    switch (channel) {
        case 36:
        case 40:
        case 44:
        case 48:
        case 149:
        case 153:
        case 157:
        case 161:
        case 165:
            return true;
        default:
            return false;
    }
}

Result<uint32_t, std::string> encode5GHz40MHzChanspec(const ScannerTuning& tuning)
{
    struct Case {
        int primaryChannel;
        int centerChannel;
        uint32_t sideband;
    };
    for (const auto& candidate : std::array<Case, 8>{ {
             { 36, 38, kControlSidebandLower },
             { 40, 38, kControlSidebandLowerUpper },
             { 44, 46, kControlSidebandLower },
             { 48, 46, kControlSidebandLowerUpper },
             { 149, 151, kControlSidebandLower },
             { 153, 151, kControlSidebandLowerUpper },
             { 157, 159, kControlSidebandLower },
             { 161, 159, kControlSidebandLowerUpper },
         } }) {
        if (candidate.primaryChannel != tuning.primaryChannel) {
            continue;
        }

        if (tuning.centerChannel.has_value()
            && tuning.centerChannel.value() != candidate.centerChannel) {
            return Result<uint32_t, std::string>::error(
                "Invalid 40 MHz center channel " + std::to_string(tuning.centerChannel.value())
                + " for primary channel " + std::to_string(tuning.primaryChannel));
        }

        return Result<uint32_t, std::string>::okay(
            kBand5Ghz | kWidth40Mhz | candidate.sideband
            | static_cast<uint32_t>(candidate.centerChannel));
    }

    return Result<uint32_t, std::string>::error(
        "Unsupported 40 MHz scanner channel " + std::to_string(tuning.primaryChannel));
}

Result<uint32_t, std::string> encode5GHz80MHzChanspec(const ScannerTuning& tuning)
{
    struct Case {
        int primaryChannel;
        int centerChannel;
        uint32_t sideband;
    };
    for (const auto& candidate : std::array<Case, 8>{ {
             { 36, 42, kControlSidebandLower },
             { 40, 42, kControlSidebandLowerUpper },
             { 44, 42, kControlSidebandUpperLower },
             { 48, 42, kControlSidebandUpperUpper },
             { 149, 155, kControlSidebandLower },
             { 153, 155, kControlSidebandLowerUpper },
             { 157, 155, kControlSidebandUpperLower },
             { 161, 155, kControlSidebandUpperUpper },
         } }) {
        if (candidate.primaryChannel != tuning.primaryChannel) {
            continue;
        }

        if (tuning.centerChannel.has_value()
            && tuning.centerChannel.value() != candidate.centerChannel) {
            return Result<uint32_t, std::string>::error(
                "Invalid 80 MHz center channel " + std::to_string(tuning.centerChannel.value())
                + " for primary channel " + std::to_string(tuning.primaryChannel));
        }

        return Result<uint32_t, std::string>::okay(
            kBand5Ghz | kWidth80Mhz | candidate.sideband
            | static_cast<uint32_t>(candidate.centerChannel));
    }

    return Result<uint32_t, std::string>::error(
        "Unsupported 80 MHz scanner channel " + std::to_string(tuning.primaryChannel));
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
