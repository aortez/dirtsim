#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace AudioApi {
namespace NoteOff {

DEFINE_API_NAME(NoteOff);

struct Okay;

struct Command {
    uint32_t note_id = 0;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool released = false;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace NoteOff
} // namespace AudioApi
} // namespace DirtSim
