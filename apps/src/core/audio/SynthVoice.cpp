#include "SynthVoice.h"
#include <algorithm>

namespace DirtSim {
namespace Audio {

SynthVoice::SynthVoice(double sampleRate) : oscillator_(sampleRate), envelope_(sampleRate)
{}

void SynthVoice::noteOn(
    double frequencyHz,
    double amplitude,
    double attackSeconds,
    double releaseSeconds,
    Waveform waveform)
{
    oscillator_.setFrequency(frequencyHz);
    oscillator_.setWaveform(waveform);
    oscillator_.resetPhase();

    envelope_.setAttackSeconds(attackSeconds);
    envelope_.setReleaseSeconds(releaseSeconds);
    envelope_.noteOn();

    amplitude_ = std::clamp(amplitude, 0.0, 1.0);
}

void SynthVoice::noteOff()
{
    envelope_.noteOff();
}

void SynthVoice::setSampleRate(double sampleRate)
{
    oscillator_.setSampleRate(sampleRate);
    envelope_.setSampleRate(sampleRate);
}

double SynthVoice::renderSample()
{
    const double env = envelope_.nextAmplitude();
    if (env <= 0.0) {
        return 0.0;
    }
    return oscillator_.nextSample() * env * amplitude_;
}

double SynthVoice::getAmplitude() const
{
    return amplitude_;
}

double SynthVoice::getFrequency() const
{
    return oscillator_.getFrequency();
}

double SynthVoice::getEnvelopeLevel() const
{
    return envelope_.getLevel();
}

EnvelopeState SynthVoice::getEnvelopeState() const
{
    return envelope_.getState();
}

Waveform SynthVoice::getWaveform() const
{
    return oscillator_.getWaveform();
}

bool SynthVoice::isActive() const
{
    return envelope_.isActive();
}

} // namespace Audio
} // namespace DirtSim
