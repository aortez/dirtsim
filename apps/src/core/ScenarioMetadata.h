#pragma once

#include "core/ScenarioId.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {

enum class ScenarioKind : uint8_t {
    GridWorld = 0,
    NesWorld,
};

struct ScenarioMetadata {
    Scenario::EnumType id = Scenario::EnumType::Empty;
    ScenarioKind kind = ScenarioKind::GridWorld;
    std::string name;
    std::string description;
    std::string category;
    uint32_t requiredWidth = 0;
    uint32_t requiredHeight = 0;

    using serialize = zpp::bits::members<7>;
};

void to_json(nlohmann::json& j, const ScenarioMetadata& meta);
void from_json(const nlohmann::json& j, ScenarioMetadata& meta);

} // namespace DirtSim
