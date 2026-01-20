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
Result<std::monostate, std::string> execDb(sqlite::database& db, const char* operation, Func&& func)
{
    try {
        func(db);
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }
    catch (const sqlite::sqlite_exception& e) {
        std::string message = "TrainingResultRepository: ";
        message += operation;
        message += " failed: ";
        message += e.what();
        message += " (code ";
        message += std::to_string(e.get_code());
        message += ")";
        spdlog::error("{}", message);
        return Result<std::monostate, std::string>::error(std::move(message));
    }
    catch (const std::exception& e) {
        std::string message = "TrainingResultRepository: ";
        message += operation;
        message += " failed: ";
        message += e.what();
        spdlog::error("{}", message);
        return Result<std::monostate, std::string>::error(std::move(message));
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

Result<bool, std::string> TrainingResultRepository::exists(GenomeId trainingSessionId) const
{
    if (!db_) {
        const bool exists = std::any_of(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& result) {
                return result.summary.trainingSessionId == trainingSessionId;
            });
        return Result<bool, std::string>::okay(exists);
    }

    return existsInDb(trainingSessionId);
}

Result<bool, std::string> TrainingResultRepository::existsInDb(GenomeId trainingSessionId) const
{
    bool exists = false;
    auto result = execDb(*db_, "exists", [&](sqlite::database& db) {
        int count = 0;
        db << "SELECT COUNT(1) FROM training_results WHERE training_session_id = ?"
           << trainingSessionId.toString()
            >> count;
        exists = count > 0;
    });
    if (result.isError()) {
        return Result<bool, std::string>::error(result.errorValue());
    }
    return Result<bool, std::string>::okay(exists);
}

Result<std::monostate, std::string> TrainingResultRepository::store(
    const Api::TrainingResult& result)
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
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    nlohmann::json summaryJson;
    nlohmann::json candidatesJson;
    try {
        summaryJson = result.summary;
        candidatesJson = result.candidates;
    }
    catch (const std::exception& e) {
        std::string message = "TrainingResultRepository: serialize failed: ";
        message += e.what();
        spdlog::error("{}", message);
        return Result<std::monostate, std::string>::error(std::move(message));
    }
    const int candidateCount = static_cast<int>(result.candidates.size());
    const int64_t createdAt = currentEpochSeconds();

    return execDb(*db_, "store", [&](sqlite::database& db) {
        db << R"(
            INSERT OR REPLACE INTO training_results
                (training_session_id, summary_json, candidates_json, candidate_count, created_at)
            VALUES (?, ?, ?, ?, ?)
        )" << result.summary.trainingSessionId.toString()
           << summaryJson.dump() << candidatesJson.dump() << candidateCount << createdAt;
    });
}

Result<std::optional<Api::TrainingResult>, std::string> TrainingResultRepository::get(
    GenomeId trainingSessionId) const
{
    if (!db_) {
        auto it = std::find_if(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& existing) {
                return existing.summary.trainingSessionId == trainingSessionId;
            });
        if (it == inMemoryResults_.end()) {
            return Result<std::optional<Api::TrainingResult>, std::string>::okay(std::nullopt);
        }
        return Result<std::optional<Api::TrainingResult>, std::string>::okay(*it);
    }

    return getFromDb(trainingSessionId);
}

Result<std::optional<Api::TrainingResult>, std::string> TrainingResultRepository::getFromDb(
    GenomeId trainingSessionId) const
{
    std::optional<Api::TrainingResult> result;
    std::string parseError;
    auto dbResult = execDb(*db_, "get", [&](sqlite::database& db) {
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
                      parseError = "TrainingResultRepository: Failed to parse training result ";
                      parseError += trainingSessionId.toShortString();
                      parseError += ": ";
                      parseError += e.what();
                  }
              };
    });
    if (dbResult.isError()) {
        return Result<std::optional<Api::TrainingResult>, std::string>::error(
            dbResult.errorValue());
    }
    if (!parseError.empty()) {
        spdlog::error("{}", parseError);
        return Result<std::optional<Api::TrainingResult>, std::string>::error(
            std::move(parseError));
    }
    return Result<std::optional<Api::TrainingResult>, std::string>::okay(result);
}

Result<std::vector<Api::TrainingResultList::Entry>, std::string> TrainingResultRepository::list()
    const
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
        return Result<std::vector<Api::TrainingResultList::Entry>, std::string>::okay(
            std::move(entries));
    }

    return listFromDb();
}

Result<std::vector<Api::TrainingResultList::Entry>, std::string> TrainingResultRepository::
    listFromDb() const
{
    std::vector<Api::TrainingResultList::Entry> entries;
    std::string parseError;
    auto dbResult = execDb(*db_, "list", [&](sqlite::database& db) {
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
                      parseError = "TrainingResultRepository: Failed to parse list entry: ";
                      parseError += e.what();
                  }
              };
    });
    if (dbResult.isError()) {
        return Result<std::vector<Api::TrainingResultList::Entry>, std::string>::error(
            dbResult.errorValue());
    }
    if (!parseError.empty()) {
        spdlog::error("{}", parseError);
        return Result<std::vector<Api::TrainingResultList::Entry>, std::string>::error(
            std::move(parseError));
    }
    return Result<std::vector<Api::TrainingResultList::Entry>, std::string>::okay(
        std::move(entries));
}

Result<bool, std::string> TrainingResultRepository::remove(GenomeId trainingSessionId)
{
    if (!db_) {
        auto it = std::find_if(
            inMemoryResults_.begin(),
            inMemoryResults_.end(),
            [&](const Api::TrainingResult& existing) {
                return existing.summary.trainingSessionId == trainingSessionId;
            });
        if (it == inMemoryResults_.end()) {
            return Result<bool, std::string>::okay(false);
        }
        inMemoryResults_.erase(it);
        return Result<bool, std::string>::okay(true);
    }

    return removeFromDb(trainingSessionId);
}

Result<bool, std::string> TrainingResultRepository::removeFromDb(GenomeId trainingSessionId) const
{
    int changes = 0;
    auto dbResult = execDb(*db_, "remove", [&](sqlite::database& db) {
        db << "DELETE FROM training_results WHERE training_session_id = ?"
           << trainingSessionId.toString();
        db << "SELECT changes()" >> changes;
    });
    if (dbResult.isError()) {
        return Result<bool, std::string>::error(dbResult.errorValue());
    }
    return Result<bool, std::string>::okay(changes > 0);
}

bool TrainingResultRepository::isPersistent() const
{
    return db_ != nullptr;
}

} // namespace Server
} // namespace DirtSim
