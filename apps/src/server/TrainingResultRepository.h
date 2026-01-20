#pragma once

#include "core/Result.h"
#include "server/api/TrainingResult.h"
#include "server/api/TrainingResultList.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace sqlite {
class database;
}

namespace DirtSim {
namespace Server {

class TrainingResultRepository {
public:
    TrainingResultRepository();
    explicit TrainingResultRepository(const std::filesystem::path& dbPath);
    ~TrainingResultRepository();

    TrainingResultRepository(TrainingResultRepository&&) noexcept;
    TrainingResultRepository& operator=(TrainingResultRepository&&) noexcept;
    TrainingResultRepository(const TrainingResultRepository&) = delete;
    TrainingResultRepository& operator=(const TrainingResultRepository&) = delete;

    Result<std::monostate, std::string> store(const Api::TrainingResult& result);
    Result<bool, std::string> exists(GenomeId trainingSessionId) const;
    Result<std::optional<Api::TrainingResult>, std::string> get(GenomeId trainingSessionId) const;
    Result<std::vector<Api::TrainingResultList::Entry>, std::string> list() const;
    Result<bool, std::string> remove(GenomeId trainingSessionId);
    bool isPersistent() const;

private:
    std::unique_ptr<sqlite::database> db_;
    std::vector<Api::TrainingResult> inMemoryResults_;

    void initSchema();
    Result<bool, std::string> existsInDb(GenomeId trainingSessionId) const;
    Result<std::optional<Api::TrainingResult>, std::string> getFromDb(
        GenomeId trainingSessionId) const;
    Result<std::vector<Api::TrainingResultList::Entry>, std::string> listFromDb() const;
    Result<bool, std::string> removeFromDb(GenomeId trainingSessionId) const;
};

} // namespace Server
} // namespace DirtSim
