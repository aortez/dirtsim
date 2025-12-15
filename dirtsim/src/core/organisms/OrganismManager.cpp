#include "OrganismManager.h"
#include "Duck.h"
#include "DuckBrain.h"
#include "Tree.h"
#include "brains/RuleBasedBrain.h"
#include "core/Cell.h"
#include "core/GridOfCells.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <cassert>
#include <spdlog/spdlog.h>

namespace DirtSim {

void OrganismManager::update(World& world, double deltaTime)
{
    for (auto& [id, organism] : organisms_) {
        if (organism->isActive()) {
            organism->update(world, deltaTime);
        }
    }
}

OrganismId OrganismManager::createTree(
    World& world,
    uint32_t x,
    uint32_t y,
    std::unique_ptr<TreeBrain> brain)
{
    OrganismId id = next_id_++;

    // Use default brain if none provided.
    if (!brain) {
        brain = std::make_unique<RuleBasedBrain>();
    }

    auto tree = std::make_unique<Tree>(id, std::move(brain));

    Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
    tree->setAnchorCell(pos);
    tree->setEnergy(150.0); // Starting energy for tree growth.

    // Place seed material in world.
    world.addMaterialAtCell(x, y, MaterialType::SEED, 1.0);

    // Track cell ownership.
    tree->getCells().insert(pos);
    cell_to_organism_[pos] = id;
    world.getData().at(x, y).organism_id = id;

    LOG_INFO(Tree, "OrganismManager: Planted tree {} at ({}, {})", id, x, y);

    organisms_.emplace(id, std::move(tree));

    return id;
}

OrganismId OrganismManager::createDuck(
    World& world,
    uint32_t x,
    uint32_t y,
    std::unique_ptr<DuckBrain> brain)
{
    OrganismId id = next_id_++;

    // Use default brain if none provided.
    if (!brain) {
        brain = std::make_unique<RandomDuckBrain>();
    }

    auto duck = std::make_unique<Duck>(id, std::move(brain));

    Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
    duck->setAnchorCell(pos);

    // Place duck as WOOD cell in world.
    world.addMaterialAtCell(x, y, MaterialType::WOOD, 1.0);

    // Track cell ownership.
    duck->getCells().insert(pos);
    cell_to_organism_[pos] = id;
    world.getData().at(x, y).organism_id = id;

    spdlog::info("OrganismManager: Created duck {} at ({}, {})", id, x, y);

    organisms_.emplace(id, std::move(duck));

    return id;
}

void OrganismManager::removeOrganismFromWorld(World& world, OrganismId id)
{
    auto* organism = getOrganism(id);
    if (!organism) {
        spdlog::warn("OrganismManager: Attempted to remove non-existent organism {}", id);
        return;
    }

    WorldData& data = world.getData();

    // Clear all cells owned by this organism from the world.
    for (const auto& pos : organism->getCells()) {
        if (pos.x >= 0 && pos.y >= 0 &&
            static_cast<uint32_t>(pos.x) < data.width &&
            static_cast<uint32_t>(pos.y) < data.height) {
            data.at(pos.x, pos.y) = Cell();
        }
    }

    spdlog::info("OrganismManager: Removed organism {} from world ({} cells cleared)",
        id, organism->getCells().size());

    // Now do the internal cleanup.
    removeOrganism(id);
}

void OrganismManager::removeOrganism(OrganismId id)
{
    auto it = organisms_.find(id);
    assert(it != organisms_.end() && "removeOrganism called with non-existent organism ID");

    // Remove cell ownership tracking.
    for (const auto& pos : it->second->getCells()) {
        cell_to_organism_.erase(pos);
    }

    organisms_.erase(it);
}

void OrganismManager::clear()
{
    spdlog::info("OrganismManager: Clearing all organisms (count={})", organisms_.size());
    organisms_.clear();
    cell_to_organism_.clear();
}

Organism* OrganismManager::getOrganism(OrganismId id)
{
    auto it = organisms_.find(id);
    return it != organisms_.end() ? it->second.get() : nullptr;
}

const Organism* OrganismManager::getOrganism(OrganismId id) const
{
    auto it = organisms_.find(id);
    return it != organisms_.end() ? it->second.get() : nullptr;
}

Tree* OrganismManager::getTree(OrganismId id)
{
    auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::TREE) {
        return static_cast<Tree*>(organism);
    }
    return nullptr;
}

const Tree* OrganismManager::getTree(OrganismId id) const
{
    const auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::TREE) {
        return static_cast<const Tree*>(organism);
    }
    return nullptr;
}

Duck* OrganismManager::getDuck(OrganismId id)
{
    auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::DUCK) {
        return static_cast<Duck*>(organism);
    }
    return nullptr;
}

const Duck* OrganismManager::getDuck(OrganismId id) const
{
    const auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::DUCK) {
        return static_cast<const Duck*>(organism);
    }
    return nullptr;
}

OrganismId OrganismManager::getOrganismAtCell(const Vector2i& pos) const
{
    auto it = cell_to_organism_.find(pos);
    return it != cell_to_organism_.end() ? it->second : INVALID_ORGANISM_ID;
}

void OrganismManager::addCellToOrganism(World& world, OrganismId id, Vector2i pos)
{
    auto* organism = getOrganism(id);
    if (!organism) {
        spdlog::warn("OrganismManager: Attempted to add cell to non-existent organism {}", id);
        return;
    }

    organism->getCells().insert(pos);
    cell_to_organism_[pos] = id;
    world.getData().at(pos.x, pos.y).organism_id = id;

    spdlog::debug(
        "OrganismManager: Added cell ({},{}) to organism {} (now {} cells tracked)",
        pos.x,
        pos.y,
        id,
        organism->getCells().size());
}

void OrganismManager::removeCellsFromOrganism(OrganismId id, const std::vector<Vector2i>& positions)
{
    auto* organism = getOrganism(id);
    if (!organism) {
        spdlog::warn(
            "OrganismManager: Attempted to remove cells from non-existent organism {}", id);
        return;
    }

    for (const auto& pos : positions) {
        organism->getCells().erase(pos);
        cell_to_organism_.erase(pos);
    }

    spdlog::debug(
        "OrganismManager: Removed {} cells from organism {} (now {} cells tracked)",
        positions.size(),
        id,
        organism->getCells().size());
}

void OrganismManager::notifyTransfers(const std::vector<OrganismTransfer>& transfers)
{
    if (transfers.empty()) {
        return;
    }

    spdlog::debug("OrganismManager::notifyTransfers called with {} transfers", transfers.size());

    // Batch transfers by organism ID for efficient processing.
    std::unordered_map<OrganismId, std::vector<const OrganismTransfer*>> transfers_by_organism;

    for (const auto& transfer : transfers) {
        transfers_by_organism[transfer.organism_id].push_back(&transfer);
    }

    // Update each affected organism's cell tracking.
    for (const auto& [organism_id, organism_transfers] : transfers_by_organism) {
        auto* organism = getOrganism(organism_id);
        if (!organism) {
            spdlog::warn(
                "OrganismManager: Received transfers for non-existent organism {}", organism_id);
            continue;
        }

        for (const OrganismTransfer* transfer : organism_transfers) {
            // Add destination to organism's cell set.
            organism->getCells().insert(transfer->to_pos);
            cell_to_organism_[transfer->to_pos] = organism_id;

            // Let organism handle anchor updates and bone endpoint updates.
            organism->onCellTransfer(transfer->from_pos, transfer->to_pos);

            // Note: We don't remove from_pos yet - source cell might still have material.
            // The cleanup will happen in a separate pass or when cell becomes fully empty.
        }

        spdlog::trace(
            "OrganismManager: Processed {} transfers for organism {} (now {} cells tracked)",
            organism_transfers.size(),
            organism_id,
            organism->getCells().size());
    }
}

void OrganismManager::applyBoneForces(World& world, double /*deltaTime*/)
{
    WorldData& data = world.getData();
    GridOfCells& grid = world.getGrid();
    constexpr double BONE_FORCE_SCALE = 1.0;
    constexpr double BONE_DAMPING_SCALE = 1.0;
    constexpr double MAX_BONE_FORCE = 0.5;

    // Clear bone force debug info for all organism cells.
    for (auto& [id, organism] : organisms_) {
        for (const auto& pos : organism->getCells()) {
            if (static_cast<uint32_t>(pos.x) < data.width
                && static_cast<uint32_t>(pos.y) < data.height) {
                grid.debugAt(pos.x, pos.y).accumulated_bone_force = {};
            }
        }
    }

    for (auto& [organism_id, organism] : organisms_) {
        for (const Bone& bone : organism->getBones()) {
            if (static_cast<uint32_t>(bone.cell_a.x) >= data.width
                || static_cast<uint32_t>(bone.cell_a.y) >= data.height
                || static_cast<uint32_t>(bone.cell_b.x) >= data.width
                || static_cast<uint32_t>(bone.cell_b.y) >= data.height) {
                continue;
            }

            Cell& cell_a = data.at(bone.cell_a.x, bone.cell_a.y);
            Cell& cell_b = data.at(bone.cell_b.x, bone.cell_b.y);

            // Skip if either cell no longer belongs to this organism.
            if (cell_a.organism_id != organism_id || cell_b.organism_id != organism_id) {
                continue;
            }

            // World positions including COM offset.
            Vector2d pos_a = Vector2d(bone.cell_a.x, bone.cell_a.y) + cell_a.com * 0.5;
            Vector2d pos_b = Vector2d(bone.cell_b.x, bone.cell_b.y) + cell_b.com * 0.5;

            Vector2d delta = pos_b - pos_a;
            double current_dist = delta.magnitude();

            if (current_dist < 1e-6) continue;

            double error = current_dist - bone.rest_distance;
            Vector2d direction = delta / current_dist;

            // Spring force: F_spring = stiffness * error * direction.
            Vector2d spring_force = direction * error * bone.stiffness * BONE_FORCE_SCALE;

            // Damping force: oppose stretching along bone.
            Vector2d relative_velocity = cell_b.velocity - cell_a.velocity;
            double velocity_along_bone = relative_velocity.dot(direction);
            Vector2d damping_along =
                direction * velocity_along_bone * bone.stiffness * BONE_DAMPING_SCALE;

            // Apply spring + along-bone damping (symmetric - both cells).
            Vector2d symmetric_force = spring_force + damping_along;

            // Limit maximum bone force to prevent yanking on transfers.
            double force_mag = symmetric_force.magnitude();
            if (force_mag > MAX_BONE_FORCE) {
                symmetric_force = symmetric_force.normalize() * MAX_BONE_FORCE;
            }

            cell_a.addPendingForce(symmetric_force);
            cell_b.addPendingForce(symmetric_force * -1.0);

            // Store symmetric forces in debug info.
            grid.debugAt(bone.cell_a.x, bone.cell_a.y).accumulated_bone_force += symmetric_force;
            grid.debugAt(bone.cell_b.x, bone.cell_b.y).accumulated_bone_force +=
                symmetric_force * -1.0;

            // Hinge-point rotational damping (if configured).
            if (bone.hinge_end != HingeEnd::NONE && bone.rotational_damping != 0.0) {
                // Determine which cell is the hinge (pivot) and which rotates.
                bool a_is_hinge = (bone.hinge_end == HingeEnd::CELL_A);
                Cell& rotating_cell = a_is_hinge ? cell_b : cell_a;
                Vector2i rotating_pos = a_is_hinge ? bone.cell_b : bone.cell_a;

                // Radius vector from hinge to rotating cell.
                Vector2d radius = a_is_hinge ? delta : (delta * -1.0);

                // Tangent direction (perpendicular to radius, for rotation).
                Vector2d tangent = Vector2d(-radius.y, radius.x).normalize();

                // Tangential velocity (how fast rotating around hinge).
                double tangential_velocity = rotating_cell.velocity.dot(tangent);

                // Rotational damping opposes tangential motion.
                Vector2d rot_damping_force =
                    tangent * (-tangential_velocity) * bone.rotational_damping;

                // Apply to rotating cell only (hinge stays fixed).
                rotating_cell.addPendingForce(rot_damping_force);
                grid.debugAt(rotating_pos.x, rotating_pos.y).accumulated_bone_force +=
                    rot_damping_force;
            }
        }
    }
}

} // namespace DirtSim
