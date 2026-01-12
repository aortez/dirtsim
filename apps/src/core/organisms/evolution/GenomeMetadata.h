#pragma once

#include "core/StrongType.h"

#include <cstdint>
#include <string>

namespace DirtSim {

using GenomeId = StrongType<struct GenomeIdTag>;
const GenomeId INVALID_GENOME_ID{};

/**
 * Metadata for a stored genome in the repository.
 * Tracks provenance and performance for selection and display.
 */
struct GenomeMetadata {
    std::string name;              // User-provided or auto-generated.
    double fitness = 0.0;          // Best fitness achieved.
    int generation = 0;            // Generation it came from.
    uint64_t createdTimestamp = 0; // Unix timestamp.
    std::string scenarioId;        // Which scenario it was trained on.
    std::string notes;             // Optional user notes.
};

} // namespace DirtSim
