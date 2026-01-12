#include "ScenarioRegistry.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/scenarios/BenchmarkScenario.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/DamBreakScenario.h"
#include "core/scenarios/EmptyScenario.h"
#include "core/scenarios/FallingDirtScenario.h"
#include "core/scenarios/GooseTestScenario.h"
#include "core/scenarios/RainingScenario.h"
#include "core/scenarios/SandboxScenario.h"
#include "core/scenarios/TreeGerminationScenario.h"
#include "core/scenarios/WaterEqualizationScenario.h"

using namespace DirtSim;

ScenarioRegistry ScenarioRegistry::createDefault()
{
    ScenarioRegistry registry;

    // Register all scenarios with metadata and factory functions.
    // Each registration creates a lambda that produces fresh instances.

    registry.registerScenario(ScenarioId::Benchmark, BenchmarkScenario{}.getMetadata(), []() {
        return std::make_unique<BenchmarkScenario>();
    });

    registry.registerScenario(ScenarioId::Clock, ClockScenario{}.getMetadata(), []() {
        return std::make_unique<ClockScenario>();
    });

    registry.registerScenario(ScenarioId::DamBreak, DamBreakScenario{}.getMetadata(), []() {
        return std::make_unique<DamBreakScenario>();
    });

    registry.registerScenario(ScenarioId::Empty, EmptyScenario{}.getMetadata(), []() {
        return std::make_unique<EmptyScenario>();
    });

    registry.registerScenario(ScenarioId::FallingDirt, FallingDirtScenario{}.getMetadata(), []() {
        return std::make_unique<FallingDirtScenario>();
    });

    registry.registerScenario(ScenarioId::GooseTest, GooseTestScenario{}.getMetadata(), []() {
        return std::make_unique<GooseTestScenario>();
    });

    registry.registerScenario(ScenarioId::Raining, RainingScenario{}.getMetadata(), []() {
        return std::make_unique<RainingScenario>();
    });

    registry.registerScenario(ScenarioId::Sandbox, SandboxScenario{}.getMetadata(), []() {
        return std::make_unique<SandboxScenario>();
    });

    registry.registerScenario(
        ScenarioId::TreeGermination, TreeGerminationScenario{}.getMetadata(), []() {
            return std::make_unique<TreeGerminationScenario>();
        });

    registry.registerScenario(
        ScenarioId::WaterEqualization, WaterEqualizationScenario{}.getMetadata(), []() {
            return std::make_unique<WaterEqualizationScenario>();
        });

    return registry;
}

void ScenarioRegistry::registerScenario(
    ScenarioId id, const ScenarioMetadata& metadata, ScenarioFactory factory)
{
    if (!factory) {
        spdlog::error("Attempted to register null factory for scenario ID: {}", toString(id));
        return;
    }

    if (scenarios_.find(id) != scenarios_.end()) {
        spdlog::warn("Scenario with ID '{}' already registered, overwriting", toString(id));
    }

    spdlog::info("Registering scenario '{}' - {}", toString(id), metadata.name);
    scenarios_[id] = ScenarioEntry{ metadata, std::move(factory) };
}

std::unique_ptr<Scenario> ScenarioRegistry::createScenario(ScenarioId id) const
{
    auto it = scenarios_.find(id);
    if (it != scenarios_.end()) {
        return it->second.factory();
    }
    spdlog::error("Scenario '{}' not found in registry", toString(id));
    return nullptr;
}

const ScenarioMetadata* ScenarioRegistry::getMetadata(ScenarioId id) const
{
    auto it = scenarios_.find(id);
    if (it != scenarios_.end()) {
        return &it->second.metadata;
    }
    return nullptr;
}

std::vector<ScenarioId> ScenarioRegistry::getScenarioIds() const
{
    std::vector<ScenarioId> ids;
    ids.reserve(scenarios_.size());

    for (const auto& [id, entry] : scenarios_) {
        ids.push_back(id);
    }

    // std::map keeps keys sorted, so no explicit sort needed.
    return ids;
}

std::vector<ScenarioId> ScenarioRegistry::getScenariosByCategory(const std::string& category) const
{
    std::vector<ScenarioId> ids;

    for (const auto& [id, entry] : scenarios_) {
        if (entry.metadata.category == category) {
            ids.push_back(id);
        }
    }

    // std::map keeps keys sorted, so no explicit sort needed.
    return ids;
}

void ScenarioRegistry::clear()
{
    spdlog::info("Clearing scenario registry");
    scenarios_.clear();
}