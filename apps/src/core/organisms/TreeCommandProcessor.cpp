#include "TreeCommandProcessor.h"
#include "OrganismManager.h"
#include "OrganismType.h"
#include "Tree.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <array>
#include <initializer_list>

namespace DirtSim {

namespace {
// Energy costs for tree growth commands.
constexpr double ENERGY_COST_WOOD = 10.0;
constexpr double ENERGY_COST_LEAF = 8.0;
constexpr double ENERGY_COST_ROOT = 12.0;
constexpr double ENERGY_COST_PRODUCE_SEED = 50.0;
constexpr std::array<Vector2i, 4> kCardinalDirections{
    Vector2i{ 0, 1 },
    Vector2i{ 0, -1 },
    Vector2i{ -1, 0 },
    Vector2i{ 1, 0 },
};

double getEnergyCostForCommand(const TreeCommand& cmd)
{
    return std::visit(
        [](const auto& command) -> double {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                return ENERGY_COST_WOOD;
            }
            else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                return ENERGY_COST_LEAF;
            }
            else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                return ENERGY_COST_ROOT;
            }
            else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                return ENERGY_COST_PRODUCE_SEED;
            }
            return 0.0;
        },
        cmd);
}

bool isTargetOwnedByTree(const Tree& tree, const World& world, Vector2i pos)
{
    return world.getOrganismManager().at(pos) == tree.getId();
}

bool hasOwnedNeighborWithMaterial(
    const Tree& tree,
    const World& world,
    Vector2i pos,
    std::initializer_list<Material::EnumType> allowedMaterials)
{
    for (const auto& dir : kCardinalDirections) {
        const Vector2i neighborPos = pos + dir;
        if (!world.getData().inBounds(neighborPos.x, neighborPos.y)) {
            continue;
        }
        if (world.getOrganismManager().at(neighborPos) != tree.getId()) {
            continue;
        }

        const Cell& neighbor = world.getData().at(neighborPos.x, neighborPos.y);
        for (const auto material : allowedMaterials) {
            if (neighbor.material_type == material) {
                return true;
            }
        }
    }

    return false;
}
} // namespace

CommandExecutionResult treeCommandValidate(
    const Tree& tree, const World& world, const TreeCommand& cmd, bool checkEnergy)
{
    return std::visit(
        [&](auto&& command) -> CommandExecutionResult {
            using T = std::decay_t<decltype(command)>;

            if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                if (checkEnergy && tree.getEnergy() < ENERGY_COST_WOOD) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for WOOD growth" };
                }

                if (!world.getData().inBounds(command.target_pos.x, command.target_pos.y)) {
                    return { CommandResult::INVALID_TARGET, "WOOD target out of bounds" };
                }

                if (isTargetOwnedByTree(tree, world, command.target_pos)) {
                    return { CommandResult::INVALID_TARGET, "WOOD target already owned by tree" };
                }

                if (hasOwnedNeighborWithMaterial(
                        tree,
                        world,
                        command.target_pos,
                        { Material::EnumType::Seed, Material::EnumType::Wood })) {
                    return { CommandResult::SUCCESS, "WOOD target valid" };
                }

                return { CommandResult::INVALID_TARGET,
                         "WOOD requires cardinal adjacency to WOOD or SEED" };
            }
            else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                if (checkEnergy && tree.getEnergy() < ENERGY_COST_LEAF) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for LEAF growth" };
                }

                if (!world.getData().inBounds(command.target_pos.x, command.target_pos.y)) {
                    return { CommandResult::INVALID_TARGET, "LEAF target out of bounds" };
                }

                if (isTargetOwnedByTree(tree, world, command.target_pos)) {
                    return { CommandResult::INVALID_TARGET, "LEAF target already owned by tree" };
                }

                if (hasOwnedNeighborWithMaterial(
                        tree, world, command.target_pos, { Material::EnumType::Wood })) {
                    return { CommandResult::SUCCESS, "LEAF target valid" };
                }

                return { CommandResult::INVALID_TARGET,
                         "LEAF requires cardinal adjacency to WOOD" };
            }
            else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                if (checkEnergy && tree.getEnergy() < ENERGY_COST_ROOT) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for ROOT growth" };
                }

                if (!world.getData().inBounds(command.target_pos.x, command.target_pos.y)) {
                    return { CommandResult::INVALID_TARGET, "ROOT target out of bounds" };
                }

                if (isTargetOwnedByTree(tree, world, command.target_pos)) {
                    return { CommandResult::INVALID_TARGET, "ROOT target already owned by tree" };
                }

                if (hasOwnedNeighborWithMaterial(
                        tree,
                        world,
                        command.target_pos,
                        { Material::EnumType::Root, Material::EnumType::Seed })) {
                    return { CommandResult::SUCCESS, "ROOT target valid" };
                }

                return { CommandResult::INVALID_TARGET,
                         "ROOT requires cardinal adjacency to SEED or ROOT" };
            }
            else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                if (checkEnergy && tree.getEnergy() < ENERGY_COST_PRODUCE_SEED) {
                    return { CommandResult::INSUFFICIENT_ENERGY,
                             "Not enough energy for seed production" };
                }

                if (!world.getData().inBounds(command.position.x, command.position.y)) {
                    return { CommandResult::INVALID_TARGET, "Seed position out of bounds" };
                }

                if (!hasOwnedNeighborWithMaterial(
                        tree,
                        world,
                        command.position,
                        { Material::EnumType::Leaf, Material::EnumType::Wood })) {
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

                return { CommandResult::SUCCESS, "SEED target valid" };
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

CommandExecutionResult TreeCommandProcessor::validate(
    Tree& tree, World& world, const TreeCommand& cmd)
{
    return treeCommandValidate(tree, world, cmd, true);
}

CommandExecutionResult TreeCommandProcessor::execute(
    Tree& tree, World& world, const TreeCommand& cmd)
{
    const CommandExecutionResult validation = treeCommandValidate(tree, world, cmd, false);
    if (!validation.succeeded()) {
        return validation;
    }

    const double energyCost = getEnergyCostForCommand(cmd);
    if (energyCost > 0.0 && !tree.isEnergyReservedForCommand(cmd, energyCost)) {
        LOG_WARN(Tree, "Tree {}: Energy not reserved for command", tree.getId());
        return { CommandResult::INSUFFICIENT_ENERGY, "Energy not reserved for command" };
    }

    return std::visit(
        [&](auto&& command) -> CommandExecutionResult {
            using T = std::decay_t<decltype(command)>;

            if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                // Convert world position to local coordinates.
                Vector2i anchor = tree.getAnchorCell();
                Vector2i localPos = command.target_pos - anchor;

                // Add cell to tree's local shape (rigid body will project it to grid).
                tree.addCellToLocalShape(localPos, Material::EnumType::Wood, 1.0);

                LOG_INFO(
                    Tree,
                    "Tree {}: Grew WOOD at ({}, {})",
                    tree.getId(),
                    command.target_pos.x,
                    command.target_pos.y);

                return { CommandResult::SUCCESS, "WOOD growth successful" };
            }
            else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                // Convert world position to local coordinates.
                Vector2i anchor = tree.getAnchorCell();
                Vector2i localPos = command.target_pos - anchor;

                // Add cell to tree's local shape (rigid body will project it to grid).
                tree.addCellToLocalShape(localPos, Material::EnumType::Leaf, 1.0);

                LOG_INFO(
                    Tree,
                    "Tree {}: Grew LEAF at ({}, {})",
                    tree.getId(),
                    command.target_pos.x,
                    command.target_pos.y);

                return { CommandResult::SUCCESS, "LEAF growth successful" };
            }
            else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                // Convert world position to local coordinates.
                Vector2i anchor = tree.getAnchorCell();
                Vector2i localPos = command.target_pos - anchor;

                // Add cell to tree's local shape (rigid body will project it to grid).
                tree.addCellToLocalShape(localPos, Material::EnumType::Root, 1.0);

                LOG_INFO(
                    Tree,
                    "Tree {}: Grew ROOT at ({}, {})",
                    tree.getId(),
                    command.target_pos.x,
                    command.target_pos.y);

                return { CommandResult::SUCCESS, "ROOT growth successful" };
            }
            else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                TreeSpawnParams seedParams{ .startingEnergy = 0.0, .passive = true };
                world.getOrganismManager().createTree(
                    world,
                    static_cast<uint32_t>(command.position.x),
                    static_cast<uint32_t>(command.position.y),
                    nullptr,
                    seedParams);
                tree.incrementSeedsProduced();

                LOG_INFO(
                    Tree,
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

double TreeCommandProcessor::getEnergyCost(const TreeCommand& cmd) const
{
    return getEnergyCostForCommand(cmd);
}

} // namespace DirtSim
