#pragma once

#include "VariantSerializer.h"
#include "server/scenarios/scenarios/BenchmarkConfig.h"
#include "server/scenarios/scenarios/ClockConfig.h"
#include "server/scenarios/scenarios/DamBreakConfig.h"
#include "server/scenarios/scenarios/EmptyConfig.h"
#include "server/scenarios/scenarios/FallingDirtConfig.h"
#include "server/scenarios/scenarios/RainingConfig.h"
#include "server/scenarios/scenarios/SandboxConfig.h"
#include "server/scenarios/scenarios/TreeGerminationConfig.h"
#include "server/scenarios/scenarios/WaterEqualizationConfig.h"
#include <string>
#include <variant>

namespace DirtSim {

using ScenarioConfig = std::variant<
    Config::Benchmark,
    Config::Clock,
    Config::DamBreak,
    Config::Empty,
    Config::FallingDirt,
    Config::Raining,
    Config::Sandbox,
    Config::TreeGermination,
    Config::WaterEqualization>;

inline std::string getScenarioId(const ScenarioConfig& config)
{
    return std::visit(
        [](const auto& c) -> std::string {
            return std::string(reflect::type_name<std::decay_t<decltype(c)>>());
        },
        config);
}

} // namespace DirtSim
