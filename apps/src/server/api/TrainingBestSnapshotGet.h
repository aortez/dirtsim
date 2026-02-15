#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "TrainingBestSnapshot.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"

#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingBestSnapshotGet {

DEFINE_API_NAME(TrainingBestSnapshotGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    bool hasSnapshot = false;
    TrainingBestSnapshot snapshot;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<2>;
};

API_STANDARD_TYPES();

} // namespace TrainingBestSnapshotGet
} // namespace Api
} // namespace DirtSim
