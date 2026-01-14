#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace SeedAdd {

DEFINE_API_NAME(SeedAdd);

struct Command {
    API_COMMAND_T(std::monostate);
    int x;
    int y;
    std::optional<std::string> genome_id; // UUID from GenomeRepository for tree brain.

    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SeedAdd
} // namespace Api
} // namespace DirtSim
