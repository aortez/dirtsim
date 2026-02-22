#pragma once

/**
 * \file
 * Type-safe scenario identifier enum.
 * Each value corresponds to a scenario config type in ScenarioConfig variant.
 */

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace DirtSim::Scenario {

enum class EnumType : uint8_t {
    Benchmark = 0,
    Clock,
    DamBreak,
    Empty,
    GooseTest,
    Lights,
    Raining,
    Sandbox,
    TreeGermination,
    WaterEqualization,
    Nes,
};

std::string toString(EnumType id);

std::optional<EnumType> fromString(const std::string& str);

void to_json(nlohmann::json& j, const EnumType& id);
void from_json(const nlohmann::json& j, EnumType& id);

} // namespace DirtSim::Scenario
