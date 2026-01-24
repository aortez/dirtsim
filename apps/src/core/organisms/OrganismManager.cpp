#include "OrganismManager.h"
#include "Duck.h"
#include "DuckBrain.h"
#include "Goose.h"
#include "GooseBrain.h"
#include "Tree.h"
#include "brains/RuleBasedBrain.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/Entity.h"
#include "core/GridOfCells.h"
#include "core/LightBuffer.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldLightCalculator.h"
#include "tests/MultiCellTestOrganism.h"
#include <cassert>
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

OrganismId OrganismManager::at(Vector2i pos) const
{
    if (pos.x < 0 || pos.y < 0 || pos.x >= width_ || pos.y >= height_) {
        return INVALID_ORGANISM_ID;
    }
    return grid_[pos.y * width_ + pos.x];
}

bool OrganismManager::hasOrganism(Vector2i pos) const
{
    return at(pos) != INVALID_ORGANISM_ID;
}

const std::vector<OrganismId>& OrganismManager::getGrid() const
{
    return grid_;
}

void OrganismManager::resizeGrid(int16_t newWidth, int16_t newHeight)
{
    // Early return if no resize needed.
    if (width_ == newWidth && height_ == newHeight) {
        return;
    }

    int16_t oldWidth = width_;
    int16_t oldHeight = height_;

    spdlog::info(
        "OrganismManager::resizeGrid: {}x{} -> {}x{}, repositioning {} organisms",
        oldWidth,
        oldHeight,
        newWidth,
        newHeight,
        organisms_.size());

    // For each organism, scale its continuous position.
    // organism->position was set by World::resizeGrid() to preserve sub-cell precision.
    for (auto& [id, organism] : organisms_) {
        Vector2i oldAnchor = organism->getAnchorCell();

        // Scale the continuous position.
        double scaleX = static_cast<double>(newWidth) / static_cast<double>(oldWidth);
        double scaleY = static_cast<double>(newHeight) / static_cast<double>(oldHeight);

        Vector2d newPosition{ organism->position.x * scaleX, organism->position.y * scaleY };

        // Split continuous position into anchor + COM.
        Vector2i newAnchor{ static_cast<int>(std::floor(newPosition.x)),
                            static_cast<int>(std::floor(newPosition.y)) };

        // Clamp anchor to valid range.
        newAnchor.x = std::max(0, std::min(newAnchor.x, static_cast<int>(newWidth) - 1));
        newAnchor.y = std::max(0, std::min(newAnchor.y, static_cast<int>(newHeight) - 1));

        // Calculate COM from fractional part.
        double fracX = newPosition.x - std::floor(newPosition.x);
        double fracY = newPosition.y - std::floor(newPosition.y);
        Vector2d newCom{ fracX * 2.0 - 1.0, fracY * 2.0 - 1.0 };

        // Update organism position (for both cell-based and rigid body organisms).
        organism->position = newPosition;

        // Calculate offset for all cells.
        Vector2i offset = newAnchor - oldAnchor;

        // Update organism anchor.
        organism->setAnchorCell(newAnchor);

        // Move all cells by offset.
        std::unordered_set<Vector2i> newCells;
        for (const auto& oldPos : organism->getCells()) {
            Vector2i newPos = oldPos + offset;

            // Bounds check - clip cells outside new world.
            if (newPos.x >= 0 && newPos.y >= 0 && newPos.x < newWidth && newPos.y < newHeight) {
                newCells.insert(newPos);
            }
        }

        organism->getCells() = std::move(newCells);

        // Store new COM temporarily for World to write back to grid.
        // TODO: Find cleaner way to pass this back.
        organism->center_of_mass = newCom;

        spdlog::debug(
            "OrganismManager::resizeGrid: Organism {} moved from ({},{}) to ({},{})",
            id,
            oldAnchor.x,
            oldAnchor.y,
            newAnchor.x,
            newAnchor.y);
    }

    // Resize the organism grid.
    width_ = newWidth;
    height_ = newHeight;
    grid_.assign(newWidth * newHeight, INVALID_ORGANISM_ID);

    // Reproject all organisms to the new grid.
    for (auto& [id, organism] : organisms_) {
        for (const auto& pos : organism->getCells()) {
            setOrganismAt(pos, id);
        }
    }
}

void OrganismManager::setOrganismAt(Vector2i pos, OrganismId id)
{
    if (pos.x < 0 || pos.y < 0 || pos.x >= width_ || pos.y >= height_) {
        return;
    }
    grid_[pos.y * width_ + pos.x] = id;
}

void OrganismManager::clearOrganismAt(Vector2i pos)
{
    setOrganismAt(pos, INVALID_ORGANISM_ID);
}

void OrganismManager::update(World& world, double deltaTime)
{
    for (auto& [id, organism] : organisms_) {
        if (organism->isActive() && !organism->usesRigidBodyPhysics()) {
            organism->update(world, deltaTime);
        }
    }
}

void OrganismManager::advanceTime(World& world, double deltaTime)
{
    for (auto& [id, organism] : organisms_) {
        if (organism->isActive() && organism->usesRigidBodyPhysics()) {
            organism->update(world, deltaTime);
        }
    }
}

OrganismId OrganismManager::createTree(
    World& world, uint32_t x, uint32_t y, std::unique_ptr<TreeBrain> brain)
{
    OrganismId id = next_id_++;

    // Use default brain if none provided.
    if (!brain) {
        brain = std::make_unique<RuleBasedBrain>();
    }

    auto tree =
        std::make_unique<Tree>(id, std::move(brain), std::make_unique<TreeCommandProcessor>());

    Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
    tree->setAnchorCell(pos);
    tree->setEnergy(150.0); // Starting energy for tree growth.

    // Place seed material in world.
    world.addMaterialAtCell(
        { static_cast<int16_t>(x), static_cast<int16_t>(y) }, Material::EnumType::Seed, 1.0);

    // Track cell ownership.
    tree->getCells().insert(pos);
    setOrganismAt(pos, id);

    LOG_INFO(Tree, "OrganismManager: Planted tree {} at ({}, {})", id, x, y);

    organisms_.emplace(id, std::move(tree));

    return id;
}

OrganismId OrganismManager::createDuck(
    World& world, uint32_t x, uint32_t y, std::unique_ptr<DuckBrain> brain)
{
    Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };

    // Check if spawn location is already occupied by another organism.
    OrganismId existing = at(pos);
    if (existing != INVALID_ORGANISM_ID) {
        spdlog::warn(
            "OrganismManager::createDuck: Spawn location ({}, {}) already occupied by organism {}",
            x,
            y,
            existing);
        DIRTSIM_ASSERT(false, "createDuck: Spawn location already occupied by another organism");
    }

    OrganismId id = next_id_++;

    // Use default brain if none provided.
    if (!brain) {
        brain = std::make_unique<RandomDuckBrain>();
    }

    auto duck = std::make_unique<Duck>(id, std::move(brain));

    duck->setAnchorCell(pos);

    // Place duck as WOOD cell in world (replace whatever is there).
    world.getData().at(x, y).replaceMaterial(Material::EnumType::Wood, 1.0);

    // Track cell ownership.
    duck->getCells().insert(pos);
    setOrganismAt(pos, id);

    spdlog::info("OrganismManager: Created duck {} at ({}, {})", id, x, y);

    organisms_.emplace(id, std::move(duck));

    return id;
}

OrganismId OrganismManager::createGoose(
    World& world, uint32_t x, uint32_t y, std::unique_ptr<GooseBrain> brain)
{
    OrganismId id = next_id_++;

    // Use default brain if none provided.
    if (!brain) {
        brain = std::make_unique<RandomGooseBrain>();
    }

    auto goose = std::make_unique<Goose>(id, std::move(brain));

    // Set initial position (continuous, centered in cell).
    goose->setAnchorCell(Vector2i{ static_cast<int>(x), static_cast<int>(y) });

    // Register organism BEFORE initial update so addCellToOrganism can find it.
    Goose* goose_ptr = goose.get();
    organisms_.emplace(id, std::move(goose));

    // Do initial projection to grid via update with zero deltaTime.
    goose_ptr->update(world, 0.0);

    spdlog::info("OrganismManager: Created goose {} at ({}, {})", id, x, y);

    return id;
}

OrganismId OrganismManager::createMultiCellTestOrganism(
    World& world, uint32_t x, uint32_t y, MultiCellShape shape)
{
    OrganismId id = next_id_++;

    auto organism = std::make_unique<MultiCellTestOrganism>(id, shape);
    organism->setAnchorCell(Vector2i{ static_cast<int>(x), static_cast<int>(y) });

    // Register organism BEFORE initial update.
    MultiCellTestOrganism* ptr = organism.get();
    organisms_.emplace(id, std::move(organism));

    // Do initial projection to grid.
    ptr->update(world, 0.0);

    spdlog::info("OrganismManager: Created test organism {} at ({}, {})", id, x, y);

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
        if (data.inBounds(pos.x, pos.y)) {
            data.at(pos.x, pos.y) = Cell();
        }
    }

    spdlog::info(
        "OrganismManager: Removed organism {} from world ({} cells cleared)",
        id,
        organism->getCells().size());

    // Now do the internal cleanup.
    removeOrganism(id);
}

void OrganismManager::removeOrganism(OrganismId id)
{
    auto it = organisms_.find(id);
    assert(it != organisms_.end() && "removeOrganism called with non-existent organism ID");

    for (const auto& pos : it->second->getCells()) {
        clearOrganismAt(pos);
    }

    organisms_.erase(it);
    organismGenomeIds_.erase(id);
}

void OrganismManager::clear()
{
    spdlog::info("OrganismManager: Clearing all organisms (count={})", organisms_.size());
    organisms_.clear();
    organismGenomeIds_.clear();
    std::fill(grid_.begin(), grid_.end(), INVALID_ORGANISM_ID);
}

Organism::Body* OrganismManager::getOrganism(OrganismId id)
{
    auto it = organisms_.find(id);
    return it != organisms_.end() ? it->second.get() : nullptr;
}

const Organism::Body* OrganismManager::getOrganism(OrganismId id) const
{
    auto it = organisms_.find(id);
    return it != organisms_.end() ? it->second.get() : nullptr;
}

void OrganismManager::setGenomeId(OrganismId id, const GenomeId& genomeId)
{
    organismGenomeIds_[id] = genomeId;
}

std::optional<GenomeId> OrganismManager::getGenomeId(OrganismId id) const
{
    auto it = organismGenomeIds_.find(id);
    if (it == organismGenomeIds_.end()) {
        return std::nullopt;
    }
    return it->second;
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

Goose* OrganismManager::getGoose(OrganismId id)
{
    auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::GOOSE) {
        return static_cast<Goose*>(organism);
    }
    return nullptr;
}

const Goose* OrganismManager::getGoose(OrganismId id) const
{
    const auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::GOOSE) {
        return static_cast<const Goose*>(organism);
    }
    return nullptr;
}

MultiCellTestOrganism* OrganismManager::getMultiCellTestOrganism(OrganismId id)
{
    auto* organism = getOrganism(id);
    // MultiCellTestOrganism uses TREE type for now.
    if (organism && organism->getType() == OrganismType::TREE) {
        return dynamic_cast<MultiCellTestOrganism*>(organism);
    }
    return nullptr;
}

const MultiCellTestOrganism* OrganismManager::getMultiCellTestOrganism(OrganismId id) const
{
    const auto* organism = getOrganism(id);
    if (organism && organism->getType() == OrganismType::TREE) {
        return dynamic_cast<const MultiCellTestOrganism*>(organism);
    }
    return nullptr;
}

void OrganismManager::addCellToOrganism(OrganismId id, Vector2i pos)
{
    auto* organism = getOrganism(id);
    DIRTSIM_ASSERT(
        organism != nullptr,
        "addCellToOrganism called with non-existent organism - register organism first!");
    if (!organism) {
        spdlog::warn("OrganismManager: Attempted to add cell to non-existent organism {}", id);
        return;
    }

    organism->getCells().insert(pos);
    setOrganismAt(pos, id);

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
        clearOrganismAt(pos);
    }

    spdlog::debug(
        "OrganismManager: Removed {} cells from organism {} (now {} cells tracked)",
        positions.size(),
        id,
        organism->getCells().size());
}

void OrganismManager::swapOrganisms(Vector2i pos1, Vector2i pos2)
{
    OrganismId org1 = at(pos1);
    OrganismId org2 = at(pos2);

    // Detect stale tracking bug.
    if (org1 == org2 && org1 != INVALID_ORGANISM_ID) {
        spdlog::critical(
            "swapOrganisms: INVARIANT VIOLATION - Same organism {} at both positions!", org1);
        spdlog::critical("  pos1=({},{}), pos2=({},{})", pos1.x, pos1.y, pos2.x, pos2.y);

        auto* organism = getOrganism(org1);
        if (organism) {
            spdlog::critical(
                "  Organism type={}, anchor=({},{}), cells.size()={}",
                static_cast<int>(organism->getType()),
                organism->getAnchorCell().x,
                organism->getAnchorCell().y,
                organism->getCells().size());
        }

        DIRTSIM_ASSERT(false, "swapOrganisms: Same organism cannot be at both swap positions");
    }

    // Swap grid entries.
    setOrganismAt(pos1, org2);
    setOrganismAt(pos2, org1);

    // Update organism cell sets.
    if (org1 != INVALID_ORGANISM_ID) {
        auto* organism = getOrganism(org1);
        DIRTSIM_ASSERT(organism != nullptr, "Organism in grid must exist in organisms_ map");
        organism->getCells().erase(pos1);
        organism->getCells().insert(pos2);
        organism->onCellTransfer(pos1, pos2);
    }

    if (org2 != INVALID_ORGANISM_ID) {
        auto* organism = getOrganism(org2);
        DIRTSIM_ASSERT(organism != nullptr, "Organism in grid must exist in organisms_ map");
        organism->getCells().erase(pos2);
        organism->getCells().insert(pos1);
        organism->onCellTransfer(pos2, pos1);
    }
}

void OrganismManager::moveOrganismCell(Vector2i from, Vector2i to, OrganismId organism_id)
{
    auto* organism = getOrganism(organism_id);
    if (!organism) {
        spdlog::warn("moveOrganismCell: organism {} not found", organism_id);
        return;
    }

    // Skip rigid body organisms - they control their own position.
    if (organism->usesRigidBodyPhysics()) {
        spdlog::warn("moveOrganismCell: skipping rigid body organism {}", organism_id);
        return;
    }

    // Poka-yoke: Verify the source cell actually has this organism.
    OrganismId current_at_from = at(from);
    if (current_at_from != organism_id) {
        spdlog::critical(
            "moveOrganismCell: INVARIANT VIOLATION - Expected organism {} at ({},{}) but found {}",
            organism_id,
            from.x,
            from.y,
            current_at_from);
        DIRTSIM_ASSERT(false, "moveOrganismCell: Source cell doesn't have expected organism");
    }

    // Update grid.
    clearOrganismAt(from);
    setOrganismAt(to, organism_id);

    // Update organism's cell tracking.
    organism->getCells().erase(from);
    organism->getCells().insert(to);
    organism->onCellTransfer(from, to);
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
            if (data.inBounds(pos.x, pos.y)) {
                grid.debugAt(pos.x, pos.y).accumulated_bone_force = {};
            }
        }
    }

    for (auto& [organism_id, organism] : organisms_) {
        for (const Bone& bone : organism->getBones()) {
            if (!data.inBounds(bone.cell_a.x, bone.cell_a.y)
                || !data.inBounds(bone.cell_b.x, bone.cell_b.y)) {
                continue;
            }

            Cell& cell_a = data.at(bone.cell_a.x, bone.cell_a.y);
            Cell& cell_b = data.at(bone.cell_b.x, bone.cell_b.y);

            // Skip if either cell no longer belongs to this organism.
            if (at(bone.cell_a) != organism_id || at(bone.cell_b) != organism_id) {
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

void OrganismManager::syncEntitiesToWorldData(World& world)
{
    WorldData& data = world.getData();
    data.entities.clear();

    const LightBuffer& light = world.getRawLightBuffer();

    for (const auto& [id, organism] : organisms_) {
        if (!organism->isActive()) {
            continue;
        }

        if (organism->getType() == OrganismType::DUCK) {
            const Duck* duck = static_cast<const Duck*>(organism.get());
            Vector2i anchor = duck->getAnchorCell();

            Entity entity;
            entity.id = id.get();
            entity.type = EntityType::Duck;
            entity.visible = true;

            entity.position =
                Vector2<float>{ static_cast<float>(anchor.x), static_cast<float>(anchor.y) };

            if (data.inBounds(anchor.x, anchor.y)) {
                const Cell& cell = data.at(anchor.x, anchor.y);
                entity.com = Vector2<float>{ static_cast<float>(cell.com.x),
                                             static_cast<float>(cell.com.y) };
                entity.velocity = Vector2<float>{ static_cast<float>(cell.velocity.x),
                                                  static_cast<float>(cell.velocity.y) };
                entity.light_color = light.at(anchor.x, anchor.y);
            }

            entity.facing = duck->getFacing();
            entity.mass = 1.0f;

            // Copy sparkles and set emission from sparkle ratio.
            const auto& duck_sparkles = duck->getSparkles();
            entity.sparkles.reserve(duck_sparkles.size());
            for (const auto& ds : duck_sparkles) {
                SparkleParticle sp;
                sp.position = ds.position;
                sp.opacity = ds.lifetime / ds.max_lifetime;
                entity.sparkles.push_back(sp);
            }
            entity.emission = 0.7 + duck->getSparkleRatio();

            data.entities.push_back(std::move(entity));
        }
        else if (organism->getType() == OrganismType::GOOSE) {
            const Goose* goose = static_cast<const Goose*>(organism.get());
            Vector2i anchor = goose->getAnchorCell();

            Entity entity;
            entity.id = id.get();
            entity.type = EntityType::Goose;
            entity.visible = true;

            entity.position =
                Vector2<float>{ static_cast<float>(anchor.x), static_cast<float>(anchor.y) };

            // Get COM offset from the continuous position.
            double frac_x = goose->position.x - std::floor(goose->position.x);
            double frac_y = goose->position.y - std::floor(goose->position.y);
            entity.com = Vector2<float>{ static_cast<float>(frac_x * 2.0 - 1.0),
                                         static_cast<float>(frac_y * 2.0 - 1.0) };

            entity.velocity = Vector2<float>{ static_cast<float>(goose->velocity.x),
                                              static_cast<float>(goose->velocity.y) };

            entity.facing = goose->getFacing();
            entity.mass = static_cast<float>(goose->mass);

            if (data.inBounds(anchor.x, anchor.y)) {
                entity.light_color = light.at(anchor.x, anchor.y);
            }

            data.entities.push_back(std::move(entity));
        }
        // Trees render as cells (SEED, WOOD, LEAF, ROOT), not entities.
    }
}

void OrganismManager::injectEmissions(WorldLightCalculator& light_calc)
{
    constexpr uint32_t DUCK_GLOW_COLOR = 0xFFCC66FF;
    constexpr float MAX_EMISSION_INTENSITY = 0.8f;

    for (const auto& [id, organism] : organisms_) {
        if (organism->getType() == OrganismType::DUCK) {
            const Duck* duck = static_cast<const Duck*>(organism.get());
            float sparkle_ratio = duck->getSparkleRatio();
            if (sparkle_ratio > 0.0f) {
                Vector2i pos = duck->getAnchorCell();
                float intensity = sparkle_ratio * MAX_EMISSION_INTENSITY;
                light_calc.setEmissive(pos.x, pos.y, DUCK_GLOW_COLOR, intensity);
            }
        }
    }
}

} // namespace DirtSim
