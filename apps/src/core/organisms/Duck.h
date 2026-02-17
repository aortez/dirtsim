#pragma once

#include "Body.h"
#include "DuckBrain.h"
#include "DuckInput.h"
#include "DuckSensoryData.h"
#include <memory>
#include <random>
#include <vector>

namespace DirtSim {

class LightHandHeld;

/**
 * Sparkle particle owned by a duck.
 *
 * Sparkles spawn at the duck, receive random force impulses,
 * and fade out over their lifetime.
 */
struct DuckSparkle {
    Vector2<float> position{ 0.0f, 0.0f }; // Absolute world position.
    Vector2<float> velocity{ 0.0f, 0.0f }; // Cells per second.
    float lifetime = 1.0f;                 // Remaining life (1.0 = full, 0.0 = dead).
    float max_lifetime = 1.0f;             // For calculating opacity.
};

/**
 * Duck organism - a mobile creature that walks, jumps, and runs.
 *
 * The duck is represented as a single WOOD cell in the physics simulation.
 * It participates fully in cell physics (gravity, collisions, friction)
 * while the brain controls its intended movement.
 *
 * Physics approach:
 * - Duck occupies a WOOD cell (organism_id marks it as duck).
 * - Brain sets velocity intent on the cell.
 * - World physics handles gravity, collisions, friction.
 * - Duck checks surrounding cells for ground detection.
 *
 * Rendering:
 * - UI draws duck sprite at the duck's cell position.
 * - Sprite flips based on facing direction.
 */
class Duck : public Organism::Body {
public:
    /**
     * Construct a new duck with a given brain implementation.
     *
     * @param id Unique organism identifier.
     * @param brain Brain implementation for movement decisions.
     */
    Duck(OrganismId id, std::unique_ptr<DuckBrain> brain);
    ~Duck();

    // Organism interface.
    Vector2i getAnchorCell() const override { return anchor_cell_; }
    void setAnchorCell(Vector2i pos) override
    {
        anchor_cell_ = pos;
        position.x = static_cast<double>(pos.x) + 0.5;
        position.y = static_cast<double>(pos.y) + 0.5;
    }
    void update(World& world, double deltaTime) override;

    // Duck-specific state.
    bool isOnGround() const { return on_ground_; }
    DuckAction getCurrentAction() const;

    void setInput(DuckInput input);

    // Replace the brain (for testing).
    void setBrain(std::unique_ptr<DuckBrain> brain) { brain_ = std::move(brain); }

    // Access the brain (for external input like gamepad).
    DuckBrain* getBrain() { return brain_.get(); }

    // Sparkle access for rendering.
    const std::vector<DuckSparkle>& getSparkles() const { return sparkles_; }

    // Maximum sparkle count (used for emission ratio calculation).
    static constexpr int MAX_SPARKLES = 32;

    // Get sparkle ratio [0, 1] for emission calculations.
    float getSparkleRatio() const
    {
        return static_cast<float>(sparkles_.size()) / static_cast<float>(MAX_SPARKLES);
    }

    DuckSensoryData gatherSensoryData(const World& world, double deltaTime) const;

    void setHandheldLight(std::unique_ptr<LightHandHeld> light);
    LightHandHeld* getHandheldLight() { return handheld_light_.get(); }

private:
    Vector2i anchor_cell_{ 0, 0 };
    bool on_ground_ = false;
    DuckInput current_input_{};
    bool jump_cooldown_active_ = false;
    bool jump_cooldown_queued_ = false;
    uint32_t frame_counter_ = 0;

    static constexpr float GROUND_CONTACT_COM_THRESHOLD = 0.80f; // COM must be near cell bottom.
    static constexpr float GROUND_REST_VERTICAL_SPEED_THRESHOLD =
        0.10f; // Near-resting vertical speed.

    std::unique_ptr<DuckBrain> brain_;

    std::vector<DuckSparkle> sparkles_;
    std::mt19937 sparkle_rng_{ std::random_device{}() };
    Vector2d previous_velocity_{ 0.0, 0.0 };
    Vector2d smoothed_acceleration_{ 0.0, 0.0 };
    std::unique_ptr<LightHandHeld> handheld_light_;

    void applyMovementToCell(World& world, double deltaTime);
    int getDesiredSparkleCount(float acceleration) const;
    bool isSolidCell(const World& world, int x, int y) const;
    void logPhysicsState(const World& world);
    void spawnSparkle(const Vector2d& duck_velocity);
    void updateGroundDetection(const World& world);
    void updateHandheldLight(World& world, double deltaTime);
    void updateSparkles(const World& world, double deltaTime);
};

} // namespace DirtSim
