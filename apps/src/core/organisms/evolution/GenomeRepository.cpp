#include "GenomeRepository.h"

#include "core/organisms/brains/Genome.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sqlite_modern_cpp.h>

namespace DirtSim {

// Schema version for future migrations.
constexpr int SCHEMA_VERSION = 1;

namespace {

// DRY helper for database operations with error handling.
template <typename Func>
void execDb(sqlite::database& db, const char* operation, Func&& func)
{
    try {
        func(db);
    }
    catch (const sqlite::sqlite_exception& e) {
        spdlog::error(
            "GenomeRepository: {} failed: {} (code {})", operation, e.what(), e.get_code());
    }
    catch (const std::exception& e) {
        spdlog::error("GenomeRepository: {} failed: {}", operation, e.what());
    }
}

} // namespace

GenomeRepository::GenomeRepository() = default;

GenomeRepository::GenomeRepository(const std::filesystem::path& dbPath)
    : db_(std::make_unique<sqlite::database>(dbPath.string()))
{
    spdlog::info("GenomeRepository: Opening database at {}", dbPath.string());
    initSchema();
    loadFromDb();
}

GenomeRepository::~GenomeRepository() = default;

GenomeRepository::GenomeRepository(GenomeRepository&&) noexcept = default;
GenomeRepository& GenomeRepository::operator=(GenomeRepository&&) noexcept = default;

void GenomeRepository::initSchema()
{
    // Create tables if they don't exist.
    // Using TEXT for UUID (hex string) and BLOB for weights.
    *db_ << R"(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY
        )
    )";

    *db_ << R"(
        CREATE TABLE IF NOT EXISTS genomes (
            id TEXT PRIMARY KEY,
            weights BLOB NOT NULL,
            metadata_json TEXT NOT NULL
        )
    )";

    *db_ << R"(
        CREATE TABLE IF NOT EXISTS repository_state (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )";

    // Check/set schema version.
    int existingVersion = 0;
    *db_ << "SELECT version FROM schema_version LIMIT 1" >> [&](int v) { existingVersion = v; };

    if (existingVersion == 0) {
        *db_ << "INSERT INTO schema_version (version) VALUES (?)" << SCHEMA_VERSION;
        spdlog::info("GenomeRepository: Initialized schema version {}", SCHEMA_VERSION);
    }
    else if (existingVersion != SCHEMA_VERSION) {
        spdlog::warn(
            "GenomeRepository: Schema version mismatch (db={}, code={})",
            existingVersion,
            SCHEMA_VERSION);
        // Future: handle migrations here.
    }
}

void GenomeRepository::loadFromDb()
{
    int loadedCount = 0;

    // Load all genomes.
    *db_ << "SELECT id, weights, metadata_json FROM genomes" >> [&](std::string idStr,
                                                                    std::vector<WeightType> weights,
                                                                    std::string metaJson) {
        GenomeId id = UUID::fromString(idStr);
        if (id == INVALID_GENOME_ID) {
            spdlog::warn("GenomeRepository: Skipping invalid genome ID: {}", idStr);
            return;
        }

        Genome genome;
        genome.weights = std::move(weights);

        try {
            auto j = nlohmann::json::parse(metaJson);
            GenomeMetadata meta = j.get<GenomeMetadata>();
            genomes_[id] = std::move(genome);
            metadata_[id] = std::move(meta);
            loadedCount++;
        }
        catch (const std::exception& e) {
            spdlog::warn("GenomeRepository: Failed to parse metadata for {}: {}", idStr, e.what());
        }
    };

    // Load best ID if set.
    *db_ << "SELECT value FROM repository_state WHERE key = 'best_id'" >>
        [&](std::string bestIdStr) {
            if (!bestIdStr.empty()) {
                GenomeId bestId = UUID::fromString(bestIdStr);
                if (bestId != INVALID_GENOME_ID && exists(bestId)) {
                    bestId_ = bestId;
                }
            }
        };

    spdlog::info(
        "GenomeRepository: Loaded {} genomes from database{}",
        loadedCount,
        bestId_ ? " (best: " + bestId_->toString() + ")" : "");
}

void GenomeRepository::persistGenome(GenomeId id, const Genome& genome, const GenomeMetadata& meta)
{
    std::string idStr = id.toString();
    nlohmann::json metaJ = meta;
    std::string metaJson = metaJ.dump();

    execDb(*db_, "persistGenome", [&](sqlite::database& db) {
        db << "INSERT OR REPLACE INTO genomes (id, weights, metadata_json) VALUES (?, ?, ?)"
           << idStr << genome.weights << metaJson;
    });
}

void GenomeRepository::deleteGenome(GenomeId id)
{
    execDb(*db_, "deleteGenome", [&](sqlite::database& db) {
        db << "DELETE FROM genomes WHERE id = ?" << id.toString();
    });
}

void GenomeRepository::persistBestId()
{
    std::string value = bestId_ ? bestId_->toString() : "";
    execDb(*db_, "persistBestId", [&](sqlite::database& db) {
        db << "INSERT OR REPLACE INTO repository_state (key, value) VALUES ('best_id', ?)" << value;
    });
}

void GenomeRepository::clearDb()
{
    execDb(*db_, "clearDb", [&](sqlite::database& db) {
        db << "BEGIN TRANSACTION";
        db << "DELETE FROM genomes";
        db << "DELETE FROM repository_state WHERE key = 'best_id'";
        db << "COMMIT";
    });
}

void GenomeRepository::store(GenomeId id, const Genome& genome, const GenomeMetadata& meta)
{
    genomes_[id] = genome;
    metadata_[id] = meta;

    if (db_) {
        persistGenome(id, genome, meta);
    }
}

bool GenomeRepository::exists(GenomeId id) const
{
    return genomes_.find(id) != genomes_.end();
}

std::optional<Genome> GenomeRepository::get(GenomeId id) const
{
    auto it = genomes_.find(id);
    if (it == genomes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<GenomeMetadata> GenomeRepository::getMetadata(GenomeId id) const
{
    auto it = metadata_.find(id);
    if (it == metadata_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::pair<GenomeId, GenomeMetadata>> GenomeRepository::list() const
{
    std::vector<std::pair<GenomeId, GenomeMetadata>> result;
    result.reserve(metadata_.size());
    for (const auto& [id, meta] : metadata_) {
        result.emplace_back(id, meta);
    }
    return result;
}

void GenomeRepository::remove(GenomeId id)
{
    genomes_.erase(id);
    metadata_.erase(id);

    // Clear best if we removed it.
    if (bestId_ && *bestId_ == id) {
        bestId_ = std::nullopt;
        if (db_) {
            persistBestId();
        }
    }

    if (db_) {
        deleteGenome(id);
    }
}

void GenomeRepository::clear()
{
    genomes_.clear();
    metadata_.clear();
    bestId_ = std::nullopt;

    if (db_) {
        clearDb();
    }
}

void GenomeRepository::markAsBest(GenomeId id)
{
    if (genomes_.find(id) != genomes_.end()) {
        bestId_ = id;
        if (db_) {
            persistBestId();
        }
    }
}

std::optional<GenomeId> GenomeRepository::getBestId() const
{
    return bestId_;
}

std::optional<Genome> GenomeRepository::getBest() const
{
    if (!bestId_) {
        return std::nullopt;
    }
    return get(*bestId_);
}

size_t GenomeRepository::count() const
{
    return genomes_.size();
}

bool GenomeRepository::empty() const
{
    return genomes_.empty();
}

bool GenomeRepository::isPersistent() const
{
    return db_ != nullptr;
}

} // namespace DirtSim
