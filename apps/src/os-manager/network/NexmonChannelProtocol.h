#pragma once

#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace DirtSim {
namespace OsManager {
namespace NexmonChannelProtocol {

Result<uint32_t, std::string> encodeChanspec(const ScannerTuning& tuning);
Result<uint32_t, std::string> encodeChanspec20MHz(int channel);
std::vector<uint8_t> buildGetChanspecPayload();
Result<std::vector<uint8_t>, std::string> buildSetChanspecPayload(const ScannerTuning& tuning);
Result<uint32_t, std::string> parseGetChanspecPayload(std::span<const uint8_t> payload);

} // namespace NexmonChannelProtocol
} // namespace OsManager
} // namespace DirtSim
