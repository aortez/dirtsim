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

namespace GenomeGet {

DEFINE_API_NAME(GenomeGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    GenomeId id{};

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool found = false;
    GenomeId id{};
    std::vector<double> weights; // Flattened genome weights for serialization.
    GenomeMetadata metadata;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<4>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace GenomeGet
} // namespace Api
} // namespace DirtSim
