#include "Goose.h"
#include "OrganismManager.h"
#include "components/LocalShapeProjection.h"
#include "components/RigidBodyCollisionComponent.h"
#include "components/RigidBodyPhysicsComponent.h"
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
    , collision_(std::make_unique<RigidBodyCollisionComponent>())
    , physics_(std::make_unique<RigidBodyPhysicsComponent>(MaterialType::WOOD))
    , projection_(std::make_unique<LocalShapeProjection>())
{
    // Initialize local shape with a single cell at origin.
    projection_->addCell({ 0, 0 }, MaterialType::WOOD, 1.0);

    // Also keep base class local_shape in sync for mass computation.
    local_shape.push_back(LocalCell{
        .localPos = { 0, 0 },
        .material = MaterialType::WOOD,
        .fillRatio = 1.0 });

    // Compute mass from local shape.
    recomputeMass();
    recomputeCenterOfMass();
}

Goose::~Goose() = default;

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

    // Update ground detection based on current position.
    updateGroundDetection(world);

    // Let brain decide what to do.
    if (brain_) {
        GooseSensoryData sensory;
        sensory.position = getAnchorCell();
        sensory.velocity = velocity;
        sensory.on_ground = on_ground_;
        sensory.facing_x = getFacing().x;
        sensory.delta_time_seconds = deltaTime;

        brain_->think(*this, sensory, deltaTime);
    }

    // Apply movement forces (walking, jumping).
    applyMovementForces(world, deltaTime);

    // Compute current grid positions from current position.
    std::vector<Vector2i> current_cells;
    for (const auto& local : local_shape) {
        Vector2d world_pos{
            position.x + static_cast<double>(local.localPos.x),
            position.y + static_cast<double>(local.localPos.y)
        };
        current_cells.push_back(Vector2i{
            static_cast<int>(std::floor(world_pos.x)),
            static_cast<int>(std::floor(world_pos.y))
        });
    }

    // Gather forces from environment (gravity, pressure, etc. from cells).
    physics_->gatherForces(world, current_cells);

    // Apply air resistance based on current velocity.
    physics_->applyAirResistance(world, velocity);

    // Apply accumulated forces to rigid body.
    physics_->integrate(velocity, mass, deltaTime);
    physics_->clearPendingForce();

    // Predict position and check for collisions.
    Vector2d desired_position{
        position.x + velocity.x * deltaTime,
        position.y + velocity.y * deltaTime
    };

    std::vector<Vector2i> predicted_cells;
    for (const auto& local : local_shape) {
        Vector2d world_pos{
            desired_position.x + static_cast<double>(local.localPos.x),
            desired_position.y + static_cast<double>(local.localPos.y)
        };
        predicted_cells.push_back(Vector2i{
            static_cast<int>(std::floor(world_pos.x)),
            static_cast<int>(std::floor(world_pos.y))
        });
    }

    CollisionResult collision = collision_->detect(world, id_, current_cells, predicted_cells);

    // Move if not blocked, otherwise apply collision response.
    if (!collision.blocked) {
        position = desired_position;
    } else {
        collision_->respond(collision, velocity);

        // Clamp position to stay within current cell to prevent creeping into obstacles.
        Vector2i current_cell = getAnchorCell();
        constexpr double margin = 0.01;

        if (collision.contactNormal.x != 0.0) {
            double cell_min_x = static_cast<double>(current_cell.x) + margin;
            double cell_max_x = static_cast<double>(current_cell.x + 1) - margin;
            position.x = std::clamp(position.x, cell_min_x, cell_max_x);
        }

        if (collision.contactNormal.y != 0.0) {
            double cell_min_y = static_cast<double>(current_cell.y) + margin;
            double cell_max_y = static_cast<double>(current_cell.y + 1) - margin;
            position.y = std::clamp(position.y, cell_min_y, cell_max_y);
        }
    }

    // Project organism onto grid.
    projection_->clear(world);
    projection_->project(world, id_, position, velocity);

    // Keep base class occupied_cells in sync.
    occupied_cells = projection_->getOccupiedCells();
    cells_.clear();
    for (const auto& pos : occupied_cells) {
        cells_.insert(pos);
    }

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
        physics_->addForce({ walk_direction_ * WALK_FORCE, 0.0 });
    }

    // Apply jump force.
    if (jump_requested_ && on_ground_) {
        double gravity = world.getPhysicsSettings().gravity;
        double jump_direction = (gravity >= 0) ? -1.0 : 1.0;
        physics_->addForce({ 0.0, jump_direction * JUMP_FORCE });

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

} // namespace DirtSim
