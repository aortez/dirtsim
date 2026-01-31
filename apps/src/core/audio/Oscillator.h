#pragma once

#include <cstdint>

namespace DirtSim {
namespace Audio {

/**
 * Basic oscillator for generating periodic waveforms.
 */
enum class Waveform : uint8_t {
    Sine = 0,
    Square = 1,
    Triangle = 2,
    Saw = 3,
};

class Oscillator {
public:
    explicit Oscillator(double sampleRate = 48000.0);

    void resetPhase();
    void setFrequency(double frequencyHz);
    void setSampleRate(double sampleRate);
    void setWaveform(Waveform waveform);

    double nextSample();

    double getFrequency() const;
    double getSampleRate() const;
    Waveform getWaveform() const;

private:
    double sampleRate_ = 48000.0;
    double frequencyHz_ = 440.0;
    double phase_ = 0.0;
    Waveform waveform_ = Waveform::Sine;
};

} // namespace Audio
} // namespace DirtSim
