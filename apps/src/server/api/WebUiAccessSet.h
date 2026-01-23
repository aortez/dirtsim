#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace WebUiAccessSet {

DEFINE_API_NAME(WebUiAccessSet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    bool enabled = false;
    std::string token;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool enabled = false;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace WebUiAccessSet
} // namespace Api
} // namespace DirtSim
