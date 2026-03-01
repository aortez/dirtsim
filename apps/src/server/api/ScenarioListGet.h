#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioMetadata.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace ScenarioListGet {

DEFINE_API_NAME(ScenarioListGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

/**
 * @brief Command to get list of available scenarios.
 *
 * No parameters required.
 */
struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    // zpp_bits serialization (command has no fields).
    using serialize = zpp::bits::members<0>;
};

/**
 * @brief Response containing list of available scenarios.
 */
struct Okay {
    std::vector<ScenarioMetadata> scenarios;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    // zpp_bits serialization.
    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace ScenarioListGet
} // namespace Api
} // namespace DirtSim
