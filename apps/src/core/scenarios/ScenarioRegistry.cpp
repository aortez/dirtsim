#include "ScenarioRegistry.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/scenarios/BenchmarkScenario.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/DamBreakScenario.h"
#include "core/scenarios/EmptyScenario.h"
#include "core/scenarios/GooseTestScenario.h"
#include "core/scenarios/LightsScenario.h"
#include "core/scenarios/NesScenario.h"
#include "core/scenarios/RainingScenario.h"
#include "core/scenarios/SandboxScenario.h"
#include "core/scenarios/TreeGerminationScenario.h"
#include "core/scenarios/WaterEqualizationScenario.h"

using namespace DirtSim;

ScenarioRegistry::ScenarioRegistry(GenomeRepository& genomeRepository)
    : genomeRepository_(genomeRepository)
{}

ScenarioRegistry ScenarioRegistry::createDefault(GenomeRepository& genomeRepository)
{
    ScenarioRegistry registry(genomeRepository);

    // Register all scenarios with metadata and factory functions.
    // Each registration creates a lambda that produces fresh instances.

    registry.registerScenario(
        Scenario::EnumType::Benchmark, BenchmarkScenario{}.getMetadata(), []() {
            return std::make_unique<BenchmarkScenario>();
        });

    registry.registerScenario(Scenario::EnumType::Clock, ClockScenario{}.getMetadata(), []() {
        return std::make_unique<ClockScenario>();
    });

    registry.registerScenario(Scenario::EnumType::DamBreak, DamBreakScenario{}.getMetadata(), []() {
        return std::make_unique<DamBreakScenario>();
    });

    registry.registerScenario(Scenario::EnumType::Empty, EmptyScenario{}.getMetadata(), []() {
        return std::make_unique<EmptyScenario>();
    });

    registry.registerScenario(
        Scenario::EnumType::GooseTest, GooseTestScenario{}.getMetadata(), []() {
            return std::make_unique<GooseTestScenario>();
        });

    registry.registerScenario(Scenario::EnumType::Lights, LightsScenario{}.getMetadata(), []() {
        return std::make_unique<LightsScenario>();
    });

    registry.registerScenario(Scenario::EnumType::Nes, NesScenario{}.getMetadata(), []() {
        return std::make_unique<NesScenario>();
    });

    registry.registerScenario(Scenario::EnumType::Raining, RainingScenario{}.getMetadata(), []() {
        return std::make_unique<RainingScenario>();
    });

    registry.registerScenario(Scenario::EnumType::Sandbox, SandboxScenario{}.getMetadata(), []() {
        return std::make_unique<SandboxScenario>();
    });

    registry.registerScenario(
        Scenario::EnumType::TreeGermination,
        TreeGerminationScenario{ genomeRepository }.getMetadata(),
        [&genomeRepository]() {
            return std::make_unique<TreeGerminationScenario>(genomeRepository);
        });

    registry.registerScenario(
        Scenario::EnumType::WaterEqualization, WaterEqualizationScenario{}.getMetadata(), []() {
            return std::make_unique<WaterEqualizationScenario>();
        });

    return registry;
}

void ScenarioRegistry::registerScenario(
    Scenario::EnumType id, const ScenarioMetadata& metadata, ScenarioFactory factory)
{
    if (!factory) {
        spdlog::error("Attempted to register null factory for scenario ID: {}", toString(id));
        return;
    }

    if (scenarios_.find(id) != scenarios_.end()) {
        spdlog::warn("Scenario with ID '{}' already registered, overwriting", toString(id));
    }

    spdlog::debug("Registering scenario '{}' - {}", toString(id), metadata.name);
    scenarios_[id] = ScenarioEntry{ metadata, std::move(factory) };
}

std::unique_ptr<ScenarioRunner> ScenarioRegistry::createScenario(Scenario::EnumType id) const
{
    auto it = scenarios_.find(id);
    if (it != scenarios_.end()) {
        return it->second.factory();
    }
    spdlog::error("Scenario '{}' not found in registry", toString(id));
    return nullptr;
}

const ScenarioMetadata* ScenarioRegistry::getMetadata(Scenario::EnumType id) const
{
    auto it = scenarios_.find(id);
    if (it != scenarios_.end()) {
        return &it->second.metadata;
    }
    return nullptr;
}

std::vector<Scenario::EnumType> ScenarioRegistry::getScenarioIds() const
{
    std::vector<Scenario::EnumType> ids;
    ids.reserve(scenarios_.size());

    for (const auto& [id, entry] : scenarios_) {
        ids.push_back(id);
    }

    // std::map keeps keys sorted, so no explicit sort needed.
    return ids;
}

std::vector<Scenario::EnumType> ScenarioRegistry::getScenariosByCategory(
    const std::string& category) const
{
    std::vector<Scenario::EnumType> ids;

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
