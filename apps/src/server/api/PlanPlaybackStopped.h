#pragma once

#include "core/UUID.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

enum class PlanPlaybackStopReason : uint8_t {
    Stopped = 0,
    Completed = 1,
};

struct PlanPlaybackStopped {
    UUID planId{};
    PlanPlaybackStopReason reason = PlanPlaybackStopReason::Stopped;

    static constexpr const char* name() { return "PlanPlaybackStopped"; }
    using serialize = zpp::bits::members<2>;
};

void to_json(nlohmann::json& j, const PlanPlaybackStopped& value);
void from_json(const nlohmann::json& j, PlanPlaybackStopped& value);

} // namespace Api
} // namespace DirtSim
