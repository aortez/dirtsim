#include "Duck.h"
#include "OrganismSensoryData.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <algorithm>

namespace DirtSim {

Duck::Duck(OrganismId id, std::unique_ptr<DuckBrain> brain)
    : Organism(id, OrganismType::DUCK)
    , brain_(std::move(brain))
{
}

void Duck::update(World& world, double deltaTime)
{
    age_seconds_ += deltaTime;
    frame_counter_++;

    // Update ground detection first.
    updateGroundDetection(world);

    // Gather sensory data and let brain decide what to do.
    if (brain_) {
        DuckSensoryData sensory = gatherSensoryData(world);
        brain_->think(*this, sensory, deltaTime);
    }

    // Apply movement intent to the cell.
    applyMovementToCell(world, deltaTime);

    // Update sparkle particle system.
    updateSparkles(world, deltaTime);

    // Log physics state every 60 frames.
    if (frame_counter_ % 60 == 0) {
        logPhysicsState(world);
    }
}

DuckAction Duck::getCurrentAction() const
{
    return brain_ ? brain_->getCurrentAction() : DuckAction::WAIT;
}

void Duck::setWalkDirection(float dir)
{
    walk_direction_ = dir;
}

void Duck::jump()
{
    if (!on_ground_) {
        return; // Can only jump when on ground.
    }

    jump_requested_ = true;
    LOG_DEBUG(Brain, "Duck {}: Jump requested", id_);
}

void Duck::updateGroundDetection(const World& world)
{
    const WorldData& data = world.getData();

    // Check if there's solid ground below us.
    int below_y = anchor_cell_.y + 1;

    if (below_y >= static_cast<int>(data.height)) {
        // At bottom of world - consider this as ground (wall border).
        on_ground_ = true;
        return;
    }

    const Cell& below = data.at(anchor_cell_.x, below_y);

    // Ground is any non-AIR, non-empty cell.
    // Also check if it's a wall or has significant fill.
    bool is_solid_below =
        (below.material_type == MaterialType::WALL || below.material_type == MaterialType::DIRT
            || below.material_type == MaterialType::SAND || below.material_type == MaterialType::WOOD
            || below.material_type == MaterialType::METAL
            || below.material_type == MaterialType::ROOT)
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
    WorldData& data = world.getData();

    // Bounds check.
    if (anchor_cell_.x < 0 || anchor_cell_.y < 0
        || static_cast<uint32_t>(anchor_cell_.x) >= data.width
        || static_cast<uint32_t>(anchor_cell_.y) >= data.height) {
        return;
    }

    Cell& cell = data.at(anchor_cell_.x, anchor_cell_.y);

    // Apply walking force (only when on ground).
    if (on_ground_ && std::abs(walk_direction_) > 0.01f) {
        Vector2d walk_force(walk_direction_ * WALK_FORCE, 0.0);
        cell.addPendingForce(walk_force);
    }

    // Apply jump force (once, when requested).
    if (jump_requested_ && on_ground_) {
        // Negative Y is up (gravity pulls positive Y).
        double gravity = world.getPhysicsSettings().gravity;
        double jump_direction = (gravity >= 0) ? -1.0 : 1.0;
        Vector2d jump_force(0.0, jump_direction * JUMP_FORCE);
        cell.addPendingForce(jump_force);

        jump_requested_ = false;
        on_ground_ = false;
        LOG_DEBUG(Brain, "Duck {}: Applied jump force {}", id_, jump_force.y);
    }

    // Update facing direction based on walk direction or velocity.
    if (std::abs(walk_direction_) > 0.01f) {
        facing_.x = (walk_direction_ > 0) ? 1.0f : -1.0f;
        facing_.y = 0.0f;
        LOG_DEBUG(Brain, "Duck {}: walk_dir={:.2f} -> facing={:.2f}", id_, walk_direction_, facing_.x);
    }
    else if (std::abs(cell.velocity.x) > 0.1) {
        facing_.x = (cell.velocity.x > 0) ? 1.0f : -1.0f;
        facing_.y = 0.0f;
        LOG_DEBUG(Brain, "Duck {}: velocity={:.2f} -> facing={:.2f}", id_, cell.velocity.x, facing_.x);
    }
}

void Duck::logPhysicsState(const World& world)
{
    const WorldData& data = world.getData();

    // Bounds check.
    if (anchor_cell_.x < 0 || anchor_cell_.y < 0
        || static_cast<uint32_t>(anchor_cell_.x) >= data.width
        || static_cast<uint32_t>(anchor_cell_.y) >= data.height) {
        LOG_INFO(Brain, "Duck {}: OUT OF BOUNDS at ({}, {})", id_, anchor_cell_.x, anchor_cell_.y);
        return;
    }

    const Cell& cell = data.at(anchor_cell_.x, anchor_cell_.y);

    LOG_INFO(Brain, "Duck {} frame {}: pos=({}, {}), com=({:.2f}, {:.2f}), vel=({:.2f}, {:.2f}), "
        "force=({:.2f}, {:.2f}), on_ground={}, material={}",
        id_, frame_counter_,
        anchor_cell_.x, anchor_cell_.y,
        cell.com.x, cell.com.y,
        cell.velocity.x, cell.velocity.y,
        cell.pending_force.x, cell.pending_force.y,
        on_ground_,
        static_cast<int>(cell.material_type));
}

DuckSensoryData Duck::gatherSensoryData(const World& world) const
{
    DuckSensoryData data;

    // Use the utility to gather material histograms centered on duck.
    // Out-of-bounds cells are marked as WALL for edge detection.
    SensoryUtils::gatherMaterialHistograms<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
        world, anchor_cell_, data.material_histograms, data.world_offset);

    // Set actual dimensions (always grid size for duck, no scaling).
    data.actual_width = DuckSensoryData::GRID_SIZE;
    data.actual_height = DuckSensoryData::GRID_SIZE;
    data.scale_factor = 1.0;

    // Fill in duck-specific fields.
    data.position = anchor_cell_;
    data.on_ground = on_ground_;
    data.facing_x = facing_.x;
    data.age_seconds = age_seconds_;

    // Get velocity from our cell.
    const WorldData& world_data = world.getData();
    if (anchor_cell_.x >= 0 && anchor_cell_.y >= 0
        && static_cast<uint32_t>(anchor_cell_.x) < world_data.width
        && static_cast<uint32_t>(anchor_cell_.y) < world_data.height) {
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
            new_x = sparkle.position.x;  // Don't move into solid.
        }

        // Vertical collision.
        if (cell_y != old_cell_y && isSolidCell(world, static_cast<int>(new_x), cell_y)) {
            sparkle.velocity.y = -sparkle.velocity.y * SPARKLE_BOUNCE;
            new_y = sparkle.position.y;  // Don't move into solid.
        }

        // World bounds check.
        if (new_x < 0.0f) {
            new_x = 0.0f;
            sparkle.velocity.x = -sparkle.velocity.x * SPARKLE_BOUNCE;
        } else if (new_x >= static_cast<float>(data.width)) {
            new_x = static_cast<float>(data.width) - 0.01f;
            sparkle.velocity.x = -sparkle.velocity.x * SPARKLE_BOUNCE;
        }

        if (new_y < 0.0f) {
            new_y = 0.0f;
            sparkle.velocity.y = -sparkle.velocity.y * SPARKLE_BOUNCE;
        } else if (new_y >= static_cast<float>(data.height)) {
            new_y = static_cast<float>(data.height) - 0.01f;
            sparkle.velocity.y = -sparkle.velocity.y * SPARKLE_BOUNCE;
        }

        // Update position.
        sparkle.position.x = new_x;
        sparkle.position.y = new_y;

        // Check for collision with duck or other solids.
        int sparkle_cell_x = static_cast<int>(sparkle.position.x);
        int sparkle_cell_y = static_cast<int>(sparkle.position.y);

        // Save old position for tunneling recovery.
        float old_x = sparkle.position.x - sparkle.velocity.x * dt;
        float old_y = sparkle.position.y - sparkle.velocity.y * dt;

        if (sparkle_cell_x == anchor_cell_.x && sparkle_cell_y == anchor_cell_.y) {
            // Sparkle is inside duck cell - find first non-solid adjacent cell.
            float dx = sparkle.position.x - static_cast<float>(anchor_cell_.x);
            float dy = sparkle.position.y - static_cast<float>(anchor_cell_.y);

            // Try pushing in order: closest edge first, then alternates.
            bool pushed = false;

            // Build priority list based on closest edge.
            struct PushOption { int dx_off; int dy_off; bool horizontal; };
            std::vector<PushOption> options;

            if (std::abs(dx) > std::abs(dy)) {
                // Horizontal closer - try that first.
                options.push_back({(dx > 0) ? 1 : -1, 0, true});
                options.push_back({0, (dy > 0) ? 1 : -1, false});
                options.push_back({(dx > 0) ? -1 : 1, 0, true});  // Opposite horizontal.
                options.push_back({0, (dy > 0) ? -1 : 1, false}); // Opposite vertical.
            } else {
                // Vertical closer - try that first.
                options.push_back({0, (dy > 0) ? 1 : -1, false});
                options.push_back({(dx > 0) ? 1 : -1, 0, true});
                options.push_back({0, (dy > 0) ? -1 : 1, false}); // Opposite vertical.
                options.push_back({(dx > 0) ? -1 : 1, 0, true});  // Opposite horizontal.
            }

            // Try each direction until we find a non-solid cell.
            for (const auto& opt : options) {
                int target_x = anchor_cell_.x + opt.dx_off;
                int target_y = anchor_cell_.y + opt.dy_off;

                if (!isSolidCell(world, target_x, target_y)) {
                    if (opt.horizontal) {
                        sparkle.position.x = static_cast<float>(target_x) + (opt.dx_off > 0 ? 0.0f : 0.99f);
                        sparkle.velocity.x = (opt.dx_off > 0 ? 1.0f : -1.0f) * std::abs(sparkle.velocity.x) * SPARKLE_BOUNCE;
                    } else {
                        sparkle.position.y = static_cast<float>(target_y) + (opt.dy_off > 0 ? 0.0f : 0.99f);
                        sparkle.velocity.y = (opt.dy_off > 0 ? 1.0f : -1.0f) * std::abs(sparkle.velocity.y) * SPARKLE_BOUNCE;
                    }
                    pushed = true;
                    break;
                }
            }

            // If completely surrounded by solids, kill the sparkle by setting lifetime to 0.
            if (!pushed) {
                sparkle.lifetime = 0.0f;
            }
        }
        // Check if sparkle tunneled into a non-duck solid cell.
        else if (isSolidCell(world, sparkle_cell_x, sparkle_cell_y)) {
            // Sparkle is inside a solid (tunneled through) - revert to previous position and bounce.
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
        std::remove_if(sparkles_.begin(), sparkles_.end(),
            [](const DuckSparkle& s) { return s.lifetime <= 0.0f; }),
        sparkles_.end());

    // Spawn or remove sparkles to match desired count based on velocity.
    // Get duck's current velocity from cell.
    const WorldData& world_data = world.getData();
    float speed = 0.0f;
    if (anchor_cell_.x >= 0 && anchor_cell_.y >= 0 &&
        static_cast<uint32_t>(anchor_cell_.x) < world_data.width &&
        static_cast<uint32_t>(anchor_cell_.y) < world_data.height) {
        const Cell& cell = world_data.at(anchor_cell_.x, anchor_cell_.y);
        speed = static_cast<float>(cell.velocity.magnitude());
    }

    int desired_count = getDesiredSparkleCount(speed);

    // Spawn new sparkles if below desired count.
    while (static_cast<int>(sparkles_.size()) < desired_count) {
        spawnSparkle();
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
    if (x < 0 || y < 0 || x >= static_cast<int>(data.width) || y >= static_cast<int>(data.height)) {
        return true;
    }

    const Cell& cell = data.at(x, y);

    // Consider a cell solid if it has significant fill with a non-air material.
    if (cell.material_type == MaterialType::AIR || cell.fill_ratio < 0.5f) {
        return false;
    }

    // Solid materials: WALL, WOOD, METAL, DIRT, SAND, etc.
    return true;
}

int Duck::getDesiredSparkleCount(float speed) const
{
    // Linear interpolation: MIN_SPARKLES at rest, MAX_SPARKLES at SPARKLE_VELOCITY_MAX.
    float t = std::min(speed / SPARKLE_VELOCITY_MAX, 1.0f);
    int desired = MIN_SPARKLES + static_cast<int>(t * (MAX_SPARKLES - MIN_SPARKLES));

    return std::clamp(desired, MIN_SPARKLES, MAX_SPARKLES);
}

void Duck::spawnSparkle()
{
    DuckSparkle sparkle;

    // Spawn at duck position with slight random offset.
    std::uniform_real_distribution<float> offset_dist(-0.3f, 0.3f);
    sparkle.position.x = static_cast<float>(anchor_cell_.x) + offset_dist(sparkle_rng_);
    sparkle.position.y = static_cast<float>(anchor_cell_.y) + offset_dist(sparkle_rng_);

    // Initial velocity - small random burst.
    std::uniform_real_distribution<float> vel_dist(-1.5f, 1.5f);
    sparkle.velocity.x = vel_dist(sparkle_rng_);
    sparkle.velocity.y = vel_dist(sparkle_rng_) - 0.5f;  // Slight upward bias.

    // Randomize lifetime a bit.
    std::uniform_real_distribution<float> life_dist(0.7f, 1.0f);
    sparkle.lifetime = life_dist(sparkle_rng_);
    sparkle.max_lifetime = sparkle.lifetime;

    sparkles_.push_back(sparkle);
}

} // namespace DirtSim
