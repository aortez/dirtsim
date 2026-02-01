#include "Oscillator.h"
#include <algorithm>
#include <cmath>

namespace DirtSim {
namespace Audio {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
}

Oscillator::Oscillator(double sampleRate) : sampleRate_(std::max(1.0, sampleRate))
{}

void Oscillator::resetPhase()
{
    phase_ = 0.0;
}

void Oscillator::setFrequency(double frequencyHz)
{
    frequencyHz_ = std::max(0.0, frequencyHz);
}

void Oscillator::setSampleRate(double sampleRate)
{
    sampleRate_ = std::max(1.0, sampleRate);
}

void Oscillator::setWaveform(Waveform waveform)
{
    waveform_ = waveform;
}

double Oscillator::nextSample()
{
    const double phase = phase_;
    double value = 0.0;

    switch (waveform_) {
        case Waveform::Sine:
            value = std::sin(kTwoPi * phase);
            break;
        case Waveform::Square:
            value = (phase < 0.5) ? 1.0 : -1.0;
            break;
        case Waveform::Triangle:
            value = 1.0 - 4.0 * std::abs(phase - 0.5);
            break;
        case Waveform::Saw:
            value = 2.0 * phase - 1.0;
            break;
    }

    if (sampleRate_ > 0.0) {
        phase_ += frequencyHz_ / sampleRate_;
        if (phase_ >= 1.0) {
            phase_ -= std::floor(phase_);
        }
    }

    return value;
}

double Oscillator::getFrequency() const
{
    return frequencyHz_;
}

double Oscillator::getSampleRate() const
{
    return sampleRate_;
}

Waveform Oscillator::getWaveform() const
{
    return waveform_;
}

} // namespace Audio
} // namespace DirtSim
