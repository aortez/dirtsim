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

} // namespace

Result<uint32_t, std::string> encodeChanspec20MHz(int channel)
{
    if (channel >= 1 && channel <= 11) {
        return Result<uint32_t, std::string>::okay(0x1000u | static_cast<uint32_t>(channel));
    }

    if (isSupported5GHzScannerChannel(channel)) {
        return Result<uint32_t, std::string>::okay(0xd000u | static_cast<uint32_t>(channel));
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

Result<std::vector<uint8_t>, std::string> buildSetChanspecPayload(int channel)
{
    const auto chanspecResult = encodeChanspec20MHz(channel);
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
