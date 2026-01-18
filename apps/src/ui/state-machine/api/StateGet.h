#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/ScenarioId.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <string>

namespace DirtSim {
namespace UiApi {

namespace StateGet {

DEFINE_API_NAME(StateGet);

struct Command {
    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

struct Okay {
    std::string state;
    Scenario::EnumType scenario_id = Scenario::EnumType::Empty;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace StateGet
} // namespace UiApi
} // namespace DirtSim
