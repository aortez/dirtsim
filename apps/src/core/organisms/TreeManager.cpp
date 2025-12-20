#include "TreeManager.h"
#include "brains/RuleBasedBrain.h"
#include "core/Cell.h"
#include "core/GridOfCells.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <algorithm>
#include <queue>
#include <random>
#include <unordered_set>

namespace DirtSim {

void TreeManager::update(World& world, double deltaTime)
{
    for (auto& [id, tree] : trees_) {
        tree.update(world, deltaTime);
    }
}

TreeId TreeManager::plantSeed(World& world, uint32_t x, uint32_t y)
{
    TreeId id = next_tree_id_++;

    auto brain = std::make_unique<RuleBasedBrain>();
    Tree tree(id, std::move(brain));

    Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
    tree.seed_position = pos;
    tree.total_energy = 150.0; // Starting energy for tree growth.

    world.addMaterialAtCell(x, y, MaterialType::SEED, 1.0);

    tree.cells.insert(pos);
    cell_to_tree_[pos] = id;

    world.getData().at(x, y).organism_id = id;

    LOG_INFO(Tree, "TreeManager: Planted seed for tree {} at ({}, {})", id, x, y);

    trees_.emplace(id, std::move(tree));

    return id;
}

void TreeManager::removeTree(TreeId id)
{
    auto it = trees_.find(id);
    if (it == trees_.end()) {
        LOG_WARN(Tree, "TreeManager: Attempted to remove non-existent tree {}", id);
        return;
    }

    // Remove cell ownership tracking.
    for (const auto& pos : it->second.cells) {
        cell_to_tree_.erase(pos);
    }

    // Remove tree.
    trees_.erase(it);

    LOG_INFO(Tree, "TreeManager: Removed tree {}", id);
}

void TreeManager::clear()
{
    LOG_INFO(Tree, "TreeManager: Clearing all trees (count={})", trees_.size());
    trees_.clear();
    cell_to_tree_.clear();
}

Tree* TreeManager::getTree(TreeId id)
{
    auto it = trees_.find(id);
    return it != trees_.end() ? &it->second : nullptr;
}

const Tree* TreeManager::getTree(TreeId id) const
{
    auto it = trees_.find(id);
    return it != trees_.end() ? &it->second : nullptr;
}

TreeId TreeManager::getTreeAtCell(const Vector2i& pos) const
{
    auto it = cell_to_tree_.find(pos);
    return it != cell_to_tree_.end() ? it->second : INVALID_TREE_ID;
}

void TreeManager::notifyTransfers(const std::vector<OrganismTransfer>& transfers)
{
    if (!transfers.empty()) {
        spdlog::info("TreeManager::notifyTransfers called with {} transfers", transfers.size());
    }

    // Batch transfers by tree ID for efficient processing.
    std::unordered_map<TreeId, std::vector<const OrganismTransfer*>> transfers_by_tree;

    for (const auto& transfer : transfers) {
        transfers_by_tree[transfer.organism_id].push_back(&transfer);
    }

    // Update each affected tree's cell tracking.
    for (const auto& [tree_id, tree_transfers] : transfers_by_tree) {
        auto tree_it = trees_.find(tree_id);
        if (tree_it == trees_.end()) {
            LOG_WARN(Tree, "TreeManager: Received transfers for non-existent tree {}", tree_id);
            continue;
        }

        Tree& tree = tree_it->second;

        for (const OrganismTransfer* transfer : tree_transfers) {
            // Add destination to tree's cell set.
            tree.cells.insert(transfer->to_pos);
            cell_to_tree_[transfer->to_pos] = tree_id;

            // If the seed cell is moving, update seed_position to track it.
            if (transfer->from_pos == tree.seed_position) {
                tree.seed_position = transfer->to_pos;
                LOG_DEBUG(
                    Tree,
                    "TreeManager: Tree {} seed moved from ({}, {}) to ({}, {})",
                    tree_id,
                    transfer->from_pos.x,
                    transfer->from_pos.y,
                    transfer->to_pos.x,
                    transfer->to_pos.y);
            }

            // Update bone endpoints when cells move.
            // Any bone referencing from_pos now needs to reference to_pos.
            spdlog::info(
                "TreeManager: Processing transfer ({},{}) -> ({},{}) for tree {} with {} bones",
                transfer->from_pos.x,
                transfer->from_pos.y,
                transfer->to_pos.x,
                transfer->to_pos.y,
                tree_id,
                tree.bones.size());

            for (Bone& bone : tree.bones) {
                if (bone.cell_a == transfer->from_pos) {
                    bone.cell_a = transfer->to_pos;
                    spdlog::info(
                        "TreeManager: Updated bone cell_a from ({},{}) to ({},{})",
                        transfer->from_pos.x,
                        transfer->from_pos.y,
                        transfer->to_pos.x,
                        transfer->to_pos.y);
                }
                if (bone.cell_b == transfer->from_pos) {
                    bone.cell_b = transfer->to_pos;
                    spdlog::info(
                        "TreeManager: Updated bone cell_b from ({},{}) to ({},{})",
                        transfer->from_pos.x,
                        transfer->from_pos.y,
                        transfer->to_pos.x,
                        transfer->to_pos.y);
                }
            }

            // Note: We don't remove from_pos yet - source cell might still have material.
            // The cleanup will happen in a separate pass or when cell becomes fully empty.
        }

        LOG_TRACE(
            Tree,
            "TreeManager: Processed {} transfers for tree {} (now {} cells tracked)",
            tree_transfers.size(),
            tree_id,
            tree.cells.size());
    }
}

void TreeManager::applyBoneForces(World& world, double /*deltaTime*/)
{
    WorldData& data = world.getData();
    GridOfCells& grid = world.getGrid();
    constexpr double BONE_FORCE_SCALE = 1.0;
    constexpr double BONE_DAMPING_SCALE = 1.0; // Damping along bone (stretching/compression).
    constexpr double MAX_BONE_FORCE = 0.5;     // Maximum force per bone to prevent yanking.

    // Clear bone force debug info for all organism cells.
    for (auto& [tree_id, tree] : trees_) {
        for (const auto& pos : tree.cells) {
            if (static_cast<uint32_t>(pos.x) < data.width
                && static_cast<uint32_t>(pos.y) < data.height) {
                grid.debugAt(pos.x, pos.y).accumulated_bone_force = {};
            }
        }
    }

    for (auto& [tree_id, tree] : trees_) {
        for (const Bone& bone : tree.bones) {
            if (static_cast<uint32_t>(bone.cell_a.x) >= data.width
                || static_cast<uint32_t>(bone.cell_a.y) >= data.height
                || static_cast<uint32_t>(bone.cell_b.x) >= data.width
                || static_cast<uint32_t>(bone.cell_b.y) >= data.height) {
                continue;
            }

            Cell& cell_a = data.at(bone.cell_a.x, bone.cell_a.y);
            Cell& cell_b = data.at(bone.cell_b.x, bone.cell_b.y);

            // Skip if either cell no longer belongs to this organism.
            if (cell_a.organism_id != tree_id || cell_b.organism_id != tree_id) {
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

void TreeManager::removeCellsFromTree(TreeId tree_id, const std::vector<Vector2i>& positions)
{
    auto tree_it = trees_.find(tree_id);
    if (tree_it == trees_.end()) {
        LOG_WARN(Tree, "TreeManager: Attempted to remove cells from non-existent tree {}", tree_id);
        return;
    }

    Tree& tree = tree_it->second;

    for (const auto& pos : positions) {
        tree.cells.erase(pos);
        cell_to_tree_.erase(pos);
    }

    LOG_DEBUG(
        Tree,
        "TreeManager: Removed {} cells from tree {} (now {} cells tracked)",
        positions.size(),
        tree_id,
        tree.cells.size());
}

void TreeManager::addCellToTree(World& world, TreeId tree_id, Vector2i pos)
{
    auto tree_it = trees_.find(tree_id);
    if (tree_it == trees_.end()) {
        LOG_WARN(Tree, "TreeManager: Attempted to add cell to non-existent tree {}", tree_id);
        return;
    }

    Tree& tree = tree_it->second;
    tree.cells.insert(pos);
    cell_to_tree_[pos] = tree_id;
    world.getData().at(pos.x, pos.y).organism_id = tree_id;

    LOG_DEBUG(
        Tree,
        "TreeManager: Added cell ({},{}) to tree {} (now {} cells tracked)",
        pos.x,
        pos.y,
        tree_id,
        tree.cells.size());
}

} // namespace DirtSim
