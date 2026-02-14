#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace SynthKeyEvent {

DEFINE_API_NAME(SynthKeyEvent);

struct Command {
    int key_index = 0;
    bool is_black = false;
    bool is_pressed = true;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    int key_index = 0;
    bool is_black = false;
    bool is_pressed = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<3>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SynthKeyEvent
} // namespace UiApi
} // namespace DirtSim
