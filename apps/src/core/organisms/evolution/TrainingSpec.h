#pragma once

#include "core/ReflectSerializer.h"
#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/evolution/GenomeMetadata.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * Population entry describing one brain kind/variant and its counts.
 */
struct PopulationSpec {
    std::string brainKind;
    std::optional<std::string> brainVariant;
    int count = 0;
    std::vector<GenomeId> seedGenomes;
    int randomCount = 0;

    using serialize = zpp::bits::members<5>;
};

/**
 * Training specification for a single organism type in a scenario.
 */
struct TrainingSpec {
    Scenario::EnumType scenarioId = Scenario::EnumType::TreeGermination;
    OrganismType organismType = OrganismType::TREE;
    std::vector<PopulationSpec> population;

    using serialize = zpp::bits::members<3>;
};

inline void to_json(nlohmann::json& j, const PopulationSpec& spec)
{
    j = ReflectSerializer::to_json(spec);
}

inline void from_json(const nlohmann::json& j, PopulationSpec& spec)
{
    spec = ReflectSerializer::from_json<PopulationSpec>(j);
}

inline void to_json(nlohmann::json& j, const TrainingSpec& spec)
{
    j = ReflectSerializer::to_json(spec);
}

inline void from_json(const nlohmann::json& j, TrainingSpec& spec)
{
    spec = ReflectSerializer::from_json<TrainingSpec>(j);
}

} // namespace DirtSim
