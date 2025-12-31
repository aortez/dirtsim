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
#include <algorithm>

using namespace DirtSim;

ScenarioRegistry ScenarioRegistry::createDefault()
{
    ScenarioRegistry registry;

    // Register all scenarios with metadata and factory functions.
    // Each registration creates a lambda that produces fresh instances.

    {
        auto temp = std::make_unique<BenchmarkScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<BenchmarkScenario>();
        });
    }

    {
        auto temp = std::make_unique<ClockScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<ClockScenario>();
        });
    }

    {
        auto temp = std::make_unique<DamBreakScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<DamBreakScenario>();
        });
    }

    {
        auto temp = std::make_unique<EmptyScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<EmptyScenario>();
        });
    }

    {
        auto temp = std::make_unique<FallingDirtScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<FallingDirtScenario>();
        });
    }

    {
        auto temp = std::make_unique<GooseTestScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<GooseTestScenario>();
        });
    }

    {
        auto temp = std::make_unique<RainingScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<RainingScenario>();
        });
    }

    {
        auto temp = std::make_unique<SandboxScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<SandboxScenario>();
        });
    }

    {
        auto temp = std::make_unique<TreeGerminationScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<TreeGerminationScenario>();
        });
    }

    {
        auto temp = std::make_unique<WaterEqualizationScenario>();
        std::string id = getScenarioId(temp->getConfig());
        registry.registerScenario(id, temp->getMetadata(), []() {
            return std::make_unique<WaterEqualizationScenario>();
        });
    }

    return registry;
}

void ScenarioRegistry::registerScenario(
    const std::string& id, const ScenarioMetadata& metadata, ScenarioFactory factory)
{
    if (!factory) {
        spdlog::error("Attempted to register null factory for scenario ID: {}", id);
        return;
    }

    if (scenarios_.find(id) != scenarios_.end()) {
        spdlog::warn("Scenario with ID '{}' already registered, overwriting", id);
    }

    spdlog::info("Registering scenario '{}' - {}", id, metadata.name);
    scenarios_[id] = ScenarioEntry{ metadata, std::move(factory) };
}

std::unique_ptr<Scenario> ScenarioRegistry::createScenario(const std::string& id) const
{
    auto it = scenarios_.find(id);
    if (it != scenarios_.end()) {
        return it->second.factory();
    }
    spdlog::error("Scenario '{}' not found in registry", id);
    return nullptr;
}

const ScenarioMetadata* ScenarioRegistry::getMetadata(const std::string& id) const
{
    auto it = scenarios_.find(id);
    if (it != scenarios_.end()) {
        return &it->second.metadata;
    }
    return nullptr;
}

std::vector<std::string> ScenarioRegistry::getScenarioIds() const
{
    std::vector<std::string> ids;
    ids.reserve(scenarios_.size());

    for (const auto& [id, entry] : scenarios_) {
        ids.push_back(id);
    }

    // Sort alphabetically for consistent UI display.
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> ScenarioRegistry::getScenariosByCategory(const std::string& category) const
{
    std::vector<std::string> ids;

    for (const auto& [id, entry] : scenarios_) {
        if (entry.metadata.category == category) {
            ids.push_back(id);
        }
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

void ScenarioRegistry::clear()
{
    spdlog::info("Clearing scenario registry");
    scenarios_.clear();
}