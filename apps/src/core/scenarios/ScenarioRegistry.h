#pragma once

#include "core/ScenarioId.h"
#include "core/scenarios/Scenario.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace DirtSim {
class GenomeRepository;
}

/**
 * Central registry for all available scenarios.
 * Uses factory pattern to create fresh scenario instances (not singletons).
 * Owned by StateMachine to provide isolated registries for testing.
 * Holds reference to GenomeRepository for scenarios that need genome access.
 */
class ScenarioRegistry {
public:
    explicit ScenarioRegistry(DirtSim::GenomeRepository& genomeRepository);

    static ScenarioRegistry createDefault(DirtSim::GenomeRepository& genomeRepository);

    // Register a scenario factory function with the given ID.
    using ScenarioFactory = std::function<std::unique_ptr<DirtSim::ScenarioRunner>()>;
    void registerScenario(
        DirtSim::Scenario::EnumType id,
        const DirtSim::ScenarioMetadata& metadata,
        ScenarioFactory factory);

    // Create a new scenario instance by ID (factory pattern).
    std::unique_ptr<DirtSim::ScenarioRunner> createScenario(DirtSim::Scenario::EnumType id) const;

    // Get metadata for a scenario by ID (no instance created).
    const DirtSim::ScenarioMetadata* getMetadata(DirtSim::Scenario::EnumType id) const;

    // Get all registered scenario IDs.
    std::vector<DirtSim::Scenario::EnumType> getScenarioIds() const;

    // Get scenarios filtered by category.
    std::vector<DirtSim::Scenario::EnumType> getScenariosByCategory(
        const std::string& category) const;

    // Clear all registered scenarios (mainly for testing).
    void clear();

    DirtSim::GenomeRepository& getGenomeRepository() { return genomeRepository_; }

private:
    DirtSim::GenomeRepository& genomeRepository_;

    struct ScenarioEntry {
        DirtSim::ScenarioMetadata metadata;
        ScenarioFactory factory;
    };
    std::map<DirtSim::Scenario::EnumType, ScenarioEntry> scenarios_;
};