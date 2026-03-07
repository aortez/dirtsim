#include "Body.h"

#include "OrganismManager.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Organism {

namespace {
std::vector<std::pair<std::string, int>> getTopSignatureEntries(
    const std::unordered_map<std::string, int>& counts, size_t maxEntries)
{
    if (maxEntries == 0 || counts.empty()) {
        return {};
    }

    std::vector<std::pair<std::string, int>> entries;
    entries.reserve(counts.size());
    for (const auto& [signature, count] : counts) {
        entries.emplace_back(signature, count);
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second > rhs.second;
        }
        return lhs.first < rhs.first;
    });

    if (entries.size() > maxEntries) {
        entries.resize(maxEntries);
    }

    return entries;
}
} // namespace

Body::Body(OrganismId id, OrganismType type) : id_(id), type_(type)
{}

std::vector<std::pair<std::string, int>> Body::getTopCommandSignatures(size_t maxEntries) const
{
    return getTopSignatureEntries(commandSignatureCounts_, maxEntries);
}

std::vector<std::pair<std::string, int>> Body::getTopCommandOutcomeSignatures(
    size_t maxEntries) const
{
    return getTopSignatureEntries(commandOutcomeSignatureCounts_, maxEntries);
}

void Body::onCellTransfer(Vector2i from, Vector2i to)
{
    // Update anchor if it moved.
    if (from == getAnchorCell()) {
        setAnchorCell(to);
    }
}

void Body::recomputeMass()
{
    mass = 0.0;
    for (const auto& cell : local_shape) {
        double cell_mass = Material::getProperties(cell.material).density * cell.fillRatio;
        mass += cell_mass;
    }
}

void Body::recomputeCenterOfMass()
{
    if (local_shape.empty() || mass < 0.0001) {
        center_of_mass = { 0.0, 0.0 };
        return;
    }

    Vector2d weighted_sum{ 0.0, 0.0 };
    for (const auto& cell : local_shape) {
        double cell_mass = Material::getProperties(cell.material).density * cell.fillRatio;
        weighted_sum.x += static_cast<double>(cell.localPos.x) * cell_mass;
        weighted_sum.y += static_cast<double>(cell.localPos.y) * cell_mass;
    }

    center_of_mass.x = weighted_sum.x / mass;
    center_of_mass.y = weighted_sum.y / mass;
}

void Body::integratePosition(double dt)
{
    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
}

void Body::applyForce(Vector2d force, double dt)
{
    if (mass < 0.0001) {
        return;
    }

    Vector2d acceleration{ force.x / mass, force.y / mass };
    velocity.x += acceleration.x * dt;
    velocity.y += acceleration.y * dt;
}

CollisionInfo Body::detectCollisions(
    const std::vector<Vector2i>& target_cells, const World& world) const
{
    CollisionInfo info;
    const WorldData& data = world.getData();
    Vector2d normal_sum{ 0.0, 0.0 };

    for (const auto& cell_pos : target_cells) {
        // Check world boundaries.
        if (!data.inBounds(cell_pos.x, cell_pos.y)) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            // Boundary normal points inward.
            if (cell_pos.x < 0) normal_sum.x += 1.0;
            if (cell_pos.x >= data.width) normal_sum.x -= 1.0;
            if (cell_pos.y < 0) normal_sum.y += 1.0;
            if (cell_pos.y >= data.height) normal_sum.y -= 1.0;
            continue;
        }

        const Cell& cell = data.at(cell_pos.x, cell_pos.y);

        // Check for WALL.
        if (cell.material_type == Material::EnumType::Wall) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum.y -= 1.0; // Assume floor for now, refine later.
            continue;
        }

        // Check for other organism.
        OrganismId cell_org = world.getOrganismManager().at(cell_pos);
        if (cell_org != INVALID_ORGANISM_ID && cell_org != id_) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            continue;
        }

        // Check for dense solid material (skip our own cells).
        bool is_solid = cell.material_type == Material::EnumType::Dirt
            || cell.material_type == Material::EnumType::Sand
            || cell.material_type == Material::EnumType::Wood
            || cell.material_type == Material::EnumType::Metal
            || cell.material_type == Material::EnumType::Root;
        if (is_solid && cell.fill_ratio > 0.8 && cell_org != id_) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            continue;
        }
    }

    // Compute average contact normal.
    if (info.blocked && (normal_sum.x != 0.0 || normal_sum.y != 0.0)) {
        double len = std::sqrt(normal_sum.x * normal_sum.x + normal_sum.y * normal_sum.y);
        if (len > 0.0001) {
            info.contact_normal.x = normal_sum.x / len;
            info.contact_normal.y = normal_sum.y / len;
        }
    }

    return info;
}

void Body::attachLight(LightHandle handle, bool follows_facing)
{
    attached_lights_.push_back({ std::move(handle), follows_facing });
}

void Body::detachLight(LightId id)
{
    auto it = std::remove_if(
        attached_lights_.begin(), attached_lights_.end(), [id](const LightAttachment& attachment) {
            return attachment.handle.id() == id;
        });
    attached_lights_.erase(it, attached_lights_.end());
}

void Body::recordCommandSignature(std::string signature)
{
    if (signature.empty()) {
        return;
    }
    ++commandSignatureCounts_[std::move(signature)];
}

void Body::recordCommandOutcomeSignature(std::string signature)
{
    if (signature.empty()) {
        return;
    }
    ++commandOutcomeSignatureCounts_[std::move(signature)];
}

void Body::updateAttachedLights(World& world, double deltaTime)
{
    if (attached_lights_.empty()) {
        return;
    }

    // Use sub-cell position from cell's COM for smooth light movement.
    Vector2i anchor = getAnchorCell();
    Vector2f anchor_pos{ static_cast<float>(anchor.x) + 0.5f, static_cast<float>(anchor.y) + 0.5f };
    if (world.getData().inBounds(anchor.x, anchor.y)) {
        const Cell& cell = world.getData().at(anchor.x, anchor.y);
        anchor_pos.x += cell.com.x * 0.5f;
        anchor_pos.y += cell.com.y * 0.5f;
    }
    LightManager& lights = world.getLightManager();

    for (auto& attachment : attached_lights_) {
        LightId light_id = attachment.handle.id();

        if (auto* spot = lights.getLight<SpotLight>(light_id)) {
            spot->position = anchor_pos;
            if (attachment.follows_facing) {
                spot->direction = std::atan2(facing_.y, facing_.x);
            }
        }
        else if (auto* rotating = lights.getLight<RotatingLight>(light_id)) {
            rotating->position = anchor_pos;
            if (rotating->rotation_speed == 0.0f && attachment.follows_facing) {
                rotating->direction = std::atan2(facing_.y, facing_.x);
            }
            else if (rotating->rotation_speed != 0.0f) {
                rotating->direction += rotating->rotation_speed * static_cast<float>(deltaTime);
                while (rotating->direction >= 2.0f * static_cast<float>(M_PI)) {
                    rotating->direction -= 2.0f * static_cast<float>(M_PI);
                }
                while (rotating->direction < 0.0f) {
                    rotating->direction += 2.0f * static_cast<float>(M_PI);
                }
            }
        }
        else if (auto* point = lights.getLight<PointLight>(light_id)) {
            point->position = anchor_pos;
        }
    }
}

} // namespace Organism
} // namespace DirtSim
