#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace FingerMove {

DEFINE_API_NAME(FingerMove);

/**
 * @brief Command to update finger position during a drag.
 *
 * Reports the new position of an active finger. The server calculates
 * the delta from the previous position and applies forces to cells
 * within the finger's radius, pushing them in the direction of movement.
 */
struct Command {
    API_COMMAND_T(std::monostate);
    uint32_t finger_id; // Must match a previous FingerDown finger_id.
    double world_x;     // New world coordinate X (cell units, fractional).
    double world_y;     // New world coordinate Y (cell units, fractional).

    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace FingerMove
} // namespace Api
} // namespace DirtSim
