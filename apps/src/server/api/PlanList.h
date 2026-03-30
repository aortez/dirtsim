#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "Plan.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace PlanList {

DEFINE_API_NAME(PlanList);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
};

struct Entry {
    PlanSummary summary;

    using serialize = zpp::bits::members<1>;
};

void to_json(nlohmann::json& j, const Entry& value);
void from_json(const nlohmann::json& j, Entry& value);

struct Okay {
    std::vector<Entry> plans;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace PlanList
} // namespace Api
} // namespace DirtSim
