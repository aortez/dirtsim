#pragma once

#include "ScenarioId.h"
#include "VariantSerializer.h"
#include "core/scenarios/BenchmarkConfig.h"
#include "core/scenarios/ClockConfig.h"
#include "core/scenarios/DamBreakConfig.h"
#include "core/scenarios/EmptyConfig.h"
#include "core/scenarios/FallingDirtConfig.h"
#include "core/scenarios/GooseTestConfig.h"
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
    Config::FallingDirt,
    Config::GooseTest,
    Config::Raining,
    Config::Sandbox,
    Config::TreeGermination,
    Config::WaterEqualization>;

// Map variant index to ScenarioId enum (must stay in sync with variant order).
inline ScenarioId getScenarioId(const ScenarioConfig& config)
{
    return static_cast<ScenarioId>(config.index());
}

// Map ScenarioId to default config instance.
inline ScenarioConfig makeDefaultConfig(ScenarioId id)
{
    switch (id) {
        case ScenarioId::Benchmark:
            return Config::Benchmark{};
        case ScenarioId::Clock:
            return Config::Clock{};
        case ScenarioId::DamBreak:
            return Config::DamBreak{};
        case ScenarioId::Empty:
            return Config::Empty{};
        case ScenarioId::FallingDirt:
            return Config::FallingDirt{};
        case ScenarioId::GooseTest:
            return Config::GooseTest{};
        case ScenarioId::Raining:
            return Config::Raining{};
        case ScenarioId::Sandbox:
            return Config::Sandbox{};
        case ScenarioId::TreeGermination:
            return Config::TreeGermination{};
        case ScenarioId::WaterEqualization:
            return Config::WaterEqualization{};
    }
    return Config::Empty{};
}

} // namespace DirtSim
