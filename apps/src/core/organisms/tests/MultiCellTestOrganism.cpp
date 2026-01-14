#include "MultiCellTestOrganism.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/organisms/components/RigidBodyComponent.h"
#include <cmath>

namespace DirtSim {

MultiCellTestOrganism::MultiCellTestOrganism(OrganismId id, MultiCellShape shape)
    : Organism::Body(id, OrganismType::TREE),
      shape_(shape),
      rigidBody_(std::make_unique<RigidBodyComponent>(Material::EnumType::Wood))
{
    initializeShape();
    recomputeMass();
    recomputeCenterOfMass();
}

MultiCellTestOrganism::~MultiCellTestOrganism() = default;

void MultiCellTestOrganism::initializeShape()
{
    switch (shape_) {
        case MultiCellShape::STICK:
            // Two horizontal cells: XX (anchor at left cell).
            rigidBody_->addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
            rigidBody_->addCell({ 1, 0 }, Material::EnumType::Wood, 1.0);
            local_shape.push_back(LocalCell{
                .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            local_shape.push_back(LocalCell{
                .localPos = { 1, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            break;

        case MultiCellShape::LSHAPE:
            // L shape:  X   (anchor at corner).
            //          XX
            rigidBody_->addCell({ 0, -1 }, Material::EnumType::Wood, 1.0);
            rigidBody_->addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
            rigidBody_->addCell({ 1, 0 }, Material::EnumType::Wood, 1.0);
            local_shape.push_back(LocalCell{
                .localPos = { 0, -1 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            local_shape.push_back(LocalCell{
                .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            local_shape.push_back(LocalCell{
                .localPos = { 1, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            break;

        case MultiCellShape::COLUMN:
            // Three vertical cells (anchor at bottom).
            rigidBody_->addCell({ 0, -2 }, Material::EnumType::Wood, 1.0);
            rigidBody_->addCell({ 0, -1 }, Material::EnumType::Wood, 1.0);
            rigidBody_->addCell({ 0, 0 }, Material::EnumType::Wood, 1.0);
            local_shape.push_back(LocalCell{
                .localPos = { 0, -2 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            local_shape.push_back(LocalCell{
                .localPos = { 0, -1 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            local_shape.push_back(LocalCell{
                .localPos = { 0, 0 }, .material = Material::EnumType::Wood, .fillRatio = 1.0 });
            break;
    }
}

Vector2i MultiCellTestOrganism::getAnchorCell() const
{
    return Vector2i{ static_cast<int>(std::floor(position.x)),
                     static_cast<int>(std::floor(position.y)) };
}

void MultiCellTestOrganism::setAnchorCell(Vector2i pos)
{
    position.x = static_cast<double>(pos.x) + 0.5;
    position.y = static_cast<double>(pos.y) + 0.5;
}

std::vector<Vector2i> MultiCellTestOrganism::getGridPositions() const
{
    std::vector<Vector2i> result;
    for (const auto& local : local_shape) {
        Vector2d worldPos{ position.x + static_cast<double>(local.localPos.x),
                           position.y + static_cast<double>(local.localPos.y) };
        result.push_back(Vector2i{ static_cast<int>(std::floor(worldPos.x)),
                                   static_cast<int>(std::floor(worldPos.y)) });
    }
    return result;
}

void MultiCellTestOrganism::update(World& world, double deltaTime)
{
    age_seconds_ += deltaTime;

    auto result = rigidBody_->update(
        id_, position, velocity, mass, local_shape, world, deltaTime, external_force_);

    on_ground_ = result.on_ground;

    // Sync cells.
    occupied_cells = result.occupied_cells;
    cells_.clear();
    for (const auto& pos : occupied_cells) {
        cells_.insert(pos);
    }
}

} // namespace DirtSim
