#pragma once

#include "VariantSerializer.h"
#include "core/scenarios/BenchmarkConfig.h"
#include "core/scenarios/ClockConfig.h"
#include "core/scenarios/DamBreakConfig.h"
#include "core/scenarios/EmptyConfig.h"
#include "core/scenarios/FallingDirtConfig.h"
#include "core/scenarios/RainingConfig.h"
#include "core/scenarios/SandboxConfig.h"
#include "core/scenarios/TreeGerminationConfig.h"
#include "core/scenarios/WaterEqualizationConfig.h"
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
