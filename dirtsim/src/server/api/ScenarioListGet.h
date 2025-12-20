#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <cstdint>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace ScenarioListGet {

DEFINE_API_NAME(ScenarioListGet);

/**
 * @brief Command to get list of available scenarios.
 *
 * No parameters required.
 */
struct Command {
    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    // zpp_bits serialization (command has no fields).
    using serialize = zpp::bits::members<0>;
};

/**
 * @brief Info about a single scenario.
 */
struct ScenarioInfo {
    std::string id;          // Scenario ID (e.g., "sandbox", "dam_break").
    std::string name;        // Display name (e.g., "Sandbox", "Dam Break").
    std::string description; // Tooltip/help text.
    std::string category;    // Category (sandbox, demo, organisms, benchmark).

    // zpp_bits serialization.
    using serialize = zpp::bits::members<4>;
};

/**
 * @brief Response containing list of available scenarios.
 */
struct Okay {
    std::vector<ScenarioInfo> scenarios;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    // zpp_bits serialization.
    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace ScenarioListGet
} // namespace Api
} // namespace DirtSim
