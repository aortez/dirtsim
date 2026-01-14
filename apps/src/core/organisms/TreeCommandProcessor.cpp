#include "TreeCommandProcessor.h"
#include "OrganismManager.h"
#include "OrganismType.h"
#include "Tree.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <spdlog/spdlog.h>

namespace DirtSim {

// Energy costs for tree growth commands.
constexpr double ENERGY_COST_WOOD = 10.0;
constexpr double ENERGY_COST_LEAF = 8.0;
constexpr double ENERGY_COST_ROOT = 12.0;
constexpr double ENERGY_COST_REINFORCE = 5.0;
constexpr double ENERGY_COST_PRODUCE_SEED = 50.0;

CommandExecutionResult TreeCommandProcessor::execute(
    Tree& tree, World& world, const TreeCommand& cmd)
{
    return std::visit(
        [&](auto&& command) -> CommandExecutionResult {
            using T = std::decay_t<decltype(command)>;

            if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                if (tree.getEnergy() < ENERGY_COST_WOOD) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for WOOD growth" };
                }

                if (!world.getData().inBounds(command.target_pos.x, command.target_pos.y)) {
                    return { CommandResult::INVALID_TARGET, "WOOD target out of bounds" };
                }

                // Check cardinal adjacency to WOOD or SEED (structural elements only).
                Vector2i cardinal_dirs[] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };
                bool has_structural_neighbor = false;
                for (const auto& dir : cardinal_dirs) {
                    Vector2i neighbor_pos = command.target_pos + dir;
                    if (world.getData().inBounds(neighbor_pos.x, neighbor_pos.y)) {
                        if (world.getOrganismManager().at(neighbor_pos) == tree.getId()) {
                            const Cell& neighbor =
                                world.getData().at(neighbor_pos.x, neighbor_pos.y);
                            if (neighbor.material_type == Material::EnumType::Wood
                                || neighbor.material_type == Material::EnumType::Seed) {
                                has_structural_neighbor = true;
                                break;
                            }
                        }
                    }
                }

                if (!has_structural_neighbor) {
                    return { CommandResult::INVALID_TARGET,
                             "WOOD requires cardinal adjacency to WOOD or SEED" };
                }

                // Convert world position to local coordinates.
                Vector2i anchor = tree.getAnchorCell();
                Vector2i localPos = command.target_pos - anchor;

                // Add cell to tree's local shape (rigid body will project it to grid).
                tree.addCellToLocalShape(localPos, Material::EnumType::Wood, 1.0);
                tree.setEnergy(tree.getEnergy() - ENERGY_COST_WOOD);

                spdlog::info(
                    "Tree {}: Grew WOOD at ({}, {})",
                    tree.getId(),
                    command.target_pos.x,
                    command.target_pos.y);

                if (tree.getStage() == GrowthStage::GERMINATION) {
                    tree.setStage(GrowthStage::SAPLING);
                    spdlog::info("Tree {}: Transitioned to SAPLING stage", tree.getId());
                }

                return { CommandResult::SUCCESS, "WOOD growth successful" };
            }
            else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                if (tree.getEnergy() < ENERGY_COST_LEAF) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for LEAF growth" };
                }

                if (!world.getData().inBounds(command.target_pos.x, command.target_pos.y)) {
                    return { CommandResult::INVALID_TARGET, "LEAF target out of bounds" };
                }

                // Check cardinal adjacency to WOOD (leaves grow from branches).
                Vector2i cardinal_dirs[] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };
                bool has_wood_neighbor = false;
                for (const auto& dir : cardinal_dirs) {
                    Vector2i neighbor_pos = command.target_pos + dir;
                    if (world.getData().inBounds(neighbor_pos.x, neighbor_pos.y)) {
                        if (world.getOrganismManager().at(neighbor_pos) == tree.getId()) {
                            const Cell& neighbor =
                                world.getData().at(neighbor_pos.x, neighbor_pos.y);
                            if (neighbor.material_type == Material::EnumType::Wood) {
                                has_wood_neighbor = true;
                                break;
                            }
                        }
                    }
                }

                if (!has_wood_neighbor) {
                    return { CommandResult::INVALID_TARGET,
                             "LEAF requires cardinal adjacency to WOOD" };
                }

                // Convert world position to local coordinates.
                Vector2i anchor = tree.getAnchorCell();
                Vector2i localPos = command.target_pos - anchor;

                // Add cell to tree's local shape (rigid body will project it to grid).
                tree.addCellToLocalShape(localPos, Material::EnumType::Leaf, 1.0);
                tree.setEnergy(tree.getEnergy() - ENERGY_COST_LEAF);

                spdlog::info(
                    "Tree {}: Grew LEAF at ({}, {})",
                    tree.getId(),
                    command.target_pos.x,
                    command.target_pos.y);

                return { CommandResult::SUCCESS, "LEAF growth successful" };
            }
            else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                if (tree.getEnergy() < ENERGY_COST_ROOT) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for ROOT growth" };
                }

                if (!world.getData().inBounds(command.target_pos.x, command.target_pos.y)) {
                    return { CommandResult::INVALID_TARGET, "ROOT target out of bounds" };
                }

                // Check cardinal adjacency to SEED or ROOT (root network).
                Vector2i cardinal_dirs[] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };
                bool has_root_neighbor = false;
                for (const auto& dir : cardinal_dirs) {
                    Vector2i neighbor_pos = command.target_pos + dir;
                    if (world.getData().inBounds(neighbor_pos.x, neighbor_pos.y)) {
                        if (world.getOrganismManager().at(neighbor_pos) == tree.getId()) {
                            const Cell& neighbor =
                                world.getData().at(neighbor_pos.x, neighbor_pos.y);
                            if (neighbor.material_type == Material::EnumType::Root
                                || neighbor.material_type == Material::EnumType::Seed) {
                                has_root_neighbor = true;
                                break;
                            }
                        }
                    }
                }

                if (!has_root_neighbor) {
                    return { CommandResult::INVALID_TARGET,
                             "ROOT requires cardinal adjacency to SEED or ROOT" };
                }

                // Convert world position to local coordinates.
                Vector2i anchor = tree.getAnchorCell();
                Vector2i localPos = command.target_pos - anchor;

                // Add cell to tree's local shape (rigid body will project it to grid).
                tree.addCellToLocalShape(localPos, Material::EnumType::Root, 1.0);
                tree.setEnergy(tree.getEnergy() - ENERGY_COST_ROOT);

                spdlog::info(
                    "Tree {}: Grew ROOT at ({}, {})",
                    tree.getId(),
                    command.target_pos.x,
                    command.target_pos.y);

                if (tree.getStage() == GrowthStage::SEED) {
                    tree.setStage(GrowthStage::GERMINATION);
                    spdlog::info("Tree {}: Transitioned to GERMINATION stage", tree.getId());
                }

                return { CommandResult::SUCCESS, "ROOT growth successful" };
            }
            else if constexpr (std::is_same_v<T, ReinforceCellCommand>) {
                if (tree.getEnergy() < ENERGY_COST_REINFORCE) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for cell reinforcement" };
                }

                tree.setEnergy(tree.getEnergy() - ENERGY_COST_REINFORCE);

                spdlog::info(
                    "Tree {}: Reinforced cell at ({}, {}) [not yet implemented]",
                    tree.getId(),
                    command.position.x,
                    command.position.y);

                return { CommandResult::SUCCESS, "Cell reinforcement successful" };
            }
            else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                if (tree.getEnergy() < ENERGY_COST_PRODUCE_SEED) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for seed production" };
                }

                if (!world.getData().inBounds(command.position.x, command.position.y)) {
                    return { CommandResult::INVALID_TARGET, "Seed position out of bounds" };
                }

                // Check cardinal adjacency to WOOD or LEAF (seeds grow from branches).
                Vector2i cardinal_dirs[] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };
                bool has_branch_neighbor = false;
                for (const auto& dir : cardinal_dirs) {
                    Vector2i neighbor_pos = command.position + dir;
                    if (world.getData().inBounds(neighbor_pos.x, neighbor_pos.y)) {
                        if (world.getOrganismManager().at(neighbor_pos) == tree.getId()) {
                            const Cell& neighbor =
                                world.getData().at(neighbor_pos.x, neighbor_pos.y);
                            if (neighbor.material_type == Material::EnumType::Wood
                                || neighbor.material_type == Material::EnumType::Leaf) {
                                has_branch_neighbor = true;
                                break;
                            }
                        }
                    }
                }

                if (!has_branch_neighbor) {
                    return { CommandResult::INVALID_TARGET,
                             "SEED requires cardinal adjacency to WOOD or LEAF" };
                }

                // Check target cell is growable (must be AIR - seeds need to fall).
                const Cell& target_cell =
                    world.getData().at(command.position.x, command.position.y);
                if (target_cell.material_type != Material::EnumType::Air) {
                    return { CommandResult::BLOCKED, "SEED can only be placed in AIR cells" };
                }

                // Check target cell isn't owned by another organism.
                OrganismId target_owner = world.getOrganismManager().at(command.position);
                if (target_owner != INVALID_ORGANISM_ID && target_owner != tree.getId()) {
                    return { CommandResult::BLOCKED,
                             "Cannot place SEED in another organism's cell" };
                }

                world.getData()
                    .at(command.position.x, command.position.y)
                    .replaceMaterial(Material::EnumType::Seed, 1.0);

                tree.setEnergy(tree.getEnergy() - ENERGY_COST_PRODUCE_SEED);

                spdlog::info(
                    "Tree {}: Produced SEED at ({}, {})",
                    tree.getId(),
                    command.position.x,
                    command.position.y);

                return { CommandResult::SUCCESS, "Seed production successful" };
            }
            else if constexpr (std::is_same_v<T, WaitCommand>) {
                // WaitCommand is instant - no action taken.
                return { CommandResult::SUCCESS, "Wait" };
            }
            else if constexpr (std::is_same_v<T, CancelCommand>) {
                // CancelCommand should be handled by Tree::processBrainDecision, not here.
                return { CommandResult::SUCCESS, "Cancel" };
            }

            return { CommandResult::INVALID_TARGET, "Unknown command type" };
        },
        cmd);
}

} // namespace DirtSim
