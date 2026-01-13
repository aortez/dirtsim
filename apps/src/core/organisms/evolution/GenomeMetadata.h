#pragma once

#include "core/ScenarioId.h"
#include "core/UUID.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace DirtSim {

using GenomeId = UUID;
inline const GenomeId INVALID_GENOME_ID = UUID::nil();

/**
 * Metadata for a stored genome in the repository.
 * Tracks provenance and performance for selection and display.
 */
struct GenomeMetadata {
    std::string name;              // User-provided or auto-generated.
    double fitness = 0.0;          // Best fitness achieved.
    int generation = 0;            // Generation it came from.
    uint64_t createdTimestamp = 0; // Unix timestamp.
    Scenario::EnumType scenarioId =
        Scenario::EnumType::TreeGermination; // Which scenario it was trained on.
    std::string notes;                       // Optional user notes.
};

void to_json(nlohmann::json& j, const GenomeMetadata& meta);
void from_json(const nlohmann::json& j, GenomeMetadata& meta);

} // namespace DirtSim
