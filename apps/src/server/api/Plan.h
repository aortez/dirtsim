#pragma once

#include "ApiError.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/UUID.h"
#include "core/input/PlayerControlFrame.h"
#include <nlohmann/json_fwd.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

struct PlanSummary {
    UUID id{};
    uint64_t bestFrontier = 0;
    uint64_t elapsedFrames = 0;

    using serialize = zpp::bits::members<3>;
};

struct Plan {
    PlanSummary summary;
    std::vector<PlayerControlFrame> frames;

    static constexpr const char* name() { return "Plan"; }
    using serialize = zpp::bits::members<2>;

    using OkayType = std::monostate;
    using Response = Result<OkayType, ApiError>;
    using Cwc = CommandWithCallback<Plan, Response>;
};

void to_json(nlohmann::json& j, const PlanSummary& value);
void from_json(const nlohmann::json& j, PlanSummary& value);
void to_json(nlohmann::json& j, const Plan& value);
void from_json(const nlohmann::json& j, Plan& value);

} // namespace Api
} // namespace DirtSim
