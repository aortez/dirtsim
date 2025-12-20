#pragma once

#include "ReflectSerializer.h"
#include "server/scenarios/scenarios/BenchmarkConfig.h"
#include "server/scenarios/scenarios/ClockConfig.h"
#include "server/scenarios/scenarios/DamBreakConfig.h"
#include "server/scenarios/scenarios/EmptyConfig.h"
#include "server/scenarios/scenarios/FallingDirtConfig.h"
#include "server/scenarios/scenarios/RainingConfig.h"
#include "server/scenarios/scenarios/SandboxConfig.h"
#include "server/scenarios/scenarios/WaterEqualizationConfig.h"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace DirtSim {

/**
 * @brief Variant type containing all scenario configurations.
 *
 * Each config struct is defined in its own header file alongside its scenario.
 * Use std::visit() or std::get<>() to access the active config.
 */
using ScenarioConfig = std::variant<
    EmptyConfig,
    SandboxConfig,
    DamBreakConfig,
    RainingConfig,
    WaterEqualizationConfig,
    FallingDirtConfig,
    BenchmarkConfig,
    ClockConfig>;

/**
 * @brief Get scenario ID string from config variant.
 * @param config The scenario config variant.
 * @return Scenario ID string (e.g., "sandbox", "dam_break").
 */
inline std::string getScenarioId(const ScenarioConfig& config)
{
    return std::visit(
        [](auto&& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, EmptyConfig>)
                return "empty";
            else if constexpr (std::is_same_v<T, SandboxConfig>)
                return "sandbox";
            else if constexpr (std::is_same_v<T, DamBreakConfig>)
                return "dam_break";
            else if constexpr (std::is_same_v<T, RainingConfig>)
                return "raining";
            else if constexpr (std::is_same_v<T, WaterEqualizationConfig>)
                return "water_equalization";
            else if constexpr (std::is_same_v<T, FallingDirtConfig>)
                return "falling_dirt";
            else if constexpr (std::is_same_v<T, BenchmarkConfig>)
                return "benchmark";
            else if constexpr (std::is_same_v<T, ClockConfig>)
                return "clock";
            else
                return "unknown";
        },
        config);
}

} // namespace DirtSim

/**
 * ADL (Argument-Dependent Lookup) functions for nlohmann::json conversion of ScenarioConfig
 * variant.
 */
namespace DirtSim {

inline void to_json(nlohmann::json& j, const ScenarioConfig& config)
{
    std::visit(
        [&j](auto&& c) {
            using T = std::decay_t<decltype(c)>;

            // Use ReflectSerializer for automatic field serialization.
            j = ReflectSerializer::to_json(c);

            // Add type discriminator for variant deserialization.
            if constexpr (std::is_same_v<T, EmptyConfig>) {
                j["type"] = "empty";
            }
            else if constexpr (std::is_same_v<T, SandboxConfig>) {
                j["type"] = "sandbox";
            }
            else if constexpr (std::is_same_v<T, DamBreakConfig>) {
                j["type"] = "dam_break";
            }
            else if constexpr (std::is_same_v<T, RainingConfig>) {
                j["type"] = "raining";
            }
            else if constexpr (std::is_same_v<T, WaterEqualizationConfig>) {
                j["type"] = "water_equalization";
            }
            else if constexpr (std::is_same_v<T, FallingDirtConfig>) {
                j["type"] = "falling_dirt";
            }
            else if constexpr (std::is_same_v<T, BenchmarkConfig>) {
                j["type"] = "benchmark";
            }
            else if constexpr (std::is_same_v<T, ClockConfig>) {
                j["type"] = "clock";
            }
        },
        config);
}

inline void from_json(const nlohmann::json& j, ScenarioConfig& config)
{
    std::string type = j.value("type", "empty");

    // Use ReflectSerializer for automatic field deserialization.
    if (type == "empty") {
        config = ReflectSerializer::from_json<EmptyConfig>(j);
    }
    else if (type == "sandbox") {
        config = ReflectSerializer::from_json<SandboxConfig>(j);
    }
    else if (type == "dam_break") {
        config = ReflectSerializer::from_json<DamBreakConfig>(j);
    }
    else if (type == "raining") {
        config = ReflectSerializer::from_json<RainingConfig>(j);
    }
    else if (type == "water_equalization") {
        config = ReflectSerializer::from_json<WaterEqualizationConfig>(j);
    }
    else if (type == "falling_dirt") {
        config = ReflectSerializer::from_json<FallingDirtConfig>(j);
    }
    else if (type == "benchmark") {
        config = ReflectSerializer::from_json<BenchmarkConfig>(j);
    }
    else if (type == "clock") {
        config = ReflectSerializer::from_json<ClockConfig>(j);
    }
    else {
        config = EmptyConfig{};
    }
}

} // namespace DirtSim
