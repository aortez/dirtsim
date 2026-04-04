#pragma once

#include "Plan.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

enum class SearchCompletionReason : uint8_t {
    StoppedByUser = 0,
    ReachedSegmentLimit = 1,
    NoFurtherProgress = 2,
    SearchError = 3,
};

struct SearchCompleted {
    SearchCompletionReason reason = SearchCompletionReason::ReachedSegmentLimit;
    std::optional<PlanSummary> summary = std::nullopt;
    std::string errorMessage;

    static constexpr const char* name() { return "SearchCompleted"; }
    using serialize = zpp::bits::members<3>;
};

void to_json(nlohmann::json& j, const SearchCompleted& value);
void from_json(const nlohmann::json& j, SearchCompleted& value);

} // namespace Api
} // namespace DirtSim
