#pragma once

#include "Plan.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

struct PlanSaved {
    PlanSummary summary;

    static constexpr const char* name() { return "PlanSaved"; }
    using serialize = zpp::bits::members<1>;
};

void to_json(nlohmann::json& j, const PlanSaved& value);
void from_json(const nlohmann::json& j, PlanSaved& value);

} // namespace Api
} // namespace DirtSim
