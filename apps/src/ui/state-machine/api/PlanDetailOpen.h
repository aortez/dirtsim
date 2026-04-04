#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/UUID.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace PlanDetailOpen {

DEFINE_API_NAME(PlanDetailOpen);

struct Command {
    std::optional<UUID> id;
    int index = 0;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool opened = true;
    UUID id{};

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace PlanDetailOpen
} // namespace UiApi
} // namespace DirtSim
