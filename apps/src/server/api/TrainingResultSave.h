#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"

#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultSave {

DEFINE_API_NAME(TrainingResultSave);

struct Okay;

// Empty ids saves all candidates from the pending training result.
struct Command {
    std::vector<GenomeId> ids;
    bool restart = false;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    int savedCount = 0;
    int discardedCount = 0;
    std::vector<GenomeId> savedIds;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<3>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrainingResultSave
} // namespace Api
} // namespace DirtSim
