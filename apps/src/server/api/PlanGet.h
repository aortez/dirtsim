#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "Plan.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/UUID.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace PlanGet {

DEFINE_API_NAME(PlanGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    UUID planId{};

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    Plan plan;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace PlanGet
} // namespace Api
} // namespace DirtSim
