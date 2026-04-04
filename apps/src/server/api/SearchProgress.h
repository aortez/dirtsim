#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

struct SearchProgress {
    bool paused = false;
    uint32_t attemptIndex = 0;
    uint32_t backtrackCount = 0;
    uint32_t beamWidth = 0;
    uint64_t bestFrontier = 0;
    uint32_t candidateCount = 0;
    uint32_t candidateIndex = 0;
    uint64_t elapsedFrames = 0;
    uint64_t expandedNodeCount = 0;
    uint32_t segmentIndex = 0;
    uint32_t maxSegments = 0;
    uint32_t stepIndex = 0;
    uint32_t maxSteps = 0;

    static constexpr const char* name() { return "SearchProgress"; }
    using serialize = zpp::bits::members<13>;
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
