#include "LightHandHeld.h"

#include "core/Assert.h"
#include "core/LightTypes.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

LightHandHeld::LightHandHeld(LightHandle light) : config_{}, light_(std::move(light))
{}

LightHandHeld::LightHandHeld(LightHandle light, Config config)
    : config_(config), light_(std::move(light))
{}

void LightHandHeld::update(
    LightManager& lights, Vector2d position, bool facing_right, double deltaTime)
{
    const float dt = static_cast<float>(deltaTime);
    if (dt <= 0.0f) {
        return;
    }

    // Compute velocity from position change.
    Vector2d velocity{ 0.0, 0.0 };
    if (has_previous_) {
        velocity.x = (position.x - previous_position_.x) / deltaTime;
        velocity.y = (position.y - previous_position_.y) / deltaTime;
    }

    // Compute acceleration from velocity change.
    Vector2d acceleration{ 0.0, 0.0 };
    if (has_previous_) {
        acceleration.x = (velocity.x - previous_velocity_.x) / deltaTime;
        acceleration.y = (velocity.y - previous_velocity_.y) / deltaTime;
    }

    // Store for next frame.
    previous_position_ = position;
    previous_velocity_ = velocity;
    has_previous_ = true;

    updatePhysics(acceleration, deltaTime);
    applyToLight(lights, position, facing_right);
}

void LightHandHeld::updatePhysics(Vector2d holder_acceleration, double deltaTime)
{
    const float dt = static_cast<float>(deltaTime);

    // Gravity torque: pulls beam downward, strongest when horizontal (pitch=0).
    // Positive pitch = pointing down (toward +Y in screen coords).
    const float gravity_torque = config_.weight * std::cos(pitch_);

    // Pseudo-force from holder acceleration.
    // When holder accelerates up (jump), beam feels heavier and droops more.
    // When holder accelerates down (fall), beam feels lighter and rises.
    // In DirtSim: upward accel = negative y, droop = positive pitch.
    // Negative y should produce positive torque (more droop), so we negate.
    const float accel_torque =
        -static_cast<float>(holder_acceleration.y) * config_.accel_sensitivity;

    const float hold_torque = -gravity_torque;
    const float return_torque = -pitch_ * config_.max_torque * 2.0f;
    const float corrective_torque =
        std::clamp(hold_torque + return_torque, -config_.max_torque, config_.max_torque);

    // Damping torque: opposes angular velocity.
    const float damping_torque = -angular_velocity_ * config_.damping;

    // Sum torques and integrate.
    const float net_torque = gravity_torque + accel_torque + corrective_torque + damping_torque;
    const float angular_accel = net_torque / config_.inertia;

    angular_velocity_ += angular_accel * dt;
    pitch_ += angular_velocity_ * dt;

    // Clamp pitch to reasonable range.
    constexpr float MAX_PITCH = static_cast<float>(M_PI) / 2.0f;
    pitch_ = std::clamp(pitch_, -MAX_PITCH, MAX_PITCH);

    // Shutoff logic with hysteresis. Light only works when held near horizontal.
    if (is_on_ && std::abs(pitch_) > std::abs(config_.shutoff_angle)) {
        is_on_ = false;
    }
    else if (!is_on_ && std::abs(pitch_) < std::abs(config_.recovery_angle)) {
        is_on_ = true;
    }
}

void LightHandHeld::applyToLight(LightManager& lights, Vector2d position, bool facing_right)
{
    auto* spot = lights.getLight<SpotLight>(light_.id());
    DIRTSIM_ASSERT(spot, "LightHandHeld::applyToLight: Light not found.");

    // Store intensity on first call for later restore.
    if (stored_intensity_ == 1.0f && spot->intensity != 0.0f) {
        stored_intensity_ = spot->intensity;
    }

    // Compute final angle: facing mirrors instantly, pitch adds vertical wobble.
    // Facing right: angle = pitch (0 = right, positive = down-right).
    // Facing left: angle = π - pitch (π = left).
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
