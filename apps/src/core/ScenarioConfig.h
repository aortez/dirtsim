#pragma once

#include "ScenarioId.h"
#include "core/scenarios/BenchmarkConfig.h"
#include "core/scenarios/ClockConfig.h"
#include "core/scenarios/DamBreakConfig.h"
#include "core/scenarios/EmptyConfig.h"
#include "core/scenarios/GooseTestConfig.h"
#include "core/scenarios/LightsConfig.h"
#include "core/scenarios/NesConfig.h"
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
    Config::NesFlappyParatroopa,
    Config::WaterEqualization,
    Config::NesSuperTiltBro>;

// Map variant index to Scenario::EnumType enum (must stay in sync with variant order).
Scenario::EnumType getScenarioId(const ScenarioConfig& config);

// Map Scenario::EnumType to default config instance.
ScenarioConfig makeDefaultConfig(Scenario::EnumType id);

} // namespace DirtSim
