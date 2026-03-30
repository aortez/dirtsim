#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace PlanPlaybackPauseSet {

DEFINE_API_NAME(PlanPlaybackPauseSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    bool paused = false;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool paused = false;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace PlanPlaybackPauseSet
} // namespace Api
} // namespace DirtSim
