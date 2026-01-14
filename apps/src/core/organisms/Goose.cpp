#include "Goose.h"
#include "OrganismManager.h"
#include "components/RigidBodyComponent.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include <cmath>

namespace {
constexpr float WALK_FORCE = 10.0f;
constexpr float JUMP_FORCE = 150.0f;
constexpr double VERTICAL_MARGIN = 0.25; // Matches EntityRenderer offset.
} // namespace

namespace DirtSim {

Goose::Goose(OrganismId id, std::unique_ptr<GooseBrain> brain)
    : Organism::Body(id, OrganismType::GOOSE),
      brain_(std::move(brain)),
      rigidBody_(std::make_unique<RigidBodyComponent>(Material::EnumType::Wood))
{
    // Initialize local shape with a single cell at origin.
    rigidBody_->addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);

    // Keep base class local_shape in sync for mass computation.
    local_shape.push_back(
        LocalCell{ .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });

    recomputeMass();
    recomputeCenterOfMass();
}

Goose::~Goose() = default;

Vector2i Goose::getAnchorCell() const
{
    return Vector2i{ static_cast<int>(std::floor(position.x)),
                     static_cast<int>(std::floor(position.y)) };
}

void Goose::setAnchorCell(Vector2i pos)
{
    position.x = static_cast<double>(pos.x) + 0.5;
    position.y = static_cast<double>(pos.y) + 0.5;
}

void Goose::update(World& world, double deltaTime)
{
    age_seconds_ += deltaTime;
    frame_counter_++;

    // Brain decides what to do using current state (on_ground_ from previous frame).
    if (brain_) {
        GooseSensoryData sensory{ .position = getAnchorCell(),
                                  .velocity = velocity,
                                  .on_ground = on_ground_,
                                  .facing_x = getFacing().x,
                                  .delta_time_seconds = deltaTime };
        brain_->think(*this, sensory, deltaTime);
    }

    // Compute external forces from brain decisions.
    Vector2d externalForce{ 0.0, 0.0 };

    if (on_ground_ && std::abs(walk_direction_) > 0.01f) {
        externalForce.x = walk_direction_ * WALK_FORCE;
    }

    if (jump_requested_ && on_ground_) {
        double gravity = world.getPhysicsSettings().gravity;
        double jumpDirection = (gravity >= 0) ? -1.0 : 1.0;
        externalForce.y = jumpDirection * JUMP_FORCE;
        jump_requested_ = false;
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

    // Run rigid body physics.
    auto result = rigidBody_->update(
        id_,
        position,
        velocity,
        mass,
        local_shape,
        world,
        deltaTime,
        externalForce,
        VERTICAL_MARGIN);

    on_ground_ = result.on_ground;

    // Sync cells.
    occupied_cells = result.occupied_cells;
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

} // namespace DirtSim
