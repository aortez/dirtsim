#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace SimStop {

DEFINE_API_NAME(SimStop);

/**
 * @brief Command to stop the simulation and return to Idle state.
 *
 * When received in the SimRunning state, the server will:
 * - Destroy the current World
 * - Transition back to Idle state
 * - Respond with success
 */
struct Command {
    API_COMMAND_T(std::monostate);
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SimStop
} // namespace Api
} // namespace DirtSim
