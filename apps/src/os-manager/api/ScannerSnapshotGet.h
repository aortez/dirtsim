#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace ScannerSnapshotGet {

DEFINE_API_NAME(ScannerSnapshotGet);

struct ObservedRadioInfo {
    std::string bssid;
    std::string ssid;
    std::optional<int> signalDbm;
    std::optional<int> channel;
    std::optional<uint64_t> lastSeenAgeMs;
    OsManager::ScannerObservationKind observationKind = OsManager::ScannerObservationKind::Direct;

    using serialize = zpp::bits::members<6>;
};

inline void to_json(nlohmann::json& j, const ObservedRadioInfo& info)
{
    j = ReflectSerializer::to_json(info);
}

inline void from_json(const nlohmann::json& j, ObservedRadioInfo& info)
{
    info = ReflectSerializer::from_json<ObservedRadioInfo>(j);
}

struct Okay;

struct Command {
    std::optional<uint32_t> maxRadios;
    std::optional<uint64_t> maxAgeMs;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool active = false;
    OsManager::ScannerConfig requestedConfig = OsManager::scannerDefaultConfig();
    OsManager::ScannerConfig appliedConfig = OsManager::scannerDefaultConfig();
    std::optional<OsManager::ScannerTuning> currentTuning;
    std::string detail;
    std::vector<ObservedRadioInfo> radios;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<6>;
};

API_STANDARD_TYPES();

} // namespace ScannerSnapshotGet
} // namespace OsApi
} // namespace DirtSim
