#include "Duck.h"
#include "OrganismSensoryData.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"

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
    }
    else if (std::abs(cell.velocity.x) > 0.1) {
        facing_.x = (cell.velocity.x > 0) ? 1.0f : -1.0f;
        facing_.y = 0.0f;
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
    SensoryUtils::gatherMaterialHistograms<DuckSensoryData::GRID_SIZE, DuckSensoryData::NUM_MATERIALS>(
        world, anchor_cell_, data.material_histograms, data.world_offset, true);

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

} // namespace DirtSim
