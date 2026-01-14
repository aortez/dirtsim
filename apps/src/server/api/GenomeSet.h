#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace GenomeSet {

DEFINE_API_NAME(GenomeSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    GenomeId id{};                          // UUID for this genome (required).
    std::vector<double> weights;            // Genome weights.
    std::optional<GenomeMetadata> metadata; // Optional metadata.

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    bool success = true;
    bool overwritten = false; // True if genome existed and was replaced.

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace GenomeSet
} // namespace Api
} // namespace DirtSim
