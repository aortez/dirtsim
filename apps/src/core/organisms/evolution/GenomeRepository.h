#pragma once

#include "GenomeMetadata.h"
#include "core/organisms/brains/Genome.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward declaration to avoid including sqlite_modern_cpp in header.
namespace sqlite {
class database;
}

namespace DirtSim {

/**
 * Storage and retrieval for evolved genomes.
 * Persists across server state changes, tracks the best performer.
 * Public methods are thread-safe but serialized internally.
 *
 * Two modes:
 * - In-memory only (default constructor): For tests and temporary use.
 * - Persistent (path constructor): Write-through to SQLite database.
 */
class GenomeRepository {
public:
    struct StoreByHashResult {
        GenomeId id = INVALID_GENOME_ID;
        bool inserted = false;
        bool deduplicated = false;
    };

    // Default constructor - in-memory only, no persistence.
    GenomeRepository();

    // Construct with SQLite persistence at the given path.
    // Creates the database and schema if it doesn't exist.
    // Loads existing genomes from the database on construction.
    explicit GenomeRepository(const std::filesystem::path& dbPath);

    ~GenomeRepository();

    // Move-only (database connection is not copyable).
    GenomeRepository(GenomeRepository&&) noexcept;
    GenomeRepository& operator=(GenomeRepository&&) noexcept;
    GenomeRepository(const GenomeRepository&) = delete;
    GenomeRepository& operator=(const GenomeRepository&) = delete;

    // Store a genome with metadata at the given ID. Overwrites if ID exists.
    void store(GenomeId id, const Genome& genome, const GenomeMetadata& meta);

    // Store a genome keyed by content hash. Reuses existing ID when content matches.
    StoreByHashResult storeOrUpdateByHash(
        const Genome& genome,
        const GenomeMetadata& meta,
        std::optional<GenomeId> preferredId = std::nullopt);

    // Keep only the highest-fitness managed genomes (trainingSessionId set),
    // limited per organismType+brainKind bucket.
    // Returns number of genomes removed.
    size_t pruneManagedByFitness(size_t maxManagedGenomes);

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

    // Check if persistence is enabled.
    bool isPersistent() const;

private:
    // In-memory storage (always present for fast access).
    std::unordered_map<GenomeId, Genome> genomes_;
    std::unordered_map<std::string, GenomeId> hashToId_;
    std::unordered_map<GenomeId, std::string> idToHash_;
    std::unordered_map<GenomeId, GenomeMetadata> metadata_;
    std::optional<GenomeId> bestId_;

    // Optional SQLite database for persistence.
    std::unique_ptr<sqlite::database> db_;

    // Heap-allocated to preserve move semantics.
    mutable std::unique_ptr<std::mutex> mutex_ = std::make_unique<std::mutex>();

    // Database operations.
    void initSchema();
    void loadFromDb();
    void persistGenome(
        GenomeId id,
        const Genome& genome,
        const GenomeMetadata& meta,
        const std::string& contentHash);
    void persistGenomeHash(GenomeId id, const std::string& contentHash);
    void deleteGenome(GenomeId id);
    void persistBestId();
    void clearDb();

    static std::string computeContentHash(const Genome& genome, const GenomeMetadata& meta);
    void removeNoLock(GenomeId id);
};

} // namespace DirtSim
