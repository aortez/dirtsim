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

namespace FingerUp {

DEFINE_API_NAME(FingerUp);

/**
 * @brief Command to end a finger interaction session.
 *
 * Signals that the finger has been lifted. The server cleans up
 * the finger session tracking state for this finger_id.
 */
struct Command {
    uint32_t finger_id;  // Must match a previous FingerDown finger_id.

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace FingerUp
} // namespace Api
} // namespace DirtSim
