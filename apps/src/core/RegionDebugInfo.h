#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {

enum class RegionState : uint8_t {
    Awake,
    LoadedQuiet,
    Sleeping,
};

enum class WakeReason : uint8_t {
    None,
    ExternalMutation,
    Move,
    BlockedTransfer,
    NeighborTopologyChanged,
    GravityChanged,
    WaterInterface,
};

struct RegionDebugInfo {
    uint8_t state = static_cast<uint8_t>(RegionState::Awake);
    uint8_t wake_reason = static_cast<uint8_t>(WakeReason::None);

    using serialize = zpp::bits::members<2>;
};

inline void to_json(nlohmann::json& j, const RegionDebugInfo& info)
{
    j = nlohmann::json{ { "state", info.state }, { "wake_reason", info.wake_reason } };
}

inline void from_json(const nlohmann::json& j, RegionDebugInfo& info)
{
    j.at("state").get_to(info.state);
    j.at("wake_reason").get_to(info.wake_reason);
}

} // namespace DirtSim
