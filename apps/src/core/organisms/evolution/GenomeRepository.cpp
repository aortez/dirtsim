#include "GenomeRepository.h"

#include "GenomeMetadataUtils.h"
#include "core/Assert.h"
#include "core/organisms/brains/Genome.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sqlite_modern_cpp.h>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace DirtSim {

// Schema version for future migrations.
constexpr int SCHEMA_VERSION = 1;

namespace {
constexpr size_t kRobustFitnessSampleWindow = 7;

struct ManagedGenomeBucketKey {
    int organismType = -1;
    std::string brainKind;

    bool operator==(const ManagedGenomeBucketKey& other) const
    {
        return organismType == other.organismType && brainKind == other.brainKind;
    }
};

struct ManagedGenomeBucketKeyHash {
    size_t operator()(const ManagedGenomeBucketKey& key) const
    {
        const size_t typeHash = std::hash<int>{}(key.organismType);
        const size_t brainKindHash = std::hash<std::string>{}(key.brainKind);
        return typeHash ^ (brainKindHash << 1);
    }
};

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

void hashBytes(uint64_t& hash, const void* data, size_t size)
{
    constexpr uint64_t FNV_PRIME = 1099511628211ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
}

template <typename T>
void hashValue(uint64_t& hash, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    hashBytes(hash, &value, sizeof(T));
}

void hashString(uint64_t& hash, std::string_view value)
{
    const uint64_t size = value.size();
    hashValue(hash, size);
    if (!value.empty()) {
        hashBytes(hash, value.data(), value.size());
    }
}

std::string toHexString(uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

GenomeMetadata normalizeRobustMetadata(const GenomeMetadata& input)
{
    GenomeMetadata normalized = input;
    if (normalized.robustEvalCount < 0) {
        normalized.robustEvalCount = 0;
    }

    if (!normalized.robustFitnessSamples.empty()) {
        if (normalized.robustFitnessSamples.size() > kRobustFitnessSampleWindow) {
            const size_t trimCount =
                normalized.robustFitnessSamples.size() - kRobustFitnessSampleWindow;
            normalized.robustFitnessSamples.erase(
                normalized.robustFitnessSamples.begin(),
                normalized.robustFitnessSamples.begin()
                    + static_cast<std::vector<double>::difference_type>(trimCount));
        }
        normalized.robustFitness = computeMedian(normalized.robustFitnessSamples);
        normalized.robustEvalCount = std::max(
            normalized.robustEvalCount, static_cast<int>(normalized.robustFitnessSamples.size()));
        return normalized;
    }

    if (normalized.robustEvalCount > 0) {
        if (!std::isfinite(normalized.robustFitness)) {
            normalized.robustFitness = normalized.fitness;
        }
        return normalized;
    }

    return normalized;
}

void appendRobustSample(GenomeMetadata& metadata, double fitnessSample)
{
    if (!std::isfinite(fitnessSample)) {
        return;
    }

    metadata.robustEvalCount = std::max(0, metadata.robustEvalCount) + 1;
    metadata.robustFitnessSamples.push_back(fitnessSample);
    if (metadata.robustFitnessSamples.size() > kRobustFitnessSampleWindow) {
        metadata.robustFitnessSamples.erase(metadata.robustFitnessSamples.begin());
    }
    metadata.robustFitness = computeMedian(metadata.robustFitnessSamples);
}

GenomeMetadata mergeMetadata(const GenomeMetadata& existingRaw, const GenomeMetadata& incomingRaw)
{
    const GenomeMetadata existing = normalizeRobustMetadata(existingRaw);
    const GenomeMetadata incoming = normalizeRobustMetadata(incomingRaw);

    GenomeMetadata merged = incoming;
    merged.fitness = std::max(existing.fitness, incoming.fitness);

    if (merged.name.empty()) {
        merged.name = existing.name;
    }
    if (merged.notes.empty()) {
        merged.notes = existing.notes;
    }
    if (!merged.organismType.has_value()) {
        merged.organismType = existing.organismType;
    }
    if (!merged.brainKind.has_value()) {
        merged.brainKind = existing.brainKind;
    }
    if (!merged.brainVariant.has_value()) {
        merged.brainVariant = existing.brainVariant;
    }
    if (!merged.trainingSessionId.has_value()) {
        merged.trainingSessionId = existing.trainingSessionId;
    }
    if (merged.createdTimestamp == 0) {
        merged.createdTimestamp = existing.createdTimestamp;
    }

    merged.robustFitnessSamples = existing.robustFitnessSamples;
    if (!merged.robustFitnessSamples.empty()) {
        merged.robustFitness = computeMedian(merged.robustFitnessSamples);
    }
    merged.robustEvalCount = effectiveRobustEvalCount(existing);
    for (double sample : incoming.robustFitnessSamples) {
        appendRobustSample(merged, sample);
    }

    const int incomingEvalCount = effectiveRobustEvalCount(incoming);
    const int missingEvalCount =
        std::max(0, incomingEvalCount - static_cast<int>(incoming.robustFitnessSamples.size()));
    merged.robustEvalCount += missingEvalCount;

    if (merged.robustEvalCount <= 0) {
        merged = normalizeRobustMetadata(merged);
    }
    else if (merged.robustFitnessSamples.empty()) {
        merged.robustFitness =
            std::max(effectiveRobustFitness(existing), effectiveRobustFitness(incoming));
    }

    return merged;
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

    int hasContentHashColumn = 0;
    *db_ << "SELECT COUNT(*) FROM pragma_table_info('genomes') WHERE name = 'content_hash'" >>
        [&](int count) { hasContentHashColumn = count; };
    if (hasContentHashColumn == 0) {
        *db_ << "ALTER TABLE genomes ADD COLUMN content_hash TEXT";
    }
    *db_ << "CREATE INDEX IF NOT EXISTS idx_genomes_content_hash ON genomes(content_hash)";

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
    *db_ << "SELECT id, weights, metadata_json, COALESCE(content_hash, '') FROM genomes" >>
        [&](std::string idStr,
            std::vector<WeightType> weights,
            std::string metaJson,
            std::string contentHash) {
            const GenomeId id = UUID::fromString(idStr);
            if (id == INVALID_GENOME_ID) {
                spdlog::warn("GenomeRepository: Skipping invalid genome ID: {}", idStr);
                return;
            }

            Genome genome;
            genome.weights = std::move(weights);

            try {
                auto json = nlohmann::json::parse(metaJson);
                GenomeMetadata meta = normalizeRobustMetadata(json.get<GenomeMetadata>());
                if (contentHash.empty()) {
                    contentHash = computeContentHash(genome, meta);
                    persistGenomeHash(id, contentHash);
                }

                genomes_[id] = genome;
                metadata_[id] = meta;

                const auto existingHash = hashToId_.find(contentHash);
                if (existingHash == hashToId_.end()) {
                    hashToId_[contentHash] = id;
                    idToHash_[id] = contentHash;
                }
                else {
                    const GenomeId existingId = existingHash->second;
                    auto existingMeta = metadata_.find(existingId);
                    if (existingMeta == metadata_.end()
                        || effectiveRobustFitness(meta)
                            > effectiveRobustFitness(existingMeta->second)) {
                        idToHash_.erase(existingId);
                        existingHash->second = id;
                        idToHash_[id] = contentHash;
                    }
                }
                loadedCount++;
            }
            catch (const std::exception& e) {
                spdlog::warn(
                    "GenomeRepository: Failed to parse metadata for {}: {}", idStr, e.what());
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

void GenomeRepository::persistGenome(
    GenomeId id, const Genome& genome, const GenomeMetadata& meta, const std::string& contentHash)
{
    const std::string idStr = id.toString();
    nlohmann::json metaJson = meta;

    execDb(*db_, "persistGenome", [&](sqlite::database& db) {
        db << "INSERT OR REPLACE INTO genomes (id, weights, metadata_json, content_hash) "
              "VALUES (?, ?, ?, ?)"
           << idStr << genome.weights << metaJson.dump() << contentHash;
    });
}

void GenomeRepository::persistGenomeHash(GenomeId id, const std::string& contentHash)
{
    execDb(*db_, "persistGenomeHash", [&](sqlite::database& db) {
        db << "UPDATE genomes SET content_hash = ? WHERE id = ?" << contentHash << id.toString();
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

std::string GenomeRepository::computeContentHash(const Genome& genome, const GenomeMetadata& meta)
{
    constexpr uint64_t FNV_OFFSET_BASIS = 1469598103934665603ull;
    uint64_t hash = FNV_OFFSET_BASIS;

    hashValue(hash, static_cast<int>(meta.scenarioId));
    const int organismType =
        meta.organismType.has_value() ? static_cast<int>(meta.organismType.value()) : -1;
    hashValue(hash, organismType);
    hashString(hash, meta.brainKind.value_or(""));
    hashString(hash, meta.brainVariant.value_or(""));

    const uint64_t weightCount = genome.weights.size();
    hashValue(hash, weightCount);
    if (!genome.weights.empty()) {
        hashBytes(hash, genome.weights.data(), genome.weights.size() * sizeof(WeightType));
    }

    return toHexString(hash);
}

void GenomeRepository::store(GenomeId id, const Genome& genome, const GenomeMetadata& meta)
{
    if (id == INVALID_GENOME_ID) {
        id = UUID::generate();
    }

    std::lock_guard<std::mutex> lock(*mutex_);
    const GenomeMetadata normalizedMeta = normalizeRobustMetadata(meta);
    const std::string contentHash = computeContentHash(genome, normalizedMeta);

    const auto oldHashIt = idToHash_.find(id);
    if (oldHashIt != idToHash_.end() && oldHashIt->second != contentHash) {
        const auto mappedHash = hashToId_.find(oldHashIt->second);
        if (mappedHash != hashToId_.end() && mappedHash->second == id) {
            hashToId_.erase(mappedHash);
        }
    }

    genomes_[id] = genome;
    metadata_[id] = normalizedMeta;
    hashToId_[contentHash] = id;
    idToHash_[id] = contentHash;

    if (db_) {
        persistGenome(id, genome, normalizedMeta, contentHash);
    }
}

GenomeRepository::StoreByHashResult GenomeRepository::storeOrUpdateByHash(
    const Genome& genome, const GenomeMetadata& meta, std::optional<GenomeId> preferredId)
{
    std::lock_guard<std::mutex> lock(*mutex_);
    const GenomeMetadata normalizedMeta = normalizeRobustMetadata(meta);
    const std::string contentHash = computeContentHash(genome, normalizedMeta);

    const auto existing = hashToId_.find(contentHash);
    if (existing != hashToId_.end()) {
        const GenomeId existingId = existing->second;
        const auto existingMetaIt = metadata_.find(existingId);
        const GenomeMetadata mergedMeta = existingMetaIt != metadata_.end()
            ? mergeMetadata(existingMetaIt->second, normalizedMeta)
            : normalizedMeta;
        genomes_[existingId] = genome;
        metadata_[existingId] = mergedMeta;
        idToHash_[existingId] = contentHash;
        if (db_) {
            persistGenome(existingId, genome, mergedMeta, contentHash);
        }
        return StoreByHashResult{
            .id = existingId,
            .inserted = false,
            .deduplicated = true,
        };
    }

    GenomeId id = preferredId.has_value() ? preferredId.value() : UUID::generate();
    if (id == INVALID_GENOME_ID || genomes_.find(id) != genomes_.end()) {
        id = UUID::generate();
    }
    while (genomes_.find(id) != genomes_.end()) {
        id = UUID::generate();
    }

    genomes_[id] = genome;
    metadata_[id] = normalizedMeta;
    hashToId_[contentHash] = id;
    idToHash_[id] = contentHash;

    if (db_) {
        persistGenome(id, genome, normalizedMeta, contentHash);
    }

    return StoreByHashResult{
        .id = id,
        .inserted = true,
        .deduplicated = false,
    };
}

size_t GenomeRepository::pruneManagedByFitness(size_t maxManagedGenomes)
{
    if (maxManagedGenomes == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(*mutex_);

    std::unordered_map<ManagedGenomeBucketKey, std::vector<GenomeId>, ManagedGenomeBucketKeyHash>
        managedBucketToIds;
    managedBucketToIds.reserve(metadata_.size());
    for (const auto& [id, meta] : metadata_) {
        if (meta.trainingSessionId.has_value()) {
            const ManagedGenomeBucketKey key{
                .organismType = meta.organismType.has_value()
                    ? static_cast<int>(meta.organismType.value())
                    : -1,
                .brainKind = meta.brainKind.value_or(""),
            };
            managedBucketToIds[key].push_back(id);
        }
    }

    size_t removed = 0;
    for (auto& [bucket, managedIds] : managedBucketToIds) {
        (void)bucket;
        if (managedIds.size() <= maxManagedGenomes) {
            continue;
        }

        std::sort(managedIds.begin(), managedIds.end(), [this](GenomeId left, GenomeId right) {
            const auto leftMetaIt = metadata_.find(left);
            const auto rightMetaIt = metadata_.find(right);
            DIRTSIM_ASSERT(
                leftMetaIt != metadata_.end(), "GenomeRepository: Missing left metadata");
            DIRTSIM_ASSERT(
                rightMetaIt != metadata_.end(), "GenomeRepository: Missing right metadata");

            const auto& leftMeta = leftMetaIt->second;
            const auto& rightMeta = rightMetaIt->second;
            const double leftScore = effectiveRobustFitness(leftMeta);
            const double rightScore = effectiveRobustFitness(rightMeta);
            if (leftScore != rightScore) {
                return leftScore < rightScore;
            }
            if (leftMeta.createdTimestamp != rightMeta.createdTimestamp) {
                return leftMeta.createdTimestamp < rightMeta.createdTimestamp;
            }
            return left.toString() < right.toString();
        });

        const size_t targetRemovals = managedIds.size() - maxManagedGenomes;
        size_t removedFromBucket = 0;
        for (GenomeId id : managedIds) {
            if (removedFromBucket >= targetRemovals) {
                break;
            }
            if (bestId_.has_value() && bestId_.value() == id) {
                continue;
            }
            removeNoLock(id);
            removedFromBucket++;
            removed++;
        }
    }

    return removed;
}

bool GenomeRepository::exists(GenomeId id) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return genomes_.find(id) != genomes_.end();
}

std::optional<Genome> GenomeRepository::get(GenomeId id) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    auto it = genomes_.find(id);
    if (it == genomes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<GenomeMetadata> GenomeRepository::getMetadata(GenomeId id) const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    auto it = metadata_.find(id);
    if (it == metadata_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::pair<GenomeId, GenomeMetadata>> GenomeRepository::list() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    std::vector<std::pair<GenomeId, GenomeMetadata>> result;
    result.reserve(metadata_.size());
    for (const auto& [id, meta] : metadata_) {
        result.emplace_back(id, meta);
    }
    return result;
}

void GenomeRepository::remove(GenomeId id)
{
    std::lock_guard<std::mutex> lock(*mutex_);
    removeNoLock(id);
}

void GenomeRepository::clear()
{
    std::lock_guard<std::mutex> lock(*mutex_);
    genomes_.clear();
    hashToId_.clear();
    idToHash_.clear();
    metadata_.clear();
    bestId_ = std::nullopt;

    if (db_) {
        clearDb();
    }
}

void GenomeRepository::markAsBest(GenomeId id)
{
    std::lock_guard<std::mutex> lock(*mutex_);
    if (genomes_.find(id) != genomes_.end()) {
        bestId_ = id;
        if (db_) {
            persistBestId();
        }
    }
}

std::optional<GenomeId> GenomeRepository::getBestId() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return bestId_;
}

std::optional<Genome> GenomeRepository::getBest() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    if (!bestId_) {
        return std::nullopt;
    }
    auto it = genomes_.find(*bestId_);
    if (it == genomes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

size_t GenomeRepository::count() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return genomes_.size();
}

bool GenomeRepository::empty() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return genomes_.empty();
}

bool GenomeRepository::isPersistent() const
{
    std::lock_guard<std::mutex> lock(*mutex_);
    return db_ != nullptr;
}

void GenomeRepository::removeNoLock(GenomeId id)
{
    genomes_.erase(id);
    metadata_.erase(id);

    const auto hashIt = idToHash_.find(id);
    if (hashIt != idToHash_.end()) {
        const auto reverseHashIt = hashToId_.find(hashIt->second);
        if (reverseHashIt != hashToId_.end() && reverseHashIt->second == id) {
            hashToId_.erase(reverseHashIt);
        }
        idToHash_.erase(hashIt);
    }

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

} // namespace DirtSim
