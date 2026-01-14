#pragma once

/**
 * \file
 * Type-safe scenario identifier enum.
 * Each value corresponds to a scenario config type in ScenarioConfig variant.
 */

#include <cstdint>
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
};

std::string toString(EnumType id);

std::optional<EnumType> fromString(const std::string& str);

} // namespace DirtSim::Scenario
