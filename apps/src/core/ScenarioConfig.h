#pragma once

#include "ScenarioId.h"
#include "VariantSerializer.h"
#include "core/scenarios/BenchmarkConfig.h"
#include "core/scenarios/ClockConfig.h"
#include "core/scenarios/DamBreakConfig.h"
#include "core/scenarios/EmptyConfig.h"
#include "core/scenarios/GooseTestConfig.h"
#include "core/scenarios/LightsConfig.h"
#include "core/scenarios/RainingConfig.h"
#include "core/scenarios/SandboxConfig.h"
#include "core/scenarios/TreeGerminationConfig.h"
#include "core/scenarios/WaterEqualizationConfig.h"

#include <variant>

namespace DirtSim {

using ScenarioConfig = std::variant<
    Config::Benchmark,
    Config::Clock,
    Config::DamBreak,
    Config::Empty,
    Config::GooseTest,
    Config::Lights,
    Config::Raining,
    Config::Sandbox,
    Config::TreeGermination,
    Config::WaterEqualization>;

// Map variant index to Scenario::EnumType enum (must stay in sync with variant order).
inline Scenario::EnumType getScenarioId(const ScenarioConfig& config)
{
    return static_cast<Scenario::EnumType>(config.index());
}

// Map Scenario::EnumType to default config instance.
inline ScenarioConfig makeDefaultConfig(Scenario::EnumType id)
{
    switch (id) {
        case Scenario::EnumType::Benchmark:
            return Config::Benchmark{};
        case Scenario::EnumType::Clock:
            return Config::Clock{};
        case Scenario::EnumType::DamBreak:
            return Config::DamBreak{};
        case Scenario::EnumType::Empty:
            return Config::Empty{};
        case Scenario::EnumType::GooseTest:
            return Config::GooseTest{};
        case Scenario::EnumType::Lights:
            return Config::Lights{};
        case Scenario::EnumType::Raining:
            return Config::Raining{};
        case Scenario::EnumType::Sandbox:
            return Config::Sandbox{};
        case Scenario::EnumType::TreeGermination:
            return Config::TreeGermination{};
        case Scenario::EnumType::WaterEqualization:
            return Config::WaterEqualization{};
    }
    return Config::Empty{};
}

} // namespace DirtSim
