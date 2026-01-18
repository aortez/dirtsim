#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "TrainingResultAvailable.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"

#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultGet {

DEFINE_API_NAME(TrainingResultGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    GenomeId trainingSessionId{};

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    TrainingResultAvailable::Summary summary;
    std::vector<TrainingResultAvailable::Candidate> candidates;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<2>;
};

API_STANDARD_TYPES();

} // namespace TrainingResultGet
} // namespace Api
} // namespace DirtSim
