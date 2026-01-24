#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace GenomeDetailLoad {

DEFINE_API_NAME(GenomeDetailLoad);

struct Command {
    GenomeId id{};

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool queued = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace GenomeDetailLoad
} // namespace UiApi
} // namespace DirtSim
