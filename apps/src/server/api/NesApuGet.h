#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/scenarios/nes/SmolnesRuntime.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace NesApuGet {

DEFINE_API_NAME(NesApuGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    bool pulse1_enabled = false;
    bool pulse2_enabled = false;
    bool triangle_enabled = false;
    bool noise_enabled = false;
    uint8_t pulse1_length_counter = 0;
    uint8_t pulse2_length_counter = 0;
    uint8_t triangle_length_counter = 0;
    uint8_t noise_length_counter = 0;
    uint16_t pulse1_timer_period = 0;
    uint16_t pulse2_timer_period = 0;
    uint16_t triangle_timer_period = 0;
    uint16_t noise_timer_period = 0;
    uint8_t pulse1_duty = 0;
    uint8_t pulse2_duty = 0;
    bool noise_mode = false;
    bool frame_counter_mode_5step = false;
    uint64_t register_write_count = 0;
    uint64_t total_samples_generated = 0;

    // Audio playback stats.
    uint64_t audio_underruns = 0;
    uint64_t audio_overruns = 0;
    uint64_t audio_callback_calls = 0;
    uint64_t audio_samples_dropped = 0;

    static Okay fromSnapshot(const SmolnesRuntime::ApuSnapshot& s);

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<22>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace NesApuGet
} // namespace Api
} // namespace DirtSim
