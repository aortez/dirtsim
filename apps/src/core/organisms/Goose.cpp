#include "Goose.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <cmath>

namespace {
// Physics constants (same as Duck for comparison).
static constexpr float WALK_FORCE = 50.0f;
static constexpr float JUMP_FORCE = 600.0f;
} // namespace

namespace DirtSim {

Goose::Goose(OrganismId id, std::unique_ptr<GooseBrain> brain)
    : Organism(id, OrganismType::GOOSE)
    , brain_(std::move(brain))
{
    // Initialize local shape with a single cell at origin.
    local_shape.push_back(LocalCell{
        .local_pos = { 0, 0 },
        .material = MaterialType::WOOD,
        .fill_ratio = 1.0 });

    // Compute mass from local shape.
    recomputeMass();
    recomputeCenterOfMass();
}

Vector2i Goose::getAnchorCell() const
{
    // Anchor is the grid cell the goose occupies (floor of continuous position).
    return Vector2i{
        static_cast<int>(std::floor(position.x)),
        static_cast<int>(std::floor(position.y))
    };
}

void Goose::setAnchorCell(Vector2i pos)
{
    // Set continuous position to center of cell.
    position.x = static_cast<double>(pos.x) + 0.5;
    position.y = static_cast<double>(pos.y) + 0.5;
}

void Goose::update(World& world, double deltaTime)
{
    age_seconds_ += deltaTime;
    frame_counter_++;

    // 1. Update ground detection based on current position.
    updateGroundDetection(world);

    // 2. Let brain decide what to do.
    if (brain_) {
        GooseSensoryData sensory;
        sensory.position = getAnchorCell();
        sensory.velocity = velocity;
        sensory.on_ground = on_ground_;
        sensory.facing_x = getFacing().x;
        sensory.delta_time_seconds = deltaTime;

        brain_->think(*this, sensory, deltaTime);
    }

    // 3. Apply movement forces (walking, jumping).
    applyMovementForces(world, deltaTime);

    // 4. Gather forces from environment (gravity, pending forces on cells).
    gatherForces(world);

    // 5. Apply accumulated forces to rigid body.
    applyForce(pending_force_, deltaTime);
    pending_force_ = { 0.0, 0.0 };

    // 6. Predict position and check for collisions.
    Vector2d desired_position{
        position.x + velocity.x * deltaTime,
        position.y + velocity.y * deltaTime
    };

    std::vector<Vector2i> predicted_cells;
    for (const auto& local : local_shape) {
        Vector2d world_pos{
            desired_position.x + static_cast<double>(local.local_pos.x),
            desired_position.y + static_cast<double>(local.local_pos.y)
        };
        predicted_cells.push_back(Vector2i{
            static_cast<int>(std::floor(world_pos.x)),
            static_cast<int>(std::floor(world_pos.y))
        });
    }

    CollisionInfo collision = detectCollisions(predicted_cells, world);

    // 7. Move if not blocked, otherwise apply collision response.
    if (!collision.blocked) {
        position = desired_position;
    } else {
        // Zero out velocity component going into the obstacle.
        Vector2d normal = collision.contact_normal;
        double v_into_surface = velocity.x * normal.x + velocity.y * normal.y;
        if (v_into_surface < 0) {
            velocity.x -= v_into_surface * normal.x;
            velocity.y -= v_into_surface * normal.y;
        }
    }

    // 8. Project organism onto grid.
    projectToGrid(world);

    // Debug logging.
    if (frame_counter_ % 60 == 0) {
        Vector2i anchor = getAnchorCell();
        LOG_INFO(
            Brain,
            "Goose {}: pos=({:.2f}, {:.2f}), grid=({}, {}), vel=({:.2f}, {:.2f}), on_ground={}",
            id_,
            position.x,
            position.y,
            anchor.x,
            anchor.y,
            velocity.x,
            velocity.y,
            on_ground_);
    }
}

GooseAction Goose::getCurrentAction() const
{
    return brain_ ? brain_->getCurrentAction() : GooseAction::WAIT;
}

void Goose::setWalkDirection(float dir)
{
    walk_direction_ = dir;
}

void Goose::jump()
{
    if (!on_ground_) {
        return;
    }

    jump_requested_ = true;
    LOG_DEBUG(Brain, "Goose {}: Jump requested", id_);
}

void Goose::gatherForces(World& world)
{
    const PhysicsSettings& physics = world.getPhysicsSettings();

    // Apply gravity.
    double gravity = physics.gravity;
    pending_force_.y += mass * gravity;

    // Gather any pending forces from occupied grid cells.
    const WorldData& data = world.getData();
    for (const auto& grid_pos : occupied_cells) {
        if (grid_pos.x < 0 || grid_pos.y < 0
            || static_cast<uint32_t>(grid_pos.x) >= data.width
            || static_cast<uint32_t>(grid_pos.y) >= data.height) {
            continue;
        }

        const Cell& cell = data.at(grid_pos.x, grid_pos.y);
        pending_force_.x += cell.pending_force.x;
        pending_force_.y += cell.pending_force.y;
    }
}

void Goose::projectToGrid(World& world)
{
    WorldData& data = world.getData();

    // Clear old projection.
    clearOldProjection(world);

    occupied_cells.clear();

    // Project each local cell to grid.
    for (const auto& local : local_shape) {
        // World position = organism position + local offset.
        Vector2d world_pos{
            position.x + static_cast<double>(local.local_pos.x),
            position.y + static_cast<double>(local.local_pos.y)
        };

        // Snap to grid (floor).
        Vector2i grid_pos{
            static_cast<int>(std::floor(world_pos.x)),
            static_cast<int>(std::floor(world_pos.y))
        };

        // Bounds check.
        if (grid_pos.x < 0 || grid_pos.y < 0
            || static_cast<uint32_t>(grid_pos.x) >= data.width
            || static_cast<uint32_t>(grid_pos.y) >= data.height) {
            continue;
        }

        Cell& cell = data.at(grid_pos.x, grid_pos.y);

        // Project cell.
        cell.organism_id = id_;
        cell.material_type = local.material;
        cell.fill_ratio = local.fill_ratio;
        cell.velocity = velocity;

        // Compute sub-cell COM from fractional position.
        // Map [0,1) within cell to [-1,1] COM space.
        double frac_x = world_pos.x - std::floor(world_pos.x);
        double frac_y = world_pos.y - std::floor(world_pos.y);
        cell.com.x = frac_x * 2.0 - 1.0;
        cell.com.y = frac_y * 2.0 - 1.0;

        // Clear pending force (we gathered it already).
        cell.pending_force = { 0.0, 0.0 };

        occupied_cells.push_back(grid_pos);
    }

    // Also update the cells_ set to match occupied_cells.
    cells_.clear();
    for (const auto& pos : occupied_cells) {
        cells_.insert(pos);
    }
}

void Goose::clearOldProjection(World& world)
{
    WorldData& data = world.getData();

    for (const auto& old_pos : occupied_cells) {
        if (old_pos.x < 0 || old_pos.y < 0
            || static_cast<uint32_t>(old_pos.x) >= data.width
            || static_cast<uint32_t>(old_pos.y) >= data.height) {
            continue;
        }

        Cell& cell = data.at(old_pos.x, old_pos.y);
        if (cell.organism_id == id_) {
            cell.organism_id = 0;
            cell.material_type = MaterialType::AIR;
            cell.fill_ratio = 0.0;
            cell.velocity = { 0.0, 0.0 };
            cell.com = { 0.0, 0.0 };
        }
    }
}

void Goose::updateGroundDetection(const World& world)
{
    const WorldData& data = world.getData();
    Vector2i anchor = getAnchorCell();

    // Check if there's solid ground below us.
    int below_y = anchor.y + 1;

    if (below_y >= static_cast<int>(data.height)) {
        // At bottom of world - consider this as ground.
        on_ground_ = true;
        return;
    }

    if (anchor.x < 0 || static_cast<uint32_t>(anchor.x) >= data.width) {
        on_ground_ = false;
        return;
    }

    const Cell& below = data.at(anchor.x, below_y);

    // Ground is any solid, non-AIR cell with significant fill.
    bool is_solid_below = (below.material_type == MaterialType::WALL
                              || below.material_type == MaterialType::DIRT
                              || below.material_type == MaterialType::SAND
                              || below.material_type == MaterialType::WOOD
                              || below.material_type == MaterialType::METAL
                              || below.material_type == MaterialType::ROOT)
        && below.fill_ratio > 0.5;

    // Also check COM position - if we're near bottom of cell, more likely grounded.
    double frac_y = position.y - std::floor(position.y);
    bool com_at_bottom = frac_y > 0.7;

    on_ground_ = is_solid_below && com_at_bottom;

    // If velocity is near zero and solid below, consider grounded.
    if (!on_ground_ && std::abs(velocity.y) < 0.1 && is_solid_below) {
        on_ground_ = true;
    }
}

void Goose::applyMovementForces(const World& world, double /*deltaTime*/)
{
    // Apply walking force directly to rigid body.
    if (on_ground_ && std::abs(walk_direction_) > 0.01f) {
        pending_force_.x += walk_direction_ * WALK_FORCE;
    }

    // Apply jump force.
    if (jump_requested_ && on_ground_) {
        double gravity = world.getPhysicsSettings().gravity;
        double jump_direction = (gravity >= 0) ? -1.0 : 1.0;
        pending_force_.y += jump_direction * JUMP_FORCE;

        jump_requested_ = false;
        on_ground_ = false;
        LOG_DEBUG(Brain, "Goose {}: Applied jump force", id_);
    }

    // Update facing direction.
    if (std::abs(walk_direction_) > 0.01f) {
        facing_.x = (walk_direction_ > 0) ? 1.0f : -1.0f;
        facing_.y = 0.0f;
    }
    else if (std::abs(velocity.x) > 0.1) {
        facing_.x = (velocity.x > 0) ? 1.0f : -1.0f;
        facing_.y = 0.0f;
    }
}

bool Goose::isSolidCell(const World& world, int x, int y) const
{
    const WorldData& data = world.getData();

    if (x < 0 || y < 0 || x >= static_cast<int>(data.width)
        || y >= static_cast<int>(data.height)) {
        return true; // Out of bounds is solid.
    }

    const Cell& cell = data.at(x, y);

    if (cell.material_type == MaterialType::AIR || cell.fill_ratio < 0.5f) {
        return false;
    }

    return true;
}

} // namespace DirtSim
