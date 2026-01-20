#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace PlantSeed {

DEFINE_API_NAME(PlantSeed);

struct Command {
    std::optional<int> x;
    std::optional<int> y;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace PlantSeed
} // namespace UiApi
} // namespace DirtSim
