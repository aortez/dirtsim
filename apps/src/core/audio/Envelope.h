#pragma once

#include <cstdint>

namespace DirtSim {
namespace Audio {

/**
 * Linear attack/release envelope.
 */
enum class EnvelopeState : uint8_t {
    Idle = 0,
    Attack = 1,
    Sustain = 2,
    Release = 3,
};

class Envelope {
public:
    explicit Envelope(double sampleRate = 48000.0);

    void noteOn();
    void noteOff();

    void setAttackSeconds(double seconds);
    void setReleaseSeconds(double seconds);
    void setSampleRate(double sampleRate);

    double nextAmplitude();

    double getLevel() const;
    EnvelopeState getState() const;
    bool isActive() const;

private:
    double sampleRate_ = 48000.0;
    double attackSeconds_ = 0.01;
    double releaseSeconds_ = 0.1;
    double level_ = 0.0;
    EnvelopeState state_ = EnvelopeState::Idle;
};

} // namespace Audio
} // namespace DirtSim
