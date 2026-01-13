#include "LightHandHeld.h"

#include "core/LightTypes.h"
#include <algorithm>
#include <cmath>

namespace DirtSim {

LightHandHeld::LightHandHeld(LightHandle light) : light_(std::move(light)), config_{}
{}

LightHandHeld::LightHandHeld(LightHandle light, Config config)
    : light_(std::move(light)), config_(config)
{}

void LightHandHeld::update(Vector2d holder_acceleration, double deltaTime)
{
    const float dt = static_cast<float>(deltaTime);
    if (dt <= 0.0f) {
        return;
    }

    // Gravity torque: pulls beam downward, strongest when horizontal (pitch=0).
    const float gravity_torque = -config_.weight * std::cos(pitch_);

    // Pseudo-force from holder acceleration.
    // When holder accelerates up (jump), beam feels heavier and droops.
    // When holder accelerates down (fall), beam feels lighter and rises.
    const float accel_torque =
        static_cast<float>(-holder_acceleration.y) * config_.accel_sensitivity;

    // Duck's corrective torque: tries to return to horizontal (pitch=0).
    const float correction_strength = -pitch_ * config_.max_torque * 2.0f;
    const float duck_torque =
        std::clamp(correction_strength, -config_.max_torque, config_.max_torque);

    // Damping torque: opposes angular velocity.
    const float damping_torque = -angular_velocity_ * config_.damping;

    // Sum torques and integrate.
    const float net_torque = gravity_torque + accel_torque + duck_torque + damping_torque;
    const float angular_accel = net_torque / config_.inertia;

    angular_velocity_ += angular_accel * dt;
    pitch_ += angular_velocity_ * dt;

    // Clamp pitch to reasonable range.
    constexpr float MAX_PITCH = static_cast<float>(M_PI) / 2.0f;
    pitch_ = std::clamp(pitch_, -MAX_PITCH, MAX_PITCH);

    // Shutoff logic with hysteresis to prevent flicker.
    if (is_on_ && pitch_ < config_.shutoff_angle) {
        is_on_ = false;
    }
    else if (!is_on_ && pitch_ > config_.recovery_angle) {
        is_on_ = true;
    }
}

void LightHandHeld::applyToLight(LightManager& lights, Vector2d position, bool facing_right)
{
    auto* spot = lights.getLight<SpotLight>(light_.id());
    if (!spot) {
        return;
    }

    // Store intensity on first call for later restore.
    if (stored_intensity_ == 1.0f && spot->intensity != 0.0f) {
        stored_intensity_ = spot->intensity;
    }

    // Compute final angle: facing mirrors instantly, pitch adds vertical wobble.
    // Facing right: angle = pitch (0 = right, negative = down-right).
    // Facing left: angle = π - pitch (π = left, π + positive = down-left).
    float angle;
    if (facing_right) {
        angle = pitch_;
    }
    else {
        angle = static_cast<float>(M_PI) - pitch_;
    }

    spot->position = position;
    spot->direction = angle;
    spot->intensity = is_on_ ? stored_intensity_ : 0.0f;
}

} // namespace DirtSim
