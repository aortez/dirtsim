#pragma once

/**
 * \file
 * Type-safe scenario identifier enum.
 * Each value corresponds to a scenario config type in ScenarioConfig variant.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace DirtSim {

enum class ScenarioId : uint8_t {
    Benchmark = 0,
    Clock,
    DamBreak,
    Empty,
    FallingDirt,
    GooseTest,
    Raining,
    Sandbox,
    TreeGermination,
    WaterEqualization,
};

std::string toString(ScenarioId id);

std::optional<ScenarioId> fromString(std::string_view str);

} // namespace DirtSim
