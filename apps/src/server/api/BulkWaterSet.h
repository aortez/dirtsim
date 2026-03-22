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

namespace BulkWaterSet {

DEFINE_API_NAME(BulkWaterSet);

struct Command {
    API_COMMAND_T(std::monostate);
    int x;
    int y;
    double amount = 1.0;

    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace BulkWaterSet
} // namespace Api
} // namespace DirtSim
