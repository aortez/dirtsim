#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace SynthKeyPress {

DEFINE_API_NAME(SynthKeyPress);

struct Command {
    int key_index = 0;
    bool is_black = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    int key_index = 0;
    bool is_black = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SynthKeyPress
} // namespace UiApi
} // namespace DirtSim
