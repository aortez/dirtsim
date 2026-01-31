#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace TrainingResultSave {

DEFINE_API_NAME(TrainingResultSave);

struct Command {
    std::optional<int> count;
    bool restart = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool queued = false;
    int savedCount = 0;
    int discardedCount = 0;
    std::vector<GenomeId> savedIds;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<4>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrainingResultSave
} // namespace UiApi
} // namespace DirtSim
