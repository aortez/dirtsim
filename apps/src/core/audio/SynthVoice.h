#pragma once

#include "Envelope.h"
#include "Oscillator.h"

namespace DirtSim {
namespace Audio {

/**
 * Monophonic synth voice with oscillator and envelope.
 */
class SynthVoice {
public:
    explicit SynthVoice(double sampleRate = 48000.0);

    void noteOn(
        double frequencyHz,
        double amplitude,
        double attackSeconds,
        double releaseSeconds,
        Waveform waveform);
    void noteOff();

    void setSampleRate(double sampleRate);

    double renderSample();

    double getAmplitude() const;
    double getFrequency() const;
    double getEnvelopeLevel() const;
    EnvelopeState getEnvelopeState() const;
    Waveform getWaveform() const;
    bool isActive() const;

private:
    Oscillator oscillator_;
    Envelope envelope_;
    double amplitude_ = 0.5;
};

} // namespace Audio
} // namespace DirtSim
