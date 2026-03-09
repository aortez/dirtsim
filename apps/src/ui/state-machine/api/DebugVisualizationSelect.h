#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include "ui/rendering/DebugVisualizationMode.h"
#include <nlohmann/json.hpp>

namespace DirtSim {
namespace UiApi {

namespace DebugVisualizationSelect {

DEFINE_API_NAME(DebugVisualizationSelect);

struct Command {
    Ui::DebugVisualizationMode mode;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

struct Okay {
    Ui::DebugVisualizationMode mode;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace DebugVisualizationSelect
} // namespace UiApi
} // namespace DirtSim
