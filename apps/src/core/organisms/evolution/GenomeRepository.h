#pragma once

#include "GenomeMetadata.h"
#include "core/organisms/brains/Genome.h"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DirtSim {

/**
 * Storage and retrieval for evolved genomes.
 * Persists across server state changes, tracks the best performer.
 */
class GenomeRepository {
public:
    // Store a genome with metadata at the given ID. Overwrites if ID exists.
    void store(GenomeId id, const Genome& genome, const GenomeMetadata& meta);

    // Check if a genome exists.
    bool exists(GenomeId id) const;

    // Retrieve genome or metadata by ID.
    std::optional<Genome> get(GenomeId id) const;
    std::optional<GenomeMetadata> getMetadata(GenomeId id) const;

    // List all stored genomes with their metadata.
    std::vector<std::pair<GenomeId, GenomeMetadata>> list() const;

    // Remove a genome.
    void remove(GenomeId id);

    // Clear all stored genomes.
    void clear();

    // Best genome tracking.
    void markAsBest(GenomeId id);
    std::optional<GenomeId> getBestId() const;
    std::optional<Genome> getBest() const;

    // Statistics.
    size_t count() const;
    bool empty() const;

private:
    std::unordered_map<GenomeId, Genome> genomes_;
    std::unordered_map<GenomeId, GenomeMetadata> metadata_;
    std::optional<GenomeId> bestId_;
};

} // namespace DirtSim
