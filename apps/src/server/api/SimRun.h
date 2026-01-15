#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"
#include "core/Vector2.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace SimRun {

DEFINE_API_NAME(SimRun);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    double timestep = 0.016;
    int max_steps = -1;
    int max_frame_ms = 0;
    std::optional<Scenario::EnumType>
        scenario_id;           // Optional scenario (nullopt = use server config default).
    bool start_paused = false; // Load scenario but don't start advancing.
    Vector2s container_size;   // UI container size in pixels (0,0 = use defaults).

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<6>;
};

struct Okay {
    bool running;
    uint32_t current_step;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SimRun
} // namespace Api
} // namespace DirtSim
