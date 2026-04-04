#pragma once

#include "core/Result.h"
#include "server/api/Plan.h"
#include "server/api/PlanList.h"
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

class PlanRepository {
public:
    PlanRepository();
    explicit PlanRepository(const std::filesystem::path& dbPath);
    ~PlanRepository();

    PlanRepository(PlanRepository&&) noexcept;
    PlanRepository& operator=(PlanRepository&&) noexcept;
    PlanRepository(const PlanRepository&) = delete;
    PlanRepository& operator=(const PlanRepository&) = delete;

    Result<std::monostate, std::string> store(const Api::Plan& plan);
    Result<std::optional<Api::Plan>, std::string> get(UUID planId) const;
    Result<std::vector<Api::PlanList::Entry>, std::string> list() const;
    Result<bool, std::string> remove(UUID planId);
    bool isPersistent() const;

private:
    std::unique_ptr<sqlite::database> db_;
    std::vector<Api::Plan> inMemoryPlans_;

    void initSchema();
    Result<std::optional<Api::Plan>, std::string> getFromDb(UUID planId) const;
    Result<std::vector<Api::PlanList::Entry>, std::string> listFromDb() const;
    Result<bool, std::string> removeFromDb(UUID planId) const;
};

} // namespace Server
} // namespace DirtSim
