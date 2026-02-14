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
#include <vector>
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

enum class HoldState : uint8_t {
    Held = 0,
    Releasing = 1,
};

struct ActiveNote {
    uint32_t note_id = 0;
    double frequency_hz = 0.0;
    double amplitude = 0.0;
    Audio::Waveform waveform = Audio::Waveform::Sine;
    Audio::EnvelopeState envelope_state = Audio::EnvelopeState::Idle;
    HoldState hold_state = HoldState::Held;

    API_JSON_SERIALIZABLE(ActiveNote);
    using serialize = zpp::bits::members<6>;
};

inline void to_json(nlohmann::json& j, const ActiveNote& note)
{
    j = ReflectSerializer::to_json(note);
}

inline void from_json(const nlohmann::json& j, ActiveNote& note)
{
    note = ReflectSerializer::from_json<ActiveNote>(j);
}

struct Okay {
    std::vector<ActiveNote> active_notes;
    double sample_rate = 0.0;
    std::string device_name;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<3>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace StatusGet
} // namespace AudioApi
} // namespace DirtSim
