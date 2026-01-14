#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/MaterialType.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace CellSet {

DEFINE_API_NAME(CellSet);

struct Command {
    API_COMMAND_T(std::monostate);
    int x;
    int y;
    Material::EnumType material;
    double fill = 1.0;

    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<4>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace CellSet
} // namespace Api
} // namespace DirtSim
