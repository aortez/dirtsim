#include "NesApuGet.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace NesApuGet {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

nlohmann::json Okay::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Okay Okay::fromSnapshot(const SmolnesRuntime::ApuSnapshot& s)
{
    Okay okay;
    okay.pulse1_enabled = s.pulse1Enabled;
    okay.pulse2_enabled = s.pulse2Enabled;
    okay.triangle_enabled = s.triangleEnabled;
    okay.noise_enabled = s.noiseEnabled;
    okay.pulse1_length_counter = s.pulse1LengthCounter;
    okay.pulse2_length_counter = s.pulse2LengthCounter;
    okay.triangle_length_counter = s.triangleLengthCounter;
    okay.noise_length_counter = s.noiseLengthCounter;
    okay.pulse1_timer_period = s.pulse1TimerPeriod;
    okay.pulse2_timer_period = s.pulse2TimerPeriod;
    okay.triangle_timer_period = s.triangleTimerPeriod;
    okay.noise_timer_period = s.noiseTimerPeriod;
    okay.pulse1_duty = s.pulse1Duty;
    okay.pulse2_duty = s.pulse2Duty;
    okay.noise_mode = s.noiseMode;
    okay.frame_counter_mode_5step = s.frameCounterMode5Step;
    okay.register_write_count = s.registerWriteCount;
    okay.total_samples_generated = s.totalSamplesGenerated;
    okay.audio_underruns = s.audioUnderruns;
    okay.audio_overruns = s.audioOverruns;
    okay.audio_callback_calls = s.audioCallbackCalls;
    okay.audio_samples_dropped = s.audioSamplesDropped;
    return okay;
}

} // namespace NesApuGet
} // namespace Api
} // namespace DirtSim
