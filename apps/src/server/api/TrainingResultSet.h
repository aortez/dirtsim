#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "TrainingResult.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"

#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultSet {

DEFINE_API_NAME(TrainingResultSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    TrainingResult result;
    bool overwrite = true;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool stored = true;
    bool overwritten = false;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<2>;
};

API_STANDARD_TYPES();

} // namespace TrainingResultSet
} // namespace Api
} // namespace DirtSim
