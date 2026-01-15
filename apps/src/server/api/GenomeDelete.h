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

namespace GenomeDelete {

DEFINE_API_NAME(GenomeDelete);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    GenomeId id{};

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool success = false; // True if genome existed and was deleted.

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace GenomeDelete
} // namespace Api
} // namespace DirtSim
