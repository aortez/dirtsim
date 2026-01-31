#include "Envelope.h"
#include <algorithm>

namespace DirtSim {
namespace Audio {

Envelope::Envelope(double sampleRate) : sampleRate_(std::max(1.0, sampleRate))
{}

void Envelope::noteOn()
{
    level_ = 0.0;
    state_ = EnvelopeState::Attack;
}

void Envelope::noteOff()
{
    if (state_ != EnvelopeState::Idle) {
        state_ = EnvelopeState::Release;
    }
}

void Envelope::setAttackSeconds(double seconds)
{
    attackSeconds_ = std::max(0.0, seconds);
}

void Envelope::setReleaseSeconds(double seconds)
{
    releaseSeconds_ = std::max(0.0, seconds);
}

void Envelope::setSampleRate(double sampleRate)
{
    sampleRate_ = std::max(1.0, sampleRate);
}

double Envelope::nextAmplitude()
{
    if (state_ == EnvelopeState::Idle) {
        level_ = 0.0;
        return 0.0;
    }

    if (state_ == EnvelopeState::Attack) {
        if (attackSeconds_ <= 0.0) {
            level_ = 1.0;
            state_ = EnvelopeState::Sustain;
            return level_;
        }

        const double step = 1.0 / (attackSeconds_ * sampleRate_);
        level_ += step;
        if (level_ >= 1.0) {
            level_ = 1.0;
            state_ = EnvelopeState::Sustain;
        }
        return level_;
    }

    if (state_ == EnvelopeState::Release) {
        if (releaseSeconds_ <= 0.0) {
            level_ = 0.0;
            state_ = EnvelopeState::Idle;
            return level_;
        }

        const double step = 1.0 / (releaseSeconds_ * sampleRate_);
        level_ -= step;
        if (level_ <= 0.0) {
            level_ = 0.0;
            state_ = EnvelopeState::Idle;
        }
        return level_;
    }

    return level_;
}

double Envelope::getLevel() const
{
    return level_;
}

EnvelopeState Envelope::getState() const
{
    return state_;
}

bool Envelope::isActive() const
{
    return state_ != EnvelopeState::Idle;
}

} // namespace Audio
} // namespace DirtSim
