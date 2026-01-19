#pragma once

#include "server/api/TrainingResult.h"
#include "server/api/TrainingResultList.h"
#include <filesystem>
#include <memory>
#include <optional>
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

    void store(const Api::TrainingResult& result);
    bool exists(GenomeId trainingSessionId) const;
    std::optional<Api::TrainingResult> get(GenomeId trainingSessionId) const;
    std::vector<Api::TrainingResultList::Entry> list() const;
    bool remove(GenomeId trainingSessionId);
    bool isPersistent() const;

private:
    std::unique_ptr<sqlite::database> db_;
    std::vector<Api::TrainingResult> inMemoryResults_;

    void initSchema();
    bool existsInDb(GenomeId trainingSessionId) const;
    std::optional<Api::TrainingResult> getFromDb(GenomeId trainingSessionId) const;
    std::vector<Api::TrainingResultList::Entry> listFromDb() const;
    bool removeFromDb(GenomeId trainingSessionId) const;
};

} // namespace Server
} // namespace DirtSim
