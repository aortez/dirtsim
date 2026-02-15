#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {

enum class TrainingResumePolicy : uint8_t {
    Fresh = 0,
    WarmFromBest = 1,
};

inline void to_json(nlohmann::json& j, const TrainingResumePolicy& policy)
{
    switch (policy) {
        case TrainingResumePolicy::Fresh:
            j = "Fresh";
            return;
        case TrainingResumePolicy::WarmFromBest:
            j = "WarmFromBest";
            return;
    }

    j = "WarmFromBest";
}

inline void from_json(const nlohmann::json& j, TrainingResumePolicy& policy)
{
    if (j.is_number_integer()) {
        const auto value = static_cast<int>(j.get<int64_t>());
        if (value <= static_cast<int>(TrainingResumePolicy::Fresh)) {
            policy = TrainingResumePolicy::Fresh;
            return;
        }
        policy = TrainingResumePolicy::WarmFromBest;
        return;
    }

    if (j.is_string()) {
        const std::string value = j.get<std::string>();
        if (value == "Fresh") {
            policy = TrainingResumePolicy::Fresh;
            return;
        }
        policy = TrainingResumePolicy::WarmFromBest;
        return;
    }

    policy = TrainingResumePolicy::WarmFromBest;
}

} // namespace DirtSim
