#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/audio/Envelope.h"
#include "core/audio/Oscillator.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace AudioApi {
namespace StatusGet {

DEFINE_API_NAME(StatusGet);

struct Okay;

struct Command {
    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    bool active = false;
    uint32_t note_id = 0;
    double frequency_hz = 0.0;
    double amplitude = 0.0;
    double envelope_level = 0.0;
    Audio::EnvelopeState envelope_state = Audio::EnvelopeState::Idle;
    Audio::Waveform waveform = Audio::Waveform::Sine;
    double sample_rate = 0.0;
    std::string device_name;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<9>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace StatusGet
} // namespace AudioApi
} // namespace DirtSim
