#pragma once

#include "core/ScenarioId.h"
#include "core/scenarios/Scenario.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * Central registry for all available scenarios.
 * Uses factory pattern to create fresh scenario instances (not singletons).
 * Owned by StateMachine to provide isolated registries for testing.
 */
class ScenarioRegistry {
public:
    ScenarioRegistry() = default;

    /**
     * @brief Create a registry populated with all available scenarios.
     * @return Initialized registry with all scenario factories.
     */
    static ScenarioRegistry createDefault();

    // Register a scenario factory function with the given ID.
    using ScenarioFactory = std::function<std::unique_ptr<DirtSim::Scenario>()>;
    void registerScenario(
        DirtSim::ScenarioId id, const DirtSim::ScenarioMetadata& metadata, ScenarioFactory factory);

    // Create a new scenario instance by ID (factory pattern).
    std::unique_ptr<DirtSim::Scenario> createScenario(DirtSim::ScenarioId id) const;

    // Get metadata for a scenario by ID (no instance created).
    const DirtSim::ScenarioMetadata* getMetadata(DirtSim::ScenarioId id) const;

    // Get all registered scenario IDs.
    std::vector<DirtSim::ScenarioId> getScenarioIds() const;

    // Get scenarios filtered by category.
    std::vector<DirtSim::ScenarioId> getScenariosByCategory(const std::string& category) const;

    // Clear all registered scenarios (mainly for testing).
    void clear();

private:
    // Storage for scenario metadata and factories.
    struct ScenarioEntry {
        DirtSim::ScenarioMetadata metadata;
        ScenarioFactory factory;
    };
    std::map<DirtSim::ScenarioId, ScenarioEntry> scenarios_;
};