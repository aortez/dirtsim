#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace UiApi {

namespace WebSocketAccessSet {

DEFINE_API_NAME(WebSocketAccessSet);

struct Command {
    bool enabled = false;
    std::string token;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

struct Okay {
    bool enabled = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace WebSocketAccessSet
} // namespace UiApi
} // namespace DirtSim
