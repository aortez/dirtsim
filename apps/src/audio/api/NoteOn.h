#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/audio/Oscillator.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace AudioApi {
namespace NoteOn {

DEFINE_API_NAME(NoteOn);

struct Okay;

struct Command {
    double frequency_hz = 440.0;
    double amplitude = 0.5;
    double attack_ms = 10.0;
    double release_ms = 120.0;
    double duration_ms = 0.0;
    Audio::Waveform waveform = Audio::Waveform::Sine;
    uint32_t note_id = 0;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<7>;
};

struct Okay {
    bool accepted = true;
    uint32_t note_id = 0;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<2>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace NoteOn
} // namespace AudioApi
} // namespace DirtSim
