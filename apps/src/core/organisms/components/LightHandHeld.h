#pragma once

#include "core/LightManager.h"
#include "core/Vector2d.h"

namespace DirtSim {

/**
 * Physics simulation for a handheld light source.
 *
 * Models a flashlight as a weighted object the organism must actively hold up.
 * The light has angular inertia and responds to gravity and the holder's
 * acceleration, creating realistic wobble and struggle during movement.
 *
 * Key behaviors:
 * - Gravity constantly pulls the beam downward.
 * - Organism acceleration creates pseudo-forces (jump = beam droops).
 * - Organism exerts limited corrective torque toward target angle.
 * - Light shuts off when drooped below threshold (can't operate it while struggling).
 */
class LightHandHeld {
public:
    struct Config {
        float weight = 1.5f;
        float inertia = 0.4f;
        float max_torque = 3.0f;
        float damping = 2.0f;
        float accel_sensitivity = 0.08f;
        float shutoff_angle = -0.6f;
        float recovery_angle = -0.4f;
    };

    explicit LightHandHeld(LightHandle light);
    LightHandHeld(LightHandle light, Config config);

    /// Update physics and apply to light. Computes acceleration from position changes.
    void update(LightManager& lights, Vector2d position, bool facing_right, double deltaTime);

    float getPitch() const { return pitch_; }
    float getAngularVelocity() const { return angular_velocity_; }
    bool isOn() const { return is_on_; }
    LightId getLightId() const { return light_.id(); }
    const Config& getConfig() const { return config_; }
    void setConfig(const Config& config) { config_ = config; }

private:
    void applyToLight(LightManager& lights, Vector2d position, bool facing_right);
    void updatePhysics(Vector2d holder_acceleration, double deltaTime);

    Config config_;
    LightHandle light_;

    float angular_velocity_ = 0.0f;
    bool has_previous_ = false;
    bool is_on_ = true;
    float pitch_ = 0.0f; // Radians from horizontal. Positive = pointing down.
    Vector2d previous_position_{ 0.0, 0.0 };
    Vector2d previous_velocity_{ 0.0, 0.0 };
    float stored_intensity_ = 1.0f;
};

} // namespace DirtSim
