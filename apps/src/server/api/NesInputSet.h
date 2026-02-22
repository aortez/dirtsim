#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"

#include <cstdint>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace NesInputSet {

DEFINE_API_NAME(NesInputSet);

struct Command {
    uint8_t controller1_mask = 0;

    API_COMMAND_T(std::monostate);
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

using OkayType = std::monostate;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace NesInputSet
} // namespace Api
} // namespace DirtSim
