#include "Duck.h"
#include "OrganismSensoryData.h"
#include "components/LightHandHeld.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
// Physics constants.
static constexpr float WALK_FORCE = 10.0f;  // Force applied each frame when walking.
static constexpr float JUMP_FORCE = 300.0f; // Impulse force applied once when jumping.

// SMB1-style asymmetric air steering: you accelerate faster in the direction
// you are NOT facing. This enables the "backwards jump trick".
static constexpr float AIR_CONTROL_SAME = 0.15f;     // Steering same direction as facing.
static constexpr float AIR_CONTROL_OPPOSING = 0.30f; // Steering opposite to facing (2x bonus).

// Sparkle constants.
static constexpr int MIN_SPARKLES = 0; // Sparkles when at rest.
static constexpr float SPARKLE_ACCELERATION_FLOOR =
    30.0f; // Below this, no sparkles (filters noise).
static constexpr float SPARKLE_ACCELERATION_MAX =
    200.0f; // Acceleration for max sparkles (cells/sec^2).
static constexpr float SPARKLE_ACCEL_SMOOTHING =
    0.85f;                                      // Smoothing factor (0=instant, 1=never updates).
static constexpr float SPARKLE_LIFETIME = 2.0f; // Seconds before sparkle fades completely.
static constexpr float SPARKLE_DRAG = 0.98f;    // Velocity multiplier per frame (damping).
static constexpr float SPARKLE_IMPULSE = 3.0f;  // Max random impulse magnitude.
static constexpr float SPARKLE_IMPULSE_CHANCE = 0.15f; // Chance per frame of impulse.
static constexpr float SPARKLE_GRAVITY = 20.0f;        // Gravity acceleration (cells/sec^2).
static constexpr float SPARKLE_BOUNCE = 0.7f;          // Velocity retained after bounce (0-1).

std::string duckCommandSignature(const DirtSim::DuckInput& input)
{
    if (input.jump) {
        return "Jump";
    }

    if (input.move.x < -0.01f) {
        return "RunLeft";
    }

    if (input.move.x > 0.01f) {
        return "RunRight";
    }

    return "Wait";
}

std::string duckCommandOutcomeSignature(const std::string& commandSignature, const char* outcome)
{
    return commandSignature + " -> " + outcome;
}
} // namespace

namespace DirtSim {

Duck::Duck(OrganismId id, std::unique_ptr<DuckBrain> brain)
    : Organism::Body(id, OrganismType::DUCK), brain_(std::move(brain))
{}

Duck::~Duck() = default;

void Duck::update(World& world, double deltaTime)
{
    age_seconds_ += deltaTime;
    frame_counter_++;

    // Update jump cooldown.
    if (jump_cooldown_ > 0.0f) {
        jump_cooldown_ -= static_cast<float>(deltaTime);
    }

    // INVARIANT CHECKS: Verify duck's anchor_cell_ matches world state.
    const WorldData& data = world.getData();
    if (data.inBounds(anchor_cell_.x, anchor_cell_.y)) {

        const Cell& our_cell = data.at(anchor_cell_.x, anchor_cell_.y);

        // Cell material must be WOOD.
        if (our_cell.material_type != Material::EnumType::Wood) {
            spdlog::critical(
                "Duck {} VIOLATION: anchor ({},{}) has {} instead of WOOD!",
                id_,
                anchor_cell_.x,
                anchor_cell_.y,
                toString(our_cell.material_type));
            spdlog::critical(
                "  Duck: age={:.1f}s, on_ground={}, facing=({:.1f},{:.1f})",
                age_seconds_,
                on_ground_,
                facing_.x,
                facing_.y);
            OrganismId anchor_org = world.getOrganismManager().at(anchor_cell_);
            spdlog::critical(
                "  Cell at anchor: fill={:.2f}, vel=({:.1f},{:.1f}), organism_id={}",
                our_cell.fill_ratio,
                our_cell.velocity.x,
                our_cell.velocity.y,
                anchor_org);

            // Scan entire world to find where our organism actually is.
            spdlog::critical("  Scanning world for duck's actual cells...");
            for (int y = 0; y < data.height; ++y) {
                for (int x = 0; x < data.width; ++x) {
                    Vector2i pos{ x, y };
                    if (world.getOrganismManager().at(pos) == id_) {
                        const Cell& cell = data.at(x, y);
                        spdlog::critical(
                            "    Found organism_id={} at ({},{}): material={}, fill={:.2f}",
                            id_,
                            x,
                            y,
                            toString(cell.material_type),
                            cell.fill_ratio);
                    }
                }
            }
        }
        DIRTSIM_ASSERT(
            our_cell.material_type == Material::EnumType::Wood, "Duck anchor cell must be WOOD!");

        // OrganismManager must say we own our anchor.
        OrganismId manager_says = world.getOrganismManager().at(anchor_cell_);
        if (manager_says != id_) {
            spdlog::critical(
                "Duck {} VIOLATION: anchor ({},{}) has organism_id {} instead of {}!",
                id_,
                anchor_cell_.x,
                anchor_cell_.y,
                manager_says,
                id_);
        }
        DIRTSIM_ASSERT(manager_says == id_, "Duck anchor cell organism_id must match!");
    }

    // Update ground detection first.
    updateGroundDetection(world);

    // Gather sensory data and let brain decide what to do.
    if (brain_) {
        DuckSensoryData sensory = gatherSensoryData(world, deltaTime);
        brain_->think(*this, sensory, deltaTime);
    }

    recordCommandSignature(duckCommandSignature(current_input_));

    // Apply movement intent to the cell.
    applyMovementToCell(world, deltaTime);

    // Update sparkle particle system.
    updateSparkles(world, deltaTime);

    // Update handheld light (if present) or fall back to simple attached lights.
    if (handheld_light_) {
        updateHandheldLight(world, deltaTime);
    }
    else {
        updateAttachedLights(world, deltaTime);
    }

    // Log physics state every 60 frames.
    if (frame_counter_ % 60 == 0) {
        logPhysicsState(world);
    }
}

DuckAction Duck::getCurrentAction() const
{
    return brain_ ? brain_->getCurrentAction() : DuckAction::WAIT;
}

void Duck::setInput(DuckInput input)
{
    current_input_ = input;
}

void Duck::updateGroundDetection(const World& world)
{
    const WorldData& data = world.getData();

    // Check if there's solid ground below us.
    int below_y = anchor_cell_.y + 1;

    if (below_y >= data.height) {
        // At bottom of world - consider this as ground (wall border).
        on_ground_ = true;
        return;
    }

    const Cell& below = data.at(anchor_cell_.x, below_y);

    // Ground is any non-AIR, non-empty cell.
    // Also check if it's a wall or has significant fill.
    bool is_solid_below = (below.material_type == Material::EnumType::Wall
                           || below.material_type == Material::EnumType::Dirt
                           || below.material_type == Material::EnumType::Sand
                           || below.material_type == Material::EnumType::Wood
                           || below.material_type == Material::EnumType::Metal
                           || below.material_type == Material::EnumType::Root)
        && below.fill_ratio > 0.5;

    // Also check our own cell's COM - if it's near the bottom, we might be resting.
    const Cell& our_cell = data.at(anchor_cell_.x, anchor_cell_.y);
    bool com_at_bottom = our_cell.com.y > 0.5;

    on_ground_ = is_solid_below || (com_at_bottom && is_solid_below);

    // Alternative: check if our velocity is near zero and we're not falling.
    if (!on_ground_ && std::abs(our_cell.velocity.y) < 0.1 && is_solid_below) {
        on_ground_ = true;
    }
}

void Duck::applyMovementToCell(World& world, double /*deltaTime*/)
{
    const std::string commandSignature = duckCommandSignature(current_input_);
    const auto recordOutcome = [this, &commandSignature](const char* outcome) {
        recordCommandOutcomeSignature(duckCommandOutcomeSignature(commandSignature, outcome));
    };

    WorldData& data = world.getData();

    // Bounds check.
    if (!data.inBounds(anchor_cell_.x, anchor_cell_.y)) {
        recordOutcome("OUT_OF_BOUNDS");
        return;
    }

    Cell& cell = data.at(anchor_cell_.x, anchor_cell_.y);
    const char* outcome = "APPLIED";

    // Process jump input (independent of movement).
    if (current_input_.jump) {
        if (!on_ground_) {
            LOG_WARN(Brain, "Duck {}: Jump requested but not on ground.", id_);
            outcome = "NOT_ON_GROUND";
        }
        else if (jump_cooldown_ > 0.0f) {
            LOG_WARN(
                Brain, "Duck {}: Jump requested but in cooldown ({:.2f}s).", id_, jump_cooldown_);
            outcome = "COOLDOWN";
        }
        else {
            double gravity = world.getPhysicsSettings().gravity;
            double jump_direction = (gravity >= 0) ? -1.0 : 1.0;
            Vector2d jump_force(0.0, jump_direction * JUMP_FORCE);
            cell.addPendingForce(jump_force);
            on_ground_ = false;
            jump_cooldown_ = JUMP_COOLDOWN;
            LOG_INFO(Brain, "Duck {}: Jump applied, force={:.1f}.", id_, jump_force.y);
        }
    }

    // Apply movement force (reduced when airborne for SMB1-style air steering).
    // Brain provides continuous input [-1,1], we scale by WALK_FORCE.
    float move_x = current_input_.move.x;
    if (std::abs(move_x) > 0.01f) {
        float multiplier;
        if (on_ground_) {
            multiplier = 1.0f;
        }
        else {
            // SMB1-style asymmetric air control: steering opposite to facing gives bonus.
            bool steering_opposes_facing =
                (move_x > 0 && facing_.x < 0) || (move_x < 0 && facing_.x > 0);
            multiplier = steering_opposes_facing ? AIR_CONTROL_OPPOSING : AIR_CONTROL_SAME;
        }
        Vector2d walk_force(move_x * WALK_FORCE * multiplier, 0.0);
        cell.addPendingForce(walk_force);
    }

    // Update facing direction based on move input or velocity.
    // Only update while on ground - facing is locked during jumps (SMB1-style).
    if (on_ground_) {
        if (std::abs(move_x) > 0.01f) {
            facing_.x = (move_x > 0) ? 1.0f : -1.0f;
            facing_.y = 0.0f;
        }
        else if (std::abs(cell.velocity.x) > 1.0) {
            facing_.x = (cell.velocity.x > 0) ? 1.0f : -1.0f;
            facing_.y = 0.0f;
        }
    }

    recordOutcome(outcome);
}

void Duck::logPhysicsState(const World& world)
{
    const WorldData& data = world.getData();

    // Bounds check.
    if (!data.inBounds(anchor_cell_.x, anchor_cell_.y)) {
        LOG_INFO(Brain, "Duck {}: OUT OF BOUNDS at ({}, {})", id_, anchor_cell_.x, anchor_cell_.y);
        return;
    }

    const Cell& cell = data.at(anchor_cell_.x, anchor_cell_.y);

    LOG_INFO(
        Brain,
        "Duck {} frame {}: pos=({}, {}), com=({:.2f}, {:.2f}), vel=({:.2f}, {:.2f}), "
        "force=({:.2f}, {:.2f}), on_ground={}, material={}",
        id_,
        frame_counter_,
        anchor_cell_.x,
        anchor_cell_.y,
        cell.com.x,
        cell.com.y,
        cell.velocity.x,
        cell.velocity.y,
        cell.pending_force.x,
        cell.pending_force.y,
        on_ground_,
        static_cast<int>(cell.material_type));
}

DuckSensoryData Duck::gatherSensoryData(const World& world, double deltaTime) const
{
    DuckSensoryData data;

    // Use the utility to gather material histograms centered on duck.
    // Out-of-bounds cells are marked as WALL for edge detection.
    SensoryUtils::
        gatherMaterialHistograms<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
            world, anchor_cell_, data.material_histograms, data.world_offset);

    // Set actual dimensions (always grid size for duck, no scaling).
    data.actual_width = DuckSensoryData::GRID_SIZE;
    data.actual_height = DuckSensoryData::GRID_SIZE;
    data.scale_factor = 1.0;

    // Fill in duck-specific fields.
    data.position = anchor_cell_;
    data.on_ground = on_ground_;
    data.facing_x = facing_.x;
    data.delta_time_seconds = deltaTime;

    // Get velocity from our cell.
    const WorldData& world_data = world.getData();
    if (world_data.inBounds(anchor_cell_.x, anchor_cell_.y)) {
        const Cell& cell = world_data.at(anchor_cell_.x, anchor_cell_.y);
        data.velocity = cell.velocity;
    }
    else {
        data.velocity = Vector2d{ 0.0, 0.0 };
    }

    return data;
}

void Duck::updateSparkles(const World& world, double deltaTime)
{
    float dt = static_cast<float>(deltaTime);
    const WorldData& data = world.getData();

    // Update existing sparkles.
    for (auto& sparkle : sparkles_) {
        // Apply random impulse occasionally.
        std::uniform_real_distribution<float> chance_dist(0.0f, 1.0f);
        if (chance_dist(sparkle_rng_) < SPARKLE_IMPULSE_CHANCE) {
            std::uniform_real_distribution<float> impulse_dist(-SPARKLE_IMPULSE, SPARKLE_IMPULSE);
            sparkle.velocity.x += impulse_dist(sparkle_rng_);
            sparkle.velocity.y += impulse_dist(sparkle_rng_);
        }

        // Apply gravity.
        sparkle.velocity.y += SPARKLE_GRAVITY * dt;

        // Apply drag.
        sparkle.velocity.x *= SPARKLE_DRAG;
        sparkle.velocity.y *= SPARKLE_DRAG;

        // Calculate new position.
        float new_x = sparkle.position.x + sparkle.velocity.x * dt;
        float new_y = sparkle.position.y + sparkle.velocity.y * dt;

        // Check for collisions with solid cells.
        int cell_x = static_cast<int>(new_x);
        int cell_y = static_cast<int>(new_y);
        int old_cell_x = static_cast<int>(sparkle.position.x);
        int old_cell_y = static_cast<int>(sparkle.position.y);

        // Horizontal collision.
        if (cell_x != old_cell_x && isSolidCell(world, cell_x, old_cell_y)) {
            sparkle.velocity.x = -sparkle.velocity.x * SPARKLE_BOUNCE;
            new_x = sparkle.position.x; // Don't move into solid.
        }

        // Vertical collision.
        if (cell_y != old_cell_y && isSolidCell(world, static_cast<int>(new_x), cell_y)) {
            sparkle.velocity.y = -sparkle.velocity.y * SPARKLE_BOUNCE;
            new_y = sparkle.position.y; // Don't move into solid.
        }

        // World bounds check.
        if (new_x < 0.0f) {
            new_x = 0.0f;
            sparkle.velocity.x = -sparkle.velocity.x * SPARKLE_BOUNCE;
        }
        else if (new_x >= static_cast<float>(data.width)) {
            new_x = static_cast<float>(data.width) - 0.01f;
            sparkle.velocity.x = -sparkle.velocity.x * SPARKLE_BOUNCE;
        }

        if (new_y < 0.0f) {
            new_y = 0.0f;
            sparkle.velocity.y = -sparkle.velocity.y * SPARKLE_BOUNCE;
        }
        else if (new_y >= static_cast<float>(data.height)) {
            new_y = static_cast<float>(data.height) - 0.01f;
            sparkle.velocity.y = -sparkle.velocity.y * SPARKLE_BOUNCE;
        }

        // Update position.
        sparkle.position.x = new_x;
        sparkle.position.y = new_y;

        // Check for collision with solid cells (but not the duck itself).
        int sparkle_cell_x = static_cast<int>(sparkle.position.x);
        int sparkle_cell_y = static_cast<int>(sparkle.position.y);

        // Skip collision if sparkle is in the duck's cell (sparkles can pass through duck).
        bool in_duck_cell = (sparkle_cell_x == anchor_cell_.x && sparkle_cell_y == anchor_cell_.y);

        if (!in_duck_cell && isSolidCell(world, sparkle_cell_x, sparkle_cell_y)) {
            // Sparkle is inside a solid - revert to previous position and bounce.
            float old_x = sparkle.position.x - sparkle.velocity.x * dt;
            float old_y = sparkle.position.y - sparkle.velocity.y * dt;
            sparkle.position.x = old_x;
            sparkle.position.y = old_y;
            sparkle.velocity.x *= -SPARKLE_BOUNCE;
            sparkle.velocity.y *= -SPARKLE_BOUNCE;
        }

        // Decrease lifetime.
        sparkle.lifetime -= dt / SPARKLE_LIFETIME;
    }

    // Remove dead sparkles.
    sparkles_.erase(
        std::remove_if(
            sparkles_.begin(),
            sparkles_.end(),
            [](const DuckSparkle& s) { return s.lifetime <= 0.0f; }),
        sparkles_.end());

    // Spawn or remove sparkles to match desired count based on acceleration.
    // Get duck's current velocity from cell and calculate acceleration.
    const WorldData& world_data = world.getData();
    if (world_data.inBounds(anchor_cell_.x, anchor_cell_.y)) {
        const Cell& cell = world_data.at(anchor_cell_.x, anchor_cell_.y);

        // Calculate instantaneous acceleration components (change in velocity over time).
        Vector2d velocity_change = cell.velocity - previous_velocity_;
        Vector2d instant_accel{ 0.0, 0.0 };
        if (dt > 0.0f) {
            instant_accel.x = std::abs(velocity_change.x) / dt;
            instant_accel.y = std::abs(velocity_change.y) / dt;
        }

        // Smooth X and Y acceleration independently.
        // Use asymmetric smoothing: fast rise, slow decay for responsive sparkles.
        auto smooth_component = [](double current, double instant) {
            if (instant > current) {
                // Fast rise (respond quickly to acceleration events).
                return current * SPARKLE_ACCEL_SMOOTHING
                    + instant * (1.0 - SPARKLE_ACCEL_SMOOTHING);
            }
            else {
                // Slower decay (sparkles linger briefly after acceleration).
                return current * 0.92 + instant * 0.08;
            }
        };
        smoothed_acceleration_.x = smooth_component(smoothed_acceleration_.x, instant_accel.x);
        smoothed_acceleration_.y = smooth_component(smoothed_acceleration_.y, instant_accel.y);

        // Log acceleration stats every 10 frames.
        if (frame_counter_ % 10 == 0) {
            double smoothed_mag = smoothed_acceleration_.magnitude();
            LOG_INFO(
                Brain,
                "Duck {}: pos=({},{}), vel=({:.1f},{:.1f}), dv=({:.2f},{:.2f}), "
                "smooth=({:.1f},{:.1f}), mag={:.1f}",
                id_,
                anchor_cell_.x,
                anchor_cell_.y,
                cell.velocity.x,
                cell.velocity.y,
                velocity_change.x,
                velocity_change.y,
                smoothed_acceleration_.x,
                smoothed_acceleration_.y,
                smoothed_mag);
        }

        // Update previous velocity for next frame.
        previous_velocity_ = cell.velocity;
    }

    // Compute magnitude from smoothed components.
    float smoothed_mag = static_cast<float>(smoothed_acceleration_.magnitude());
    int desired_count = getDesiredSparkleCount(smoothed_mag);

    // Spawn new sparkles if below desired count.
    // Use previous_velocity_ which now holds the current duck velocity.
    while (static_cast<int>(sparkles_.size()) < desired_count) {
        spawnSparkle(previous_velocity_);
    }

    // Remove oldest sparkles if above desired count.
    while (static_cast<int>(sparkles_.size()) > desired_count) {
        sparkles_.pop_back();
    }
}

bool Duck::isSolidCell(const World& world, int x, int y) const
{
    const WorldData& data = world.getData();

    // Out of bounds is solid (prevents escape).
    if (x < 0 || y < 0 || x >= data.width || y >= data.height) {
        return true;
    }

    const Cell& cell = data.at(x, y);

    // Consider a cell solid if it has significant fill with a non-air material.
    if (cell.material_type == Material::EnumType::Air || cell.fill_ratio < 0.5f) {
        return false;
    }

    // Solid materials: WALL, WOOD, METAL, DIRT, SAND, etc.
    return true;
}

int Duck::getDesiredSparkleCount(float acceleration) const
{
    // Below floor, no sparkles (filters out noise during constant-velocity running).
    if (acceleration < SPARKLE_ACCELERATION_FLOOR) {
        return MIN_SPARKLES;
    }

    // Linear interpolation: floor -> MAX maps to MIN_SPARKLES -> Duck::MAX_SPARKLES.
    float range = SPARKLE_ACCELERATION_MAX - SPARKLE_ACCELERATION_FLOOR;
    float t = std::min((acceleration - SPARKLE_ACCELERATION_FLOOR) / range, 1.0f);
    int desired = MIN_SPARKLES + static_cast<int>(t * (Duck::MAX_SPARKLES - MIN_SPARKLES));

    return std::clamp(desired, MIN_SPARKLES, Duck::MAX_SPARKLES);
}

void Duck::spawnSparkle(const Vector2d& duck_velocity)
{
    DuckSparkle sparkle;

    // Spawn at duck position with slight random offset.
    std::uniform_real_distribution<float> offset_dist(-0.3f, 0.3f);
    sparkle.position.x = static_cast<float>(anchor_cell_.x) + offset_dist(sparkle_rng_);
    sparkle.position.y = static_cast<float>(anchor_cell_.y) + offset_dist(sparkle_rng_);

    // Initial velocity: duck velocity + random burst in random direction.
    constexpr float BURST_STRENGTH = 3.0f;
    std::uniform_real_distribution<float> angle_dist(0.0f, 2.0f * 3.14159265f);
    std::uniform_real_distribution<float> magnitude_dist(0.0f, 1.0f);
    float angle = angle_dist(sparkle_rng_);
    float magnitude = magnitude_dist(sparkle_rng_) * BURST_STRENGTH;
    float burst_x = magnitude * std::cos(angle);
    float burst_y = magnitude * std::sin(angle);
    sparkle.velocity.x = static_cast<float>(duck_velocity.x * 0.5) + burst_x;
    sparkle.velocity.y = static_cast<float>(duck_velocity.y * 0.5) + burst_y;

    LOG_DEBUG(
        Brain,
        "Sparkle spawn: duck_vel=({:.1f},{:.1f}), burst=({:.1f},{:.1f}), final=({:.1f},{:.1f})",
        duck_velocity.x,
        duck_velocity.y,
        burst_x,
        burst_y,
        sparkle.velocity.x,
        sparkle.velocity.y);

    // Randomize lifetime a bit.
    std::uniform_real_distribution<float> life_dist(0.7f, 1.0f);
    sparkle.lifetime = life_dist(sparkle_rng_);
    sparkle.max_lifetime = sparkle.lifetime;

    sparkles_.push_back(sparkle);
}

void Duck::setHandheldLight(std::unique_ptr<LightHandHeld> light)
{
    handheld_light_ = std::move(light);
}

void Duck::updateHandheldLight(World& world, double deltaTime)
{
    // Minimum distance outside cell edge to position the light.
    constexpr double LIGHT_EDGE_OFFSET = 0.1;
    // Desired offset from duck's position to light position.
    constexpr double LIGHT_OFFSET = 0.5;
    // Vertical offset to position light at hand level, not feet level.
    constexpr double LIGHT_VERTICAL_OFFSET = -0.5;

    if (!handheld_light_) {
        return;
    }

    if (!world.getData().inBounds(anchor_cell_.x, anchor_cell_.y)) {
        return;
    }

    const Cell& cell = world.getData().at(anchor_cell_.x, anchor_cell_.y);
    const bool facing_right = facing_.x > 0.0f;

    // Duck's actual world position using sub-cell COM.
    double duck_world_x = static_cast<double>(anchor_cell_.x) + 0.5 + cell.com.x * 0.5;

    // Position light with smooth movement but clamped to stay outside the duck's cell.
    // This gives smooth movement when COM is on the facing side, clamped otherwise.
    Vector2d position;
    if (facing_right) {
        double desired_x = duck_world_x + LIGHT_OFFSET;
        double min_x = static_cast<double>(anchor_cell_.x) + 1.0 + LIGHT_EDGE_OFFSET;
        position.x = std::max(desired_x, min_x);
    }
    else {
        double desired_x = duck_world_x - LIGHT_OFFSET;
        double max_x = static_cast<double>(anchor_cell_.x) - LIGHT_EDGE_OFFSET;
        position.x = std::min(desired_x, max_x);
    }
    position.y =
        static_cast<double>(anchor_cell_.y) + 0.5 + cell.com.y * 0.5 + LIGHT_VERTICAL_OFFSET;

    handheld_light_->update(world.getLightManager(), position, facing_right, deltaTime);
}

} // namespace DirtSim
