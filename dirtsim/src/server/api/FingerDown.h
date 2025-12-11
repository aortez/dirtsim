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

namespace FingerDown {

DEFINE_API_NAME(FingerDown);

/**
 * @brief Command to start a finger interaction session.
 *
 * Initiates a finger "touch" at the specified world coordinates.
 * The server tracks this finger session until a corresponding FingerUp.
 * Subsequent FingerMove commands calculate force based on delta from last position.
 */
struct Command {
    uint32_t finger_id;  // Client-assigned finger ID (for multi-touch support).
    double world_x;      // World coordinate X (cell units, fractional).
    double world_y;      // World coordinate Y (cell units, fractional).
    double radius;       // Radius of influence in cell units.

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<4>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace FingerDown
} // namespace Api
} // namespace DirtSim
