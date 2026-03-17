#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace NesFrameDelaySet {

DEFINE_API_NAME(NesFrameDelaySet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    bool enabled = false;
    double frame_delay_ms = 0.0;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool enabled = false;
    double frame_delay_ms = 0.0;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace NesFrameDelaySet
} // namespace Api
} // namespace DirtSim
