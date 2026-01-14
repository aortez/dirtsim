#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace WorldResize {

DEFINE_API_NAME(WorldResize);

struct Command {
    API_COMMAND_T(std::monostate);
    int16_t width = 28;
    int16_t height = 28;

    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace WorldResize
} // namespace Api
} // namespace DirtSim