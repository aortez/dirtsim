#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

enum class SearchProgressEvent : uint8_t {
    Unknown = 0,
    RootInitialized = 1,
    ExpandedAlive = 2,
    Backtracked = 3,
    PrunedDead = 4,
    PrunedStalled = 5,
    PrunedVelocityStuck = 6,
    CompletedBudgetExceeded = 7,
    CompletedExhausted = 8,
    CompletedMilestoneReached = 9,
    Stopped = 10,
    Error = 11,
    PrunedBelowScreen = 12,
};

struct SearchProgress {
    bool paused = false;
    uint64_t bestFrontier = 0;
    uint64_t currentGameplayFrame = 0;
    SearchProgressEvent lastSearchEvent = SearchProgressEvent::Unknown;
    uint64_t searchedNodeCount = 0;
    uint64_t groundedVerticalJumpPriorityActionCount = 0;

    static constexpr const char* name() { return "SearchProgress"; }
    using serialize = zpp::bits::members<6>;
};

void to_json(nlohmann::json& j, const SearchProgress& value);
void from_json(const nlohmann::json& j, SearchProgress& value);

namespace SearchProgressGet {

DEFINE_API_NAME(SearchProgressGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    SearchProgress progress;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace SearchProgressGet
} // namespace Api
} // namespace DirtSim
