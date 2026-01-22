#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultDelete {

DEFINE_API_NAME(TrainingResultDelete);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    GenomeId trainingSessionId{};

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool success = false; // True if the training result existed and was deleted.

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace TrainingResultDelete
} // namespace Api
} // namespace DirtSim
