#include "PlanRepository.h"
#include "core/LoggingChannels.h"
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
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
        std::string message = "PlanRepository: ";
        message += operation;
        message += " failed: ";
        message += e.what();
        message += " (code ";
        message += std::to_string(e.get_code());
        message += ")";
        SLOG_ERROR("{}", message);
        return Result<std::monostate, std::string>::error(std::move(message));
    }
    catch (const std::exception& e) {
        std::string message = "PlanRepository: ";
        message += operation;
        message += " failed: ";
        message += e.what();
        SLOG_ERROR("{}", message);
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

PlanRepository::PlanRepository() = default;

PlanRepository::PlanRepository(const std::filesystem::path& dbPath)
    : db_(std::make_unique<sqlite::database>(dbPath.string()))
{
    SLOG_INFO("PlanRepository: Opening database at {}", dbPath.string());
    initSchema();
}

PlanRepository::~PlanRepository() = default;

PlanRepository::PlanRepository(PlanRepository&&) noexcept = default;
PlanRepository& PlanRepository::operator=(PlanRepository&&) noexcept = default;

void PlanRepository::initSchema()
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
        CREATE TABLE IF NOT EXISTS plans (
            plan_id TEXT PRIMARY KEY,
            summary_json TEXT NOT NULL,
            frames_json TEXT NOT NULL,
            created_at INTEGER NOT NULL
        )
    )";

    int existingVersion = 0;
    *db_ << "SELECT version FROM schema_version LIMIT 1" >>
        [&](int version) { existingVersion = version; };

    if (existingVersion == 0) {
        *db_ << "INSERT INTO schema_version (version) VALUES (?)" << kSchemaVersion;
        SLOG_INFO("PlanRepository: Initialized schema version {}", kSchemaVersion);
    }
    else if (existingVersion != kSchemaVersion) {
        SLOG_WARN(
            "PlanRepository: Schema version mismatch (db={}, code={})",
            existingVersion,
            kSchemaVersion);
    }
}

Result<std::monostate, std::string> PlanRepository::store(const Api::Plan& plan)
{
    if (!db_) {
        auto it = std::find_if(
            inMemoryPlans_.begin(), inMemoryPlans_.end(), [&](const Api::Plan& existing) {
                return existing.summary.id == plan.summary.id;
            });
        if (it != inMemoryPlans_.end()) {
            *it = plan;
        }
        else {
            inMemoryPlans_.push_back(plan);
        }
        return Result<std::monostate, std::string>::okay(std::monostate{});
    }

    nlohmann::json summaryJson;
    nlohmann::json framesJson;
    try {
        summaryJson = plan.summary;
        framesJson = plan.frames;
    }
    catch (const std::exception& e) {
        std::string message = "PlanRepository: serialize failed: ";
        message += e.what();
        SLOG_ERROR("{}", message);
        return Result<std::monostate, std::string>::error(std::move(message));
    }

    return execDb(*db_, "store", [&](sqlite::database& db) {
        db << R"(
            INSERT OR REPLACE INTO plans
                (plan_id, summary_json, frames_json, created_at)
            VALUES (?, ?, ?, ?)
        )" << plan.summary.id.toString()
           << summaryJson.dump() << framesJson.dump() << currentEpochSeconds();
    });
}

Result<std::optional<Api::Plan>, std::string> PlanRepository::get(UUID planId) const
{
    if (!db_) {
        auto it =
            std::find_if(inMemoryPlans_.begin(), inMemoryPlans_.end(), [&](const Api::Plan& plan) {
                return plan.summary.id == planId;
            });
        if (it == inMemoryPlans_.end()) {
            return Result<std::optional<Api::Plan>, std::string>::okay(std::nullopt);
        }
        return Result<std::optional<Api::Plan>, std::string>::okay(*it);
    }

    return getFromDb(planId);
}

Result<std::optional<Api::Plan>, std::string> PlanRepository::getFromDb(UUID planId) const
{
    std::optional<Api::Plan> result;
    std::string parseError;
    auto dbResult = execDb(*db_, "get", [&](sqlite::database& db) {
        db << "SELECT summary_json, frames_json FROM plans WHERE plan_id = ?" << planId.toString()
            >> [&](std::string summaryJson, std::string framesJson) {
                  try {
                      Api::Plan parsed;
                      parsed.summary = nlohmann::json::parse(summaryJson).get<Api::PlanSummary>();
                      parsed.frames =
                          nlohmann::json::parse(framesJson).get<std::vector<PlayerControlFrame>>();
                      result = std::move(parsed);
                  }
                  catch (const std::exception& e) {
                      parseError = "PlanRepository: Failed to parse plan ";
                      parseError += planId.toShortString();
                      parseError += ": ";
                      parseError += e.what();
                  }
              };
    });
    if (dbResult.isError()) {
        return Result<std::optional<Api::Plan>, std::string>::error(dbResult.errorValue());
    }
    if (!parseError.empty()) {
        SLOG_ERROR("{}", parseError);
        return Result<std::optional<Api::Plan>, std::string>::error(std::move(parseError));
    }
    return Result<std::optional<Api::Plan>, std::string>::okay(result);
}

Result<std::vector<Api::PlanList::Entry>, std::string> PlanRepository::list() const
{
    if (!db_) {
        std::vector<Api::PlanList::Entry> entries;
        entries.reserve(inMemoryPlans_.size());
        for (const auto& plan : inMemoryPlans_) {
            entries.push_back(Api::PlanList::Entry{ .summary = plan.summary });
        }
        return Result<std::vector<Api::PlanList::Entry>, std::string>::okay(std::move(entries));
    }

    return listFromDb();
}

Result<std::vector<Api::PlanList::Entry>, std::string> PlanRepository::listFromDb() const
{
    std::vector<Api::PlanList::Entry> entries;
    std::string parseError;
    auto dbResult = execDb(*db_, "list", [&](sqlite::database& db) {
        db << "SELECT summary_json FROM plans ORDER BY created_at DESC" >>
            [&](std::string summaryJson) {
                try {
                    entries.push_back(
                        Api::PlanList::Entry{
                            .summary = nlohmann::json::parse(summaryJson).get<Api::PlanSummary>(),
                        });
                }
                catch (const std::exception& e) {
                    parseError = "PlanRepository: Failed to parse list entry: ";
                    parseError += e.what();
                }
            };
    });
    if (dbResult.isError()) {
        return Result<std::vector<Api::PlanList::Entry>, std::string>::error(dbResult.errorValue());
    }
    if (!parseError.empty()) {
        SLOG_ERROR("{}", parseError);
        return Result<std::vector<Api::PlanList::Entry>, std::string>::error(std::move(parseError));
    }
    return Result<std::vector<Api::PlanList::Entry>, std::string>::okay(std::move(entries));
}

Result<bool, std::string> PlanRepository::remove(UUID planId)
{
    if (!db_) {
        auto it =
            std::find_if(inMemoryPlans_.begin(), inMemoryPlans_.end(), [&](const Api::Plan& plan) {
                return plan.summary.id == planId;
            });
        if (it == inMemoryPlans_.end()) {
            return Result<bool, std::string>::okay(false);
        }
        inMemoryPlans_.erase(it);
        return Result<bool, std::string>::okay(true);
    }

    return removeFromDb(planId);
}

Result<bool, std::string> PlanRepository::removeFromDb(UUID planId) const
{
    int changes = 0;
    auto dbResult = execDb(*db_, "remove", [&](sqlite::database& db) {
        db << "DELETE FROM plans WHERE plan_id = ?" << planId.toString();
        db << "SELECT changes()" >> changes;
    });
    if (dbResult.isError()) {
        return Result<bool, std::string>::error(dbResult.errorValue());
    }
    return Result<bool, std::string>::okay(changes > 0);
}

bool PlanRepository::isPersistent() const
{
    return db_ != nullptr;
}

} // namespace Server
} // namespace DirtSim
