#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace ScannerProbeRun {

DEFINE_API_NAME(ScannerProbeRun);

struct ProbeDwellInfo {
    bool sawTraffic = false;
    uint32_t radiosSeen = 0;
    uint32_t newRadiosSeen = 0;
    std::optional<int> strongestSignalDbm;
    std::vector<int> observedChannels;

    using serialize = zpp::bits::members<5>;
};

inline void to_json(nlohmann::json& j, const ProbeDwellInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, ProbeDwellInfo& info)
{
    info = ReflectSerializer::from_json<ProbeDwellInfo>(j);
}

struct Okay;

struct Command {
    OsManager::ScannerTuning tuning;
    int dwellMs = 250;
    uint32_t sampleCount = 10;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    OsManager::ScannerTuning tuning;
    int dwellMs = 0;
    std::vector<ProbeDwellInfo> dwells;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<3>;
};

API_STANDARD_TYPES();

} // namespace ScannerProbeRun
} // namespace OsApi
} // namespace DirtSim
