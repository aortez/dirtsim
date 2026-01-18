#pragma once

#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace DirtSim {
namespace Client {

struct FunctionalTestSummary {
    std::string name;
    int64_t duration_ms = 0;
    Result<std::monostate, std::string> result;

    nlohmann::json toJson() const;
};

class FunctionalTestRunner {
public:
    FunctionalTestSummary runCanExit(
        const std::string& uiAddress, const std::string& serverAddress, int timeoutMs);

private:
    Result<std::monostate, std::string> restartServicesAndVerify(
        const std::string& uiAddress, const std::string& serverAddress, int timeoutMs);
};

} // namespace Client
} // namespace DirtSim
