#include "TrainingResultRepository.h"
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sqlite_modern_cpp.h>

namespace DirtSim {
namespace Server {

namespace {
constexpr int kSchemaVersion = 1;

template <typename Func>
void execDb(sqlite::database& db, const char* operation, Func&& func)
{
    try {
        func(db);
    }
    catch (const sqlite::sqlite_exception& e) {
        spdlog::error(
            "TrainingResultRepository: {} failed: {} (code {})", operation, e.what(), e.get_code());
    }
    catch (const std::exception& e) {
        spdlog::error("TrainingResultRepository: {} failed: {}", operation, e.what());
    }
}

int64_t currentEpochSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

TrainingResultRepository::TrainingResultRepository() = default;

TrainingResultRepository::TrainingResultRepository(const std::filesystem::path& dbPath)
    : db_(std::make_unique<sqlite::database>(dbPath.string()))
{
    spdlog::info("TrainingResultRepository: Opening database at {}", dbPath.string());
    initSchema();
}

TrainingResultRepository::~TrainingResultRepository() = default;

TrainingResultRepository::TrainingResultRepository(TrainingResultRepository&&) noexcept = default;
TrainingResultRepository& TrainingResultRepository::operator=(TrainingResultRepository&&) noexcept =
    default;

void TrainingResultRepository::initSchema()
{
    if (!db_) {
        return;
    }

    *db_ << R"(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY
        )
    )";

    *db_ << R"(
        CREATE TABLE IF NOT EXISTS training_results (
            training_session_id TEXT PRIMARY KEY,
            summary_json TEXT NOT NULL,
            candidates_json TEXT NOT NULL,
            candidate_count INTEGER NOT NULL,
            created_at INTEGER NOT NULL
        )
    )";

    int existingVersion = 0;
    *db_ << "SELECT version FROM schema_version LIMIT 1" >> [&](int v) { existingVersion = v; };

    if (existingVersion == 0) {
        *db_ << "INSERT INTO schema_version (version) VALUES (?)" << kSchemaVersion;
        spdlog::info("TrainingResultRepository: Initialized schema version {}", kSchemaVersion);
    }
    else if (existingVersion != kSchemaVersion) {
        spdlog::warn(
            "TrainingResultRepository: Schema version mismatch (db={}, code={})",
            existingVersion,
            kSchemaVersion);
    }
}

bool TrainingResultRepository::exists(GenomeId trainingSessionId) const
{
    if (!db_) {
        return std::any_of(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& result) {
                return result.summary.trainingSessionId == trainingSessionId;
            });
    }

    return existsInDb(trainingSessionId);
}

bool TrainingResultRepository::existsInDb(GenomeId trainingSessionId) const
{
    bool exists = false;
    execDb(*db_, "exists", [&](sqlite::database& db) {
        int count = 0;
        db << "SELECT COUNT(1) FROM training_results WHERE training_session_id = ?"
           << trainingSessionId.toString()
            >> count;
        exists = count > 0;
    });
    return exists;
}

void TrainingResultRepository::store(const Api::TrainingResult& result)
{
    if (!db_) {
        auto it = std::find_if(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& existing) {
                return existing.summary.trainingSessionId == result.summary.trainingSessionId;
            });
        if (it != inMemoryResults_.end()) {
            *it = result;
        }
        else {
            inMemoryResults_.push_back(result);
        }
        return;
    }

    nlohmann::json summaryJson = result.summary;
    nlohmann::json candidatesJson = result.candidates;
    const int candidateCount = static_cast<int>(result.candidates.size());
    const int64_t createdAt = currentEpochSeconds();

    execDb(*db_, "store", [&](sqlite::database& db) {
        db << R"(
            INSERT OR REPLACE INTO training_results
                (training_session_id, summary_json, candidates_json, candidate_count, created_at)
            VALUES (?, ?, ?, ?, ?)
        )" << result.summary.trainingSessionId.toString()
           << summaryJson.dump() << candidatesJson.dump() << candidateCount << createdAt;
    });
}

std::optional<Api::TrainingResult> TrainingResultRepository::get(GenomeId trainingSessionId) const
{
    if (!db_) {
        auto it = std::find_if(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& existing) {
                return existing.summary.trainingSessionId == trainingSessionId;
            });
        if (it == inMemoryResults_.end()) {
            return std::nullopt;
        }
        return *it;
    }

    return getFromDb(trainingSessionId);
}

std::optional<Api::TrainingResult> TrainingResultRepository::getFromDb(
    GenomeId trainingSessionId) const
{
    std::optional<Api::TrainingResult> result;
    execDb(*db_, "get", [&](sqlite::database& db) {
        db << "SELECT summary_json, candidates_json FROM training_results WHERE "
              "training_session_id = ?"
           << trainingSessionId.toString()
            >> [&](std::string summaryJson, std::string candidatesJson) {
                  try {
                      Api::TrainingResult parsed;
                      auto summaryJ = nlohmann::json::parse(summaryJson);
                      auto candidatesJ = nlohmann::json::parse(candidatesJson);
                      parsed.summary = summaryJ.get<Api::TrainingResult::Summary>();
                      parsed.candidates =
                          candidatesJ.get<std::vector<Api::TrainingResult::Candidate>>();
                      result = std::move(parsed);
                  }
                  catch (const std::exception& e) {
                      spdlog::error(
                          "TrainingResultRepository: Failed to parse training result {}: {}",
                          trainingSessionId.toShortString(),
                          e.what());
                  }
              };
    });
    return result;
}

std::vector<Api::TrainingResultList::Entry> TrainingResultRepository::list() const
{
    if (!db_) {
        std::vector<Api::TrainingResultList::Entry> entries;
        entries.reserve(inMemoryResults_.size());
        for (const auto& result : inMemoryResults_) {
            Api::TrainingResultList::Entry entry;
            entry.summary = result.summary;
            entry.candidateCount = static_cast<int>(result.candidates.size());
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    return listFromDb();
}

std::vector<Api::TrainingResultList::Entry> TrainingResultRepository::listFromDb() const
{
    std::vector<Api::TrainingResultList::Entry> entries;
    execDb(*db_, "list", [&](sqlite::database& db) {
        db << "SELECT summary_json, candidate_count FROM training_results ORDER BY created_at DESC"
            >> [&](std::string summaryJson, int candidateCount) {
                  try {
                      Api::TrainingResultList::Entry entry;
                      auto summaryJ = nlohmann::json::parse(summaryJson);
                      entry.summary = summaryJ.get<Api::TrainingResult::Summary>();
                      entry.candidateCount = candidateCount;
                      entries.push_back(std::move(entry));
                  }
                  catch (const std::exception& e) {
                      spdlog::error(
                          "TrainingResultRepository: Failed to parse list entry: {}", e.what());
                  }
              };
    });
    return entries;
}

bool TrainingResultRepository::remove(GenomeId trainingSessionId)
{
    if (!db_) {
        auto it = std::find_if(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& existing) {
                return existing.summary.trainingSessionId == trainingSessionId;
            });
        if (it == inMemoryResults_.end()) {
            return false;
        }
        inMemoryResults_.erase(it);
        return true;
    }

    return removeFromDb(trainingSessionId);
}

bool TrainingResultRepository::removeFromDb(GenomeId trainingSessionId) const
{
    const bool existed = existsInDb(trainingSessionId);
    if (!existed) {
        return false;
    }

    execDb(*db_, "remove", [&](sqlite::database& db) {
        db << "DELETE FROM training_results WHERE training_session_id = ?"
           << trainingSessionId.toString();
    });
    return true;
}

bool TrainingResultRepository::isPersistent() const
{
    return db_ != nullptr;
}

} // namespace Server
} // namespace DirtSim
