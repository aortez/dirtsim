#include "World.h"
#include "core/LoggingChannels.h"

#include "Assert.h"
#include "Cell.h"
#include "GridOfCells.h"
#include "LightManager.h"
#include "PhysicsSettings.h"
#include "ReflectSerializer.h"
#include "ScopeTimer.h"
#include "Timers.h"
#include "Vector2i.h"
#include "WorldAdhesionCalculator.h"
#include "WorldAirResistanceCalculator.h"
#include "WorldCohesionCalculator.h"
#include "WorldCollisionCalculator.h"
#include "WorldData.h"
#include "WorldDiagramGeneratorEmoji.h"
#include "WorldFrictionCalculator.h"
#include "WorldInterpolationTool.h"
#include "WorldLightCalculator.h"
#include "WorldPressureCalculator.h"
#include "WorldRigidBodyCalculator.h"
#include "WorldVelocityLimitCalculator.h"
#include "WorldViscosityCalculator.h"
#include "organisms/OrganismManager.h"
#include "scenarios/Scenario.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {

struct Vector2iHash {
    size_t operator()(const DirtSim::Vector2i& v) const
    {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 16);
    }
};

} // namespace

namespace DirtSim {

// =================================================================
// PIMPL IMPLEMENTATION STRUCT
// =================================================================

struct World::Impl {
    // World state data (previously public).
    WorldData data_;

    // Physics settings (previously public).
    PhysicsSettings physicsSettings_;

    // Persistent grid cache (initialized after data_ in constructor).
    std::optional<GridOfCells> grid_;

    // Calculators. WorldFrictionCalculator is constructed locally with GridOfCells reference.
    WorldAdhesionCalculator adhesion_calculator_;
    WorldCollisionCalculator collision_calculator_;
    WorldLightCalculator light_calculator_;
    WorldPressureCalculator pressure_calculator_;
    WorldViscosityCalculator viscosity_calculator_;

    // Material transfer queue (internal simulation state).
    std::vector<MaterialMove> pending_moves_;

    // Light sources.
    LightManager light_manager_;

    // Performance timing.
    mutable Timers timers_;

    // Constructor.
    Impl() : physicsSettings_(getDefaultPhysicsSettings())
    {
        timers_.startTimer("total_simulation");
    }

    // Destructor.
    ~Impl() { timers_.stopTimer("total_simulation"); }
};

World::World() : World(1, 1)
{}

World::World(int width, int height)
    : cohesion_bind_force_enabled_(false),
      cohesion_bind_force_strength_(1.0),
      com_cohesion_range_(1),
      air_resistance_enabled_(true),
      air_resistance_strength_(0.1),
      selected_material_(Material::EnumType::Dirt),
      pImpl(),
      organism_manager_(std::make_unique<OrganismManager>()),
      rng_(std::make_unique<std::mt19937>(std::random_device{}()))
{
    // Set dimensions (other WorldData members use defaults from struct declaration).
    pImpl->data_.width = static_cast<int16_t>(width);
    pImpl->data_.height = static_cast<int16_t>(height);

    spdlog::info(
        "Creating World: {}x{} grid with pure-material physics",
        pImpl->data_.width,
        pImpl->data_.height);

    // Initialize cell grid.
    pImpl->data_.cells.resize(pImpl->data_.width * pImpl->data_.height);
    pImpl->data_.debug_info.resize(pImpl->data_.width * pImpl->data_.height);

    // Initialize organism manager grid.
    organism_manager_->resizeGrid(width, height);

    // Initialize light calculator emissive overlay.
    pImpl->light_calculator_.resize(width, height);

    // Initialize with empty air.
    for (auto& cell : pImpl->data_.cells) {
        cell = Cell{ Material::EnumType::Air, 0.0 };
    }

    // Note: Boundary walls are now set up by Scenarios in their setup() method.
    // Each scenario controls whether it wants walls or not.

    // Initialize persistent GridOfCells for debug info and caching.
    pImpl->grid_.emplace(
        pImpl->data_.cells, pImpl->data_.debug_info, pImpl->data_.width, pImpl->data_.height);

    SLOG_INFO("World initialization complete");
}

World::~World()
{
    SLOG_INFO("Destroying World: {}x{} grid", pImpl->data_.width, pImpl->data_.height);
}

// =================================================================
// CALCULATOR ACCESSORS
// =================================================================

WorldPressureCalculator& World::getPressureCalculator()
{
    return pImpl->pressure_calculator_;
}

const WorldPressureCalculator& World::getPressureCalculator() const
{
    return pImpl->pressure_calculator_;
}

WorldCollisionCalculator& World::getCollisionCalculator()
{
    return pImpl->collision_calculator_;
}

const WorldCollisionCalculator& World::getCollisionCalculator() const
{
    return pImpl->collision_calculator_;
}

WorldAdhesionCalculator& World::getAdhesionCalculator()
{
    return pImpl->adhesion_calculator_;
}

const WorldAdhesionCalculator& World::getAdhesionCalculator() const
{
    return pImpl->adhesion_calculator_;
}

WorldViscosityCalculator& World::getViscosityCalculator()
{
    return pImpl->viscosity_calculator_;
}

const WorldViscosityCalculator& World::getViscosityCalculator() const
{
    return pImpl->viscosity_calculator_;
}

WorldLightCalculator& World::getLightCalculator()
{
    return pImpl->light_calculator_;
}

const WorldLightCalculator& World::getLightCalculator() const
{
    return pImpl->light_calculator_;
}

const LightBuffer& World::getRawLightBuffer() const
{
    return pImpl->light_calculator_.getRawLightBuffer();
}

LightManager& World::getLightManager()
{
    return pImpl->light_manager_;
}

const LightManager& World::getLightManager() const
{
    return pImpl->light_manager_;
}

Timers& World::getTimers()
{
    return pImpl->timers_;
}

const Timers& World::getTimers() const
{
    return pImpl->timers_;
}

void World::dumpTimerStats() const
{
    pImpl->timers_.dumpTimerStats();
}

WorldData& World::getData()
{
    return pImpl->data_;
}

const WorldData& World::getData() const
{
    return pImpl->data_;
}

GridOfCells& World::getGrid()
{
    return *pImpl->grid_;
}

const GridOfCells& World::getGrid() const
{
    return *pImpl->grid_;
}

PhysicsSettings& World::getPhysicsSettings()
{
    return pImpl->physicsSettings_;
}

const PhysicsSettings& World::getPhysicsSettings() const
{
    return pImpl->physicsSettings_;
}

// =================================================================
// SIMPLE GETTERS/SETTERS (moved from inline in header)
// =================================================================

void World::setSelectedMaterial(Material::EnumType type)
{
    selected_material_ = type;
}

Material::EnumType World::getSelectedMaterial() const
{
    return selected_material_;
}

void World::setDirtFragmentationFactor(double /* factor */)
{
    // No-op for World.
}

// =================================================================
// TIME REVERSAL STUBS (no-op implementations)
// =================================================================

void World::enableTimeReversal(bool /* enabled */)
{}
bool World::isTimeReversalEnabled() const
{
    return false;
}
void World::saveWorldState()
{}
bool World::canGoBackward() const
{
    return false;
}
bool World::canGoForward() const
{
    return false;
}
void World::goBackward()
{}
void World::goForward()
{}
void World::clearHistory()
{}
size_t World::getHistorySize() const
{
    return 0;
}

// =================================================================
// COHESION/ADHESION CONTROL
// =================================================================

void World::setCohesionBindForceEnabled(bool enabled)
{
    cohesion_bind_force_enabled_ = enabled;
}

bool World::isCohesionBindForceEnabled() const
{
    return cohesion_bind_force_enabled_;
}

void World::setCohesionComForceEnabled(bool enabled)
{
    pImpl->physicsSettings_.cohesion_enabled = enabled;
    pImpl->physicsSettings_.cohesion_strength = enabled ? 150.0 : 0.0;
}

bool World::isCohesionComForceEnabled() const
{
    return pImpl->physicsSettings_.cohesion_strength > 0.0;
}

void World::setCohesionComForceStrength(double strength)
{
    pImpl->physicsSettings_.cohesion_strength = strength;
}

double World::getCohesionComForceStrength() const
{
    return pImpl->physicsSettings_.cohesion_strength;
}

void World::setAdhesionStrength(double strength)
{
    pImpl->physicsSettings_.adhesion_strength = strength;
}

double World::getAdhesionStrength() const
{
    return pImpl->physicsSettings_.adhesion_strength;
}

void World::setAdhesionEnabled(bool enabled)
{
    pImpl->physicsSettings_.adhesion_enabled = enabled;
    pImpl->physicsSettings_.adhesion_strength = enabled ? 5.0 : 0.0;
}

bool World::isAdhesionEnabled() const
{
    return pImpl->physicsSettings_.adhesion_strength > 0.0;
}

void World::setCohesionBindForceStrength(double strength)
{
    cohesion_bind_force_strength_ = strength;
}

double World::getCohesionBindForceStrength() const
{
    return cohesion_bind_force_strength_;
}

// =================================================================
// VISCOSITY/FRICTION CONTROL
// =================================================================

void World::setViscosityStrength(double strength)
{
    pImpl->physicsSettings_.viscosity_strength = strength;
}

double World::getViscosityStrength() const
{
    return pImpl->physicsSettings_.viscosity_strength;
}

void World::setFrictionStrength(double strength)
{
    pImpl->physicsSettings_.friction_strength = strength;
}

double World::getFrictionStrength() const
{
    return pImpl->physicsSettings_.friction_strength;
}

void World::setCOMCohesionRange(int range)
{
    com_cohesion_range_ = range;
}

int World::getCOMCohesionRange() const
{
    return com_cohesion_range_;
}

// =================================================================
// AIR RESISTANCE CONTROL
// =================================================================

void World::setAirResistanceEnabled(bool enabled)
{
    air_resistance_enabled_ = enabled;
}

bool World::isAirResistanceEnabled() const
{
    return air_resistance_enabled_;
}

void World::setAirResistanceStrength(double strength)
{
    air_resistance_strength_ = strength;
}

double World::getAirResistanceStrength() const
{
    return air_resistance_strength_;
}

// =================================================================
// OTHER METHODS
// =================================================================

void World::setRandomSeed(uint32_t seed)
{
    rng_ = std::make_unique<std::mt19937>(seed);
    spdlog::debug("World RNG seed set to {}", seed);
}

std::string World::toAsciiDiagram() const
{
    return WorldDiagramGeneratorEmoji::generateEmojiDiagram(*this);
}

// =================================================================
// CORE SIMULATION METHODS
// =================================================================

void World::advanceTime(double deltaTimeSeconds)
{
    ScopeTimer timer(pImpl->timers_, "advance_time");

    const double scaledDeltaTime = deltaTimeSeconds * pImpl->physicsSettings_.timescale;
    spdlog::debug(
        "World::advanceTime: deltaTime={:.4f}s, timestep={}",
        deltaTimeSeconds,
        pImpl->data_.timestep);
    if (scaledDeltaTime == 0.0) {
        return;
    }

    pImpl->light_calculator_.clearAllEmissive();

    // Rebuild grid cache for current frame (maps may have changed from previous step).
    {
        ScopeTimer timer(pImpl->timers_, "grid_cache_rebuild");
        pImpl->grid_.emplace(
            pImpl->data_.cells, pImpl->data_.debug_info, pImpl->data_.width, pImpl->data_.height);
    }
    GridOfCells& grid = *pImpl->grid_;

    // Inject hydrostatic pressure from gravity.
    if (pImpl->physicsSettings_.pressure_hydrostatic_strength > 0.0) {
        ScopeTimer hydroTimer(pImpl->timers_, "hydrostatic_pressure");
        pImpl->pressure_calculator_.injectGravityPressure(*this, scaledDeltaTime);
    }

    // Add dynamic pressure from last frame's collisions.
    if (pImpl->physicsSettings_.pressure_dynamic_strength > 0.0) {
        ScopeTimer dynamicTimer(pImpl->timers_, "dynamic_pressure");
        pImpl->pressure_calculator_.processBlockedTransfers(
            *this, pImpl->pressure_calculator_.blocked_transfers_);
        pImpl->pressure_calculator_.blocked_transfers_.clear();
    }

    // Diffuse all pressure together before applying forces.
    if (pImpl->physicsSettings_.pressure_diffusion_strength > 0.0) {
        ScopeTimer diffusionTimer(pImpl->timers_, "pressure_diffusion");
        pImpl->pressure_calculator_.applyPressureDiffusion(*this, scaledDeltaTime);
    }

    // Decay dynamic pressure.
    {
        ScopeTimer decayTimer(pImpl->timers_, "pressure_decay");
        pImpl->pressure_calculator_.applyPressureDecay(*this, scaledDeltaTime);
    }

    // Update organisms before force accumulation so new cells participate in physics.
    {
        ScopeTimer organismTimer(pImpl->timers_, "organisms");
        organism_manager_->update(*this, scaledDeltaTime);
    }

    // Apply forces using the diffused pressure field.
    resolveForces(scaledDeltaTime, grid);

    // Advance rigid body organisms (Goose, etc.) now that world forces are applied to cells.
    // These organisms gather forces from their cells and integrate their own velocity.
    {
        ScopeTimer organismPhysicsTimer(pImpl->timers_, "organism_physics");
        organism_manager_->advanceTime(*this, scaledDeltaTime);
    }

    // Resolve rigid body physics for organism structures (Tree, Duck).
    resolveRigidBodies(scaledDeltaTime);

    {
        ScopeTimer velocityTimer(pImpl->timers_, "velocity_limiting");
        processVelocityLimiting(scaledDeltaTime);
    }

    {
        ScopeTimer transfersTimer(pImpl->timers_, "update_transfers");
        pImpl->pending_moves_ = computeMaterialMoves(scaledDeltaTime);
    }

    // Process material moves - detects collisions for next frame's dynamic pressure.
    processMaterialMoves();

    // Rebuild grid cache after transfers so lighting uses current occupancy.
    {
        ScopeTimer timer(pImpl->timers_, "grid_cache_rebuild_post_moves");
        pImpl->grid_.emplace(
            pImpl->data_.cells, pImpl->data_.debug_info, pImpl->data_.width, pImpl->data_.height);
    }

    // Prune disconnected organism fragments AFTER transfers complete.
    // This ensures connectivity checks use current positions, not stale pre-transfer positions.
    pruneDisconnectedFragments();

    // Inject organism emissions before light calculation.
    organism_manager_->injectEmissions(pImpl->light_calculator_);

    // Calculate lighting for rendering.
    {
        ScopeTimer lightTimer(pImpl->timers_, "light_calculation");
        pImpl->light_calculator_.calculate(
            *this, *pImpl->grid_, pImpl->physicsSettings_.light, pImpl->timers_);
    }

    // Sync organism render data to WorldData.entities for UI.
    organism_manager_->syncEntitiesToWorldData(*this);

    pImpl->data_.timestep++;
}

// DEPRECATED: World setup now handled by Scenario::setup().
void World::setup()
{
    spdlog::warn("World::setup() is deprecated - use Scenario::setup() instead");
}

// =================================================================.
// MATERIAL ADDITION METHODS.
// =================================================================.

void World::addMaterialAtCell(Vector2s pos, Material::EnumType type, float amount)
{
    if (!isValidCell(pos)) {
        return;
    }

    Cell& cell = pImpl->data_.at(pos.x, pos.y);
    const float added = cell.addMaterial(type, amount);

    if (added > 0.0f) {
        spdlog::trace("Added {:.3f} {} at cell ({},{})", added, toString(type), pos.x, pos.y);
    }
}

void World::addMaterialAtCell(int x, int y, Material::EnumType type, float amount)
{
    addMaterialAtCell(Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(y) }, type, amount);
}

// =================================================================.
// BLESSED API - Cell Manipulation with Organism Tracking.
// =================================================================.

void World::swapCells(Vector2s pos1, Vector2s pos2)
{
    // Validate positions.
    if (!isValidCell(pos1) || !isValidCell(pos2)) {
        spdlog::warn(
            "swapCells: Invalid positions ({}, {}) or ({}, {})", pos1.x, pos1.y, pos2.x, pos2.y);
        return;
    }

    WorldData& data = pImpl->data_;
    Cell& cell1 = data.at(pos1.x, pos1.y);
    Cell& cell2 = data.at(pos2.x, pos2.y);

    // Capture organism IDs before swap.
    OrganismId org1 = organism_manager_->at(pos1);
    OrganismId org2 = organism_manager_->at(pos2);

    // Perform the swap.
    std::swap(cell1, cell2);

    // Update organism tracking.
    if (org1 != INVALID_ORGANISM_ID || org2 != INVALID_ORGANISM_ID) {
        LOG_INFO(
            Swap,
            "swapCells: ({}, {}) ↔ ({}, {}) - organisms: {} ↔ {}",
            pos1.x,
            pos1.y,
            pos2.x,
            pos2.y,
            org1,
            org2);
        organism_manager_->swapOrganisms(pos1, pos2);
    }
}

void World::replaceMaterialAtCell(Vector2s pos, Material::EnumType material)
{
    const WorldData& data = pImpl->data_;

    if (!isValidCell(pos)) {
        return;
    }

    // AIR means "clear this cell" - delegate to clearCellAtPosition.
    if (material == Material::EnumType::Air) {
        clearCellAtPosition(pos);
        return;
    }

    Cell& cell = pImpl->data_.at(pos.x, pos.y);

    if (cell.isEmpty() || cell.material_type == material) {
        OrganismId org_id = organism_manager_->at(pos);
        if (org_id != INVALID_ORGANISM_ID) {
            spdlog::critical(
                "replaceMaterialAtCell({},{},{}): Empty cell has organism_id={}!",
                pos.x,
                pos.y,
                toString(material),
                org_id);
            spdlog::critical(
                "  Cell: material={}, fill={:.2f}", toString(cell.material_type), cell.fill_ratio);

            auto* organism = organism_manager_->getOrganism(org_id);
            if (organism) {
                spdlog::critical(
                    "  Organism: type={}, anchor=({},{}), cells.size()={}",
                    static_cast<int>(organism->getType()),
                    organism->getAnchorCell().x,
                    organism->getAnchorCell().y,
                    organism->getCells().size());
            }

            spdlog::critical(
                "World state:\n{}", WorldDiagramGeneratorEmoji::generateEmojiDiagram(*this));

            DIRTSIM_ASSERT(false, "replaceMaterialAtCell: Empty cell should not have organism");
        }
        cell.replaceMaterial(material, 1.0);
        return;
    }

    // Find best adjacent cell to displace existing material (prefer empty, then least-filled).
    Vector2s best_dir{ 0, 0 };
    float best_score = -999.0f;
    float best_fill = 2.0f; // Track fill ratio of best neighbor (2.0 = no valid neighbor yet).

    static constexpr std::array<std::pair<int16_t, int16_t>, 4> directions = {
        { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
    };

    for (auto [dx, dy] : directions) {
        Vector2s neighbor_pos{ static_cast<int16_t>(pos.x + dx), static_cast<int16_t>(pos.y + dy) };

        if (!isValidCell(neighbor_pos)) {
            continue;
        }

        // Never displace into cells that belong to an organism.
        if (organism_manager_->at(neighbor_pos) != INVALID_ORGANISM_ID) {
            continue;
        }

        const Cell& neighbor = data.at(neighbor_pos.x, neighbor_pos.y);
        float com_score = cell.com.x * dx + cell.com.y * dy;

        // Prefer empty cells, but consider filled cells as fallback.
        if (neighbor.isEmpty()) {
            // Empty cell - high priority.
            if (com_score > best_score || best_fill > 0.5f) {
                best_score = com_score;
                best_dir = { static_cast<int16_t>(dx), static_cast<int16_t>(dy) };
                best_fill = 0.0f;
            }
        }
        else if (best_fill > 0.5f) {
            // No empty found yet - track least-filled option.
            if (neighbor.fill_ratio < best_fill) {
                best_score = com_score;
                best_dir = { static_cast<int16_t>(dx), static_cast<int16_t>(dy) };
                best_fill = neighbor.fill_ratio;
            }
        }
    }

    // Expand search radius if still no good option (prefer empty, fallback to least-filled).
    if (best_fill > 0.5f) {
        for (int radius = 2; radius <= 4; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) {
                        continue;
                    }

                    Vector2s neighbor_pos{ static_cast<int16_t>(pos.x + dx),
                                           static_cast<int16_t>(pos.y + dy) };

                    if (!isValidCell(neighbor_pos)) {
                        continue;
                    }

                    // Never displace into cells that belong to an organism.
                    if (organism_manager_->at(neighbor_pos) != INVALID_ORGANISM_ID) {
                        continue;
                    }

                    const Cell& neighbor = data.at(neighbor_pos.x, neighbor_pos.y);
                    float score = cell.com.x * dx + cell.com.y * dy;

                    if (neighbor.isEmpty()) {
                        if (score > best_score || best_fill > 0.5f) {
                            best_score = score;
                            best_dir = { static_cast<int16_t>(dx), static_cast<int16_t>(dy) };
                            best_fill = 0.0f;
                        }
                    }
                    else if (best_fill > 0.5f && neighbor.fill_ratio < best_fill) {
                        best_score = score;
                        best_dir = { static_cast<int16_t>(dx), static_cast<int16_t>(dy) };
                        best_fill = neighbor.fill_ratio;
                    }
                }
            }
        }
    }

    // Last resort: if completely surrounded, just overwrite in place.
    if (best_dir.x == 0 && best_dir.y == 0) {
        OrganismId org_id = organism_manager_->at(pos);
        if (org_id != INVALID_ORGANISM_ID) {
            spdlog::warn(
                "World: replaceMaterialAtCell({},{},{}) destroying trapped organism {}",
                pos.x,
                pos.y,
                toString(material),
                org_id);
            organism_manager_->removeOrganismFromWorld(*this, org_id);
        }
        cell.replaceMaterial(material, 1.0);
        return;
    }

    // Displace existing material to empty neighbor.
    Vector2s empty_pos{ static_cast<int16_t>(pos.x + best_dir.x),
                        static_cast<int16_t>(pos.y + best_dir.y) };

    DIRTSIM_ASSERT(
        organism_manager_->at(empty_pos) == INVALID_ORGANISM_ID,
        "replaceMaterialAtCell: Cannot displace into organism cell");

    OrganismId displaced_org = organism_manager_->at(pos);
    if (displaced_org != INVALID_ORGANISM_ID) {
        spdlog::info(
            "World: replaceMaterialAtCell displacing organism {} from ({},{}) to ({},{})",
            displaced_org,
            pos.x,
            pos.y,
            empty_pos.x,
            empty_pos.y);
    }

    swapCells(empty_pos, pos);

    // Now target is empty (displaced material moved to empty_pos). Place new material.
    pImpl->data_.at(pos.x, pos.y) = Cell{ material, 1.0 };
}

void World::clearCellAtPosition(Vector2s pos)
{
    if (!isValidCell(pos)) {
        return;
    }

    // Skip cells that belong to an organism.
    if (organism_manager_->at(pos) != INVALID_ORGANISM_ID) {
        return;
    }

    pImpl->data_.at(pos.x, pos.y).clear();
}

// =================================================================.
// GRID MANAGEMENT.
// =================================================================.

void World::resizeGrid(int16_t newWidth, int16_t newHeight)
{
    if (!shouldResize(newWidth, newHeight)) {
        return;
    }

    onPreResize(newWidth, newHeight);

    // Capture continuous positions (anchor + COM) before clearing cells.
    // This preserves sub-cell precision across resize operations.
    if (organism_manager_) {
        organism_manager_->forEachOrganism([this](Organism::Body& organism) {
            Vector2i anchor = organism.getAnchorCell();

            // Read COM from world cell to get full continuous position.
            Vector2d com{ 0.0, 0.0 };
            if (pImpl->data_.inBounds(anchor.x, anchor.y)) {
                const Cell& cell = pImpl->data_.at(anchor.x, anchor.y);
                com = cell.com;
            }

            // Convert anchor + COM to continuous position.
            organism.position = Vector2d{ static_cast<double>(anchor.x) + (com.x + 1.0) / 2.0,
                                          static_cast<double>(anchor.y) + (com.y + 1.0) / 2.0 };
        });

        // Now clear organism cells from world grid before interpolation.
        organism_manager_->forEachOrganism([this](Organism::Body& organism) {
            for (const auto& pos : organism.getCells()) {
                if (pImpl->data_.inBounds(pos.x, pos.y)) {
                    pImpl->data_.at(pos.x, pos.y) = Cell(); // Clear to AIR.
                }
            }
        });
    }

    // Generate interpolated cells using the interpolation tool.
    std::vector<Cell> interpolatedCells = WorldInterpolationTool::generateInterpolatedCellsB(
        pImpl->data_.cells, pImpl->data_.width, pImpl->data_.height, newWidth, newHeight);

    // Update world state with the new interpolated cells.
    pImpl->data_.width = newWidth;
    pImpl->data_.height = newHeight;
    pImpl->data_.cells = std::move(interpolatedCells);
    pImpl->data_.debug_info.resize(newWidth * newHeight);

    // Resize light calculator emissive overlay to match new dimensions.
    pImpl->light_calculator_.resize(newWidth, newHeight);

    // Resize organism grid and reposition organisms at new proportional positions.
    // OrganismManager::resizeGrid() handles all repositioning internally.
    if (organism_manager_) {
        organism_manager_->resizeGrid(newWidth, newHeight);

        // Reproject organism cells onto the new world grid.
        organism_manager_->forEachOrganism([this](Organism::Body& organism) {
            Vector2i anchor = organism.getAnchorCell();

            for (const auto& pos : organism.getCells()) {
                // TODO: Preserve original material type. For now, use WOOD.
                Cell& cell = pImpl->data_.at(pos.x, pos.y);
                cell.replaceMaterial(Material::EnumType::Wood, 1.0);

                // Write COM to anchor cell (sub-cell position).
                if (pos == anchor) {
                    cell.com = organism.center_of_mass;
                }
            }
        });
    }

    spdlog::info("World bilinear resize complete");
}

// =================================================================.
// INTERNAL PHYSICS METHODS.
// =================================================================.

void World::applyGravity()
{
    // Cache pImpl members as local references.
    std::vector<Cell>& cells = pImpl->data_.cells;
    std::vector<CellDebug>& debug_info = pImpl->data_.debug_info;
    const double gravity = pImpl->physicsSettings_.gravity;

    for (size_t idx = 0; idx < cells.size(); ++idx) {
        Cell& cell = cells[idx];
        if (!cell.isEmpty() && !cell.isWall()) {
            // Gravity force is proportional to material density (F = m × g).
            // This enables buoyancy: denser materials sink, lighter materials float.
            const Material::Properties& props = Material::getProperties(cell.material_type);
            Vector2d gravityForce(0.0, props.density * gravity);

            // Accumulate gravity force instead of applying directly.
            cell.addPendingForce(gravityForce);

            // Debug tracking.
            debug_info[idx].accumulated_gravity_force = gravityForce;
        }
    }
}

void World::applyAirResistance()
{
    if (!air_resistance_enabled_) {
        return;
    }

    // Cache pImpl members as local references.
    WorldData& data = pImpl->data_;

    WorldAirResistanceCalculator air_resistance_calculator{};

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            Cell& cell = data.at(x, y);

            if (!cell.isEmpty() && !cell.isWall()) {
                // Skip rigid body organism cells - they compute their own air resistance.
                OrganismId org_id =
                    organism_manager_->at(Vector2i{ static_cast<int>(x), static_cast<int>(y) });
                if (org_id != INVALID_ORGANISM_ID) {
                    auto* organism = organism_manager_->getOrganism(org_id);
                    if (organism && organism->usesRigidBodyPhysics()) {
                        continue;
                    }
                }

                Vector2d air_resistance_force = air_resistance_calculator.calculateAirResistance(
                    *this, x, y, air_resistance_strength_);
                cell.addPendingForce(air_resistance_force);
            }
        }
    }
}

void World::applyCohesionForces(const GridOfCells& grid)
{
    // Cache pImpl members as local references.
    PhysicsSettings& settings = pImpl->physicsSettings_;
    Timers& timers = pImpl->timers_;
    WorldData& data = pImpl->data_;
    WorldAdhesionCalculator& adhesion_calc = pImpl->adhesion_calculator_;

    if (settings.cohesion_strength <= 0.0) {
        return;
    }

    // Create calculators once outside the loop.
    WorldCohesionCalculator cohesion_calc{};

    {
        ScopeTimer cohesionTimer(timers, "cohesion_calculation");

        // Parallelize when both cache and OpenMP are enabled.
#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static) if ( \
        GridOfCells::USE_CACHE && GridOfCells::USE_OPENMP && data.height * data.width >= 2500)
#endif
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                Cell& cell = data.at(x, y);

                if (cell.isEmpty() || cell.isWall()) {
                    continue;
                }

                // Calculate COM cohesion force (passes grid for cache optimization).
                WorldCohesionCalculator::COMCohesionForce com_cohesion =
                    cohesion_calc.calculateCOMCohesionForce(
                        *this, x, y, com_cohesion_range_, &grid);

                // Cache resistance for use in resolveForces (eliminates redundant calculation).
                const_cast<GridOfCells&>(grid).setCohesionResistance(
                    x, y, com_cohesion.resistance_magnitude);

                Vector2d com_cohesion_force(0.0, 0.0);
                if (com_cohesion.force_active) {
                    com_cohesion_force = com_cohesion.force_direction * com_cohesion.force_magnitude
                        * settings.cohesion_strength;

                    if (cell.velocity.magnitude() > 0.01) {
                        double alignment = cell.velocity.dot(com_cohesion_force.normalize());
                        double correction_factor = std::max(0.0, 1.0 - alignment);
                        com_cohesion_force = com_cohesion_force * correction_factor;
                    }

                    cell.addPendingForce(com_cohesion_force);
                }
                // Store for visualization in GridOfCells debug info.
                const_cast<GridOfCells&>(grid).debugAt(x, y).accumulated_com_cohesion_force =
                    com_cohesion_force;
            }
        }
    }

    // Adhesion force accumulation (only if enabled).
    if (settings.adhesion_strength > 0.0) {
        ScopeTimer adhesionTimer(timers, "adhesion_calculation");

        // Parallelize when both cache and OpenMP are enabled.
#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static) if ( \
        GridOfCells::USE_CACHE && GridOfCells::USE_OPENMP && data.height * data.width >= 2500)
#endif
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                Cell& cell = data.at(x, y);

                if (cell.isEmpty() || cell.isWall()) {
                    continue;
                }

                // Use cache-optimized version with MaterialNeighborhood.
                const MaterialNeighborhood mat_n = grid.getMaterialNeighborhood(x, y);
                WorldAdhesionCalculator::AdhesionForce adhesion =
                    adhesion_calc.calculateAdhesionForce(*this, x, y, mat_n);
                Vector2d adhesion_force = adhesion.force_direction * adhesion.force_magnitude
                    * settings.adhesion_strength;
                cell.addPendingForce(adhesion_force);
                // Store for visualization in GridOfCells debug info.
                const_cast<GridOfCells&>(grid).debugAt(x, y).accumulated_adhesion_force =
                    adhesion_force;
            }
        }
    }
}

void World::applyPressureForces()
{
    // Cache pImpl members as local references.
    PhysicsSettings& settings = pImpl->physicsSettings_;
    WorldData& data = pImpl->data_;
    WorldPressureCalculator& pressure_calc = pImpl->pressure_calculator_;

    if (settings.pressure_hydrostatic_strength <= 0.0
        && settings.pressure_dynamic_strength <= 0.0) {
        return;
    }

    // Apply pressure forces through the pending force system.
    // Parallelize when both cache and OpenMP are enabled.
#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static) if ( \
        GridOfCells::USE_CACHE && GridOfCells::USE_OPENMP && data.height * data.width >= 2500)
#endif
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            Cell& cell = data.at(x, y);

            // Skip empty cells and walls.
            if (cell.isEmpty() || cell.isWall()) {
                continue;
            }

            // Get total pressure for this cell.
            double total_pressure = cell.pressure;
            if (total_pressure < MIN_MATTER_THRESHOLD) {
                continue;
            }

            // Calculate pressure gradient to determine force direction.
            // The gradient is calculated as (center_pressure - neighbor_pressure) * direction,
            // which points AWAY from high pressure regions (toward increasing pressure).
            Vector2d gradient = pressure_calc.calculatePressureGradient(*this, x, y);

            // Only apply force if system is out of equilibrium.
            if (gradient.magnitude() > 0.001) {
                // Get material-specific hydrostatic weight to scale pressure response.
                const Material::Properties& props = Material::getProperties(cell.material_type);
                double hydrostatic_weight = props.hydrostatic_weight;

                Vector2d pressure_force = gradient * settings.pressure_scale * hydrostatic_weight;
                cell.addPendingForce(pressure_force);

                spdlog::debug(
                    "Cell ({},{}) pressure force: total_pressure={:.4f}, "
                    "gradient=({:.4f},{:.4f}), force=({:.4f},{:.4f})",
                    x,
                    y,
                    total_pressure,
                    gradient.x,
                    gradient.y,
                    pressure_force.x,
                    pressure_force.y);
            }
        }
    }
}

void World::resolveForces(double deltaTime, const GridOfCells& grid)
{
    // Cache frequently accessed pImpl members as local references to eliminate indirection
    // overhead.
    Timers& timers = pImpl->timers_;
    WorldViscosityCalculator& viscosity_calc = pImpl->viscosity_calculator_;
    PhysicsSettings& settings = pImpl->physicsSettings_;
    WorldData& data = pImpl->data_;
    std::vector<Cell>& cells = data.cells;

    ScopeTimer timer(timers, "resolve_forces");

    // Clear pending forces at the start of each physics frame.
    // Skip organism cells - they preserve forces added during organism update.
    {
        ScopeTimer clearTimer(timers, "resolve_forces_clear_pending");
        const auto& org_grid = organism_manager_->getGrid();
        for (size_t i = 0; i < cells.size(); ++i) {
            if (org_grid[i] == INVALID_ORGANISM_ID) {
                cells[i].clearPendingForce();
            }
        }
    }

    // Scenario tick - apply scenario forces after clear, before physics forces.
    // This allows scenarios to use addPendingForce() and have forces processed normally.
    if (scenario_) {
        ScopeTimer scenarioTimer(timers, "resolve_forces_scenario_tick");
        scenario_->tick(*this, deltaTime);
    }

    // Apply gravity forces.
    {
        ScopeTimer gravityTimer(timers, "resolve_forces_apply_gravity");
        applyGravity();
    }

    // Apply air resistance forces.
    {
        ScopeTimer airResistanceTimer(timers, "resolve_forces_apply_air_resistance");
        applyAirResistance();
    }

    // Apply pressure forces from previous frame.
    {
        ScopeTimer pressureTimer(timers, "resolve_forces_apply_pressure");
        applyPressureForces();
    }

    // Apply cohesion and adhesion forces.
    {
        ScopeTimer cohesionTimer(timers, "resolve_forces_apply_cohesion");
        applyCohesionForces(grid);
    }

    // Apply contact-based friction forces.
    {
        ScopeTimer frictionTimer(timers, "resolve_forces_apply_friction");
        // Construct friction calculator with grid reference.
        // Cast away const for debug writes (safe - doesn't affect physics state).
        WorldFrictionCalculator friction_calc{ const_cast<GridOfCells&>(grid) };
        friction_calc.setFrictionStrength(settings.friction_strength);
        friction_calc.calculateAndApplyFrictionForces(*this, deltaTime);
    }

    // Apply organism bone forces.
    ScopeTimer boneTimer(timers, "resolve_forces_apply_bones");
    organism_manager_->applyBoneForces(*this, deltaTime);

    // Apply viscous forces (momentum diffusion between same-material neighbors).
    if (settings.viscosity_strength > 0.0) {
        ScopeTimer viscosityTimer(timers, "apply_viscous_forces");
        double visc_strength = settings.viscosity_strength; // Cache once for entire loop.

        // Parallelize when cache is enabled (use sequential for reference path).
#ifdef _OPENMP
#pragma omp parallel for collapse(2) \
    schedule(static) if (GridOfCells::USE_CACHE && data.height * data.width >= 2500)
#endif
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                Cell& cell = data.at(x, y);

                if (cell.isEmpty() || cell.isWall()) {
                    continue;
                }

                // Calculate viscous force from neighbor velocity averaging.
                auto viscous_result =
                    viscosity_calc.calculateViscousForce(*this, x, y, visc_strength, &grid);
                cell.addPendingForce(viscous_result.force);

                // Store for visualization in GridOfCells debug info.
                const_cast<GridOfCells&>(grid).debugAt(x, y).accumulated_viscous_force =
                    viscous_result.force;
            }
        }
    }

    // Now resolve all accumulated forces directly (no damping).
    {
        ScopeTimer resolutionLoopTimer(timers, "resolve_forces_resolution_loop");

        // Use bitmaps to skip empty/wall cells before dereferencing Cell object.
        const CellBitmap& empty_bitmap = grid.emptyCells();
        const CellBitmap& wall_bitmap = grid.wallCells();

        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                // Fast bitmap checks - skip without dereferencing cell.
                if (empty_bitmap.isSet(x, y) || wall_bitmap.isSet(x, y)) {
                    continue;
                }

                Cell& cell = data.at(x, y);

                // Skip organism cells - they're handled by resolveRigidBodies().
                if (organism_manager_->hasOrganism(
                        Vector2i{ static_cast<int>(x), static_cast<int>(y) })) {
                    continue;
                }

                // Get the total pending force (includes gravity, pressure, cohesion,
                // adhesion, friction, viscosity, bones, etc).
                Vector2d net_force = cell.pending_force;

                // Apply F = ma: acceleration = force / mass.
                double mass = cell.getMass();
                Vector2d velocity_change;
                if (mass > 0.0001) {
                    velocity_change = net_force * (1.0 / mass) * deltaTime;
                }
                else {
                    // Near-zero mass (empty cells) - no acceleration.
                    velocity_change = Vector2d(0.0, 0.0);
                }
                cell.velocity += velocity_change;

                // Debug logging.
                if (net_force.magnitude() > 0.001) {
                    spdlog::debug(
                        "Cell ({},{}) {} - Force: ({:.3f},{:.3f}), vel_change: "
                        "({:.3f},{:.3f}), "
                        "new_vel: ({:.3f},{:.3f})",
                        x,
                        y,
                        toString(cell.material_type),
                        net_force.x,
                        net_force.y,
                        velocity_change.x,
                        velocity_change.y,
                        cell.velocity.x,
                        cell.velocity.y);
                }
            }
        }
    }
}

void World::resolveRigidBodies(double deltaTime)
{
    ScopeTimer timer(pImpl->timers_, "resolve_rigid_bodies");

    if (!organism_manager_) {
        return;
    }

    WorldData& data = pImpl->data_;

    // Process each organism that has structural cells (currently just trees).
    organism_manager_->forEachOrganism([&](Organism::Body& organism) {
        // For single-cell organisms (like ducks), apply simple F=ma physics.
        if (organism.getType() != OrganismType::TREE) {
            Vector2i anchor = organism.getAnchorCell();
            if (anchor.x >= 0 && anchor.y >= 0 && anchor.x < data.width && anchor.y < data.height) {
                Cell& cell = data.at(anchor.x, anchor.y);
                double mass = cell.getMass();
                if (mass > 0.0001) {
                    Vector2d acceleration = cell.pending_force * (1.0 / mass);
                    cell.velocity += acceleration * deltaTime;
                }
            }
            return;
        }

        OrganismId organism_id = organism.getId();
        Vector2i anchor = organism.getAnchorCell();

        // 1. Flood fill from anchor to find connected structural cells.
        std::unordered_set<Vector2i, Vector2iHash> connected;
        std::queue<Vector2i> frontier;

        frontier.push(anchor);

        while (!frontier.empty()) {
            Vector2i pos = frontier.front();
            frontier.pop();

            // Bounds check.
            if (pos.x < 0 || pos.y < 0 || pos.x >= data.width || pos.y >= data.height) {
                continue;
            }

            // Already visited.
            if (connected.count(pos)) {
                continue;
            }

            // Must belong to this organism.
            if (organism_manager_->at(pos) != organism_id) {
                continue;
            }

            Cell& cell = data.at(pos.x, pos.y);

            // Only SEED, ROOT, and WOOD form structural connections (LEAF excluded).
            if (cell.material_type != Material::EnumType::Seed
                && cell.material_type != Material::EnumType::Root
                && cell.material_type != Material::EnumType::Wood) {
                continue;
            }

            connected.insert(pos);

            // Add 4 cardinal neighbors to frontier.
            frontier.push({ pos.x - 1, pos.y });
            frontier.push({ pos.x + 1, pos.y });
            frontier.push({ pos.x, pos.y - 1 });
            frontier.push({ pos.x, pos.y + 1 });
        }

        // 2. Apply unified velocity to connected structure.
        // NOTE: Connectivity pruning is deferred until AFTER material transfers complete.
        // Running it here causes issues because cells may swap positions later in the frame,
        // making connectivity checks based on stale positions.
        if (connected.empty()) {
            return; // Lambda return, not function return.
        }

        // Gather forces and mass.
        Vector2d total_force;
        double total_mass = 0;

        for (const auto& pos : connected) {
            Cell& cell = data.at(pos.x, pos.y);
            total_force += cell.pending_force;
            total_mass += cell.getMass();
        }

        if (total_mass < 0.0001) {
            return; // Lambda return.
        }

        // Convert connected set to vector for support force calculation.
        std::vector<Vector2i> connected_vec(connected.begin(), connected.end());

        // Add ground support force from pressure field (Newton's Third Law).
        Vector2d support_force = computeOrganismSupportForce(connected_vec, organism_id);
        total_force += support_force;

        // F = ma -> a = F/m.
        Vector2d acceleration = total_force * (1.0 / total_mass);

        // Get current velocity from anchor cell.
        Vector2d velocity = data.at(anchor.x, anchor.y).velocity;
        velocity += acceleration * deltaTime;

        // Apply unified velocity to all connected cells.
        for (const auto& pos : connected) {
            data.at(pos.x, pos.y).velocity = velocity;
        }

        spdlog::debug(
            "Organism {} ({} connected cells): unified velocity=({:.3f}, {:.3f})",
            organism_id,
            connected.size(),
            velocity.x,
            velocity.y);
    }); // End forEachOrganism lambda.

    // Clear pending forces for all organism cells now that they've been applied.
    const auto& org_grid = organism_manager_->getGrid();
    for (size_t i = 0; i < data.cells.size(); ++i) {
        if (org_grid[i] != INVALID_ORGANISM_ID) {
            data.cells[i].clearPendingForce();
        }
    }
}

void World::pruneDisconnectedFragments()
{
    if (!organism_manager_) {
        return;
    }

    WorldData& data = pImpl->data_;

    organism_manager_->forEachOrganism([&](Organism::Body& organism) {
        if (organism.getType() != OrganismType::TREE) {
            return; // Only trees have structural connectivity requirements.
        }

        OrganismId organism_id = organism.getId();
        Vector2i anchor = organism.getAnchorCell();

        // Flood fill from anchor to find connected structural cells.
        std::unordered_set<Vector2i, Vector2iHash> connected;
        std::queue<Vector2i> frontier;

        frontier.push(anchor);

        while (!frontier.empty()) {
            Vector2i pos = frontier.front();
            frontier.pop();

            // Bounds check.
            if (pos.x < 0 || pos.y < 0 || pos.x >= data.width || pos.y >= data.height) {
                continue;
            }

            // Already visited.
            if (connected.count(pos)) {
                continue;
            }

            // Must belong to this organism.
            if (organism_manager_->at(pos) != organism_id) {
                continue;
            }

            Cell& cell = data.at(pos.x, pos.y);

            // Only SEED, ROOT, and WOOD form structural connections (LEAF excluded).
            if (cell.material_type != Material::EnumType::Seed
                && cell.material_type != Material::EnumType::Root
                && cell.material_type != Material::EnumType::Wood) {
                continue;
            }

            connected.insert(pos);

            // Add 4 cardinal neighbors to frontier.
            frontier.push({ pos.x - 1, pos.y });
            frontier.push({ pos.x + 1, pos.y });
            frontier.push({ pos.x, pos.y - 1 });
            frontier.push({ pos.x, pos.y + 1 });
        }

        // Prune disconnected and empty cells.
        std::vector<Vector2i> to_remove;
        for (const auto& pos : organism.getCells()) {
            if (pos.x < 0 || pos.y < 0 || pos.x >= data.width || pos.y >= data.height) {
                to_remove.push_back(pos); // Out of bounds.
                continue;
            }

            Cell& cell = data.at(pos.x, pos.y);

            // Remove empty cells (cleanup after transfers).
            if (cell.isEmpty()) {
                to_remove.push_back(pos);
                spdlog::debug(
                    "Pruned empty cell: organism {} cell ({},{}) now AIR",
                    organism_id,
                    pos.x,
                    pos.y);
                continue;
            }

            // Remove cells that lost organism ownership (transferred to another organism).
            OrganismId cell_owner = organism_manager_->at(pos);
            if (cell_owner != organism_id) {
                to_remove.push_back(pos);
                spdlog::debug(
                    "Pruned transferred cell: organism {} cell ({},{}) now belongs to organism {}",
                    organism_id,
                    pos.x,
                    pos.y,
                    cell_owner);
                continue;
            }

            // TODO: Prune structurally disconnected ROOT/WOOD cells.
            // Disabled until Phase 4 (Structure Movement) is implemented.
            // Without position constraints, organism cells can create temporary gaps
            // during transfers, causing false disconnection detections.
            // For now, we only clean up empty cells and transferred cells above.
            (void)connected; // Suppress unused variable warning.
        }

        // Update organism's cell tracking.
        if (!to_remove.empty()) {
            organism_manager_->removeCellsFromOrganism(organism_id, to_remove);
        }
    });
}

Vector2d World::computeOrganismSupportForce(
    const std::vector<Vector2i>& organism_cells, OrganismId organism_id) const
{
    const WorldData& data = pImpl->data_;
    const PhysicsSettings& settings = pImpl->physicsSettings_;

    // Gravity direction (normalized). Y+ is down in our coordinate system.
    const double gravity = settings.gravity;
    const Vector2d gravity_dir{ 0.0, 1.0 };

    // Calculate total organism weight.
    double total_weight = 0.0;
    for (const auto& pos : organism_cells) {
        if (data.inBounds(pos.x, pos.y)) {
            const Cell& cell = data.at(pos.x, pos.y);
            total_weight += cell.getMass() * gravity;
        }
    }

    if (total_weight < 0.0001) {
        return { 0.0, 0.0 };
    }

    // Find contact surface - organism cells adjacent to ground.
    // For rigid structures, solid ground contact provides full support.
    double support_fraction = 0.0;
    int contact_count = 0;

    for (const auto& pos : organism_cells) {
        // Check cell below (in gravity direction).
        int ground_x = pos.x + static_cast<int>(gravity_dir.x);
        int ground_y = pos.y + static_cast<int>(gravity_dir.y);

        // World boundary = automatic full support.
        if (!data.inBounds(ground_x, ground_y)) {
            // At world boundary - provide full support.
            return { 0.0, -total_weight }; // Cancel all gravity.
        }

        const Cell& ground_cell = data.at(ground_x, ground_y);

        // Skip if ground cell is empty.
        if (ground_cell.isEmpty()) {
            continue;
        }

        // Skip if ground cell is part of the same organism (internal).
        Vector2i ground_pos{ ground_x, ground_y };
        if (organism_manager_->at(ground_pos) == organism_id) {
            continue;
        }

        ++contact_count;

        // Determine support quality based on ground material.
        Material::EnumType mat = ground_cell.material_type;

        // Solid materials provide full support (normal force).
        if (mat == Material::EnumType::Wall || mat == Material::EnumType::Metal
            || mat == Material::EnumType::Wood || mat == Material::EnumType::Dirt
            || mat == Material::EnumType::Sand || mat == Material::EnumType::Seed
            || mat == Material::EnumType::Root) {
            // Full support from this contact point.
            support_fraction += 1.0;
        }
        else if (mat == Material::EnumType::Water) {
            // Partial buoyancy-based support from water.
            // Support = (water_density / organism_density) based on fill ratio.
            double water_density = Material::getProperties(Material::EnumType::Water).density;
            support_fraction += water_density * ground_cell.fill_ratio;
        }
        else if (mat == Material::EnumType::Leaf) {
            // Leaves provide weak support.
            support_fraction += 0.3 * ground_cell.fill_ratio;
        }
        // AIR provides no support (handled by isEmpty check above).
    }

    // No contact = free fall.
    if (contact_count == 0) {
        return { 0.0, 0.0 };
    }

    // Normalize by number of organism cells to get average support per contact.
    // If all bottom cells have solid contact, support_fraction >= 1.0.
    // Cap at 1.0 (can't exceed full support).
    double normalized_support =
        std::min(support_fraction / static_cast<double>(contact_count), 1.0);

    // If we have any solid contact, provide full support for the whole structure.
    // This is physically correct: a tree on ground is fully supported.
    if (normalized_support > 0.5) {
        normalized_support = 1.0; // Snap to full support for solid contact.
    }

    double support_magnitude = total_weight * normalized_support;

    // Support force opposes gravity (points upward, -Y).
    Vector2d support_force = { 0.0, -support_magnitude };

    spdlog::debug(
        "Organism {} support: {} contact points, support_fraction={:.2f}, "
        "support_magnitude={:.2f}, weight={:.2f}",
        organism_id,
        contact_count,
        support_fraction,
        support_magnitude,
        total_weight);

    return support_force;
}

void World::processVelocityLimiting(double deltaTime)
{
    WorldVelocityLimitCalculator calculator;
    calculator.processAllCells(*this, deltaTime);
}

std::vector<MaterialMove> World::computeMaterialMoves(double deltaTime)
{
    // Cache pImpl members as local references.
    WorldCollisionCalculator& collision_calc = pImpl->collision_calculator_;
    WorldData& data = pImpl->data_;

    // Pre-allocate moves vector based on previous frame's count.
    static size_t last_move_count = 0;
    std::vector<MaterialMove> moves;
    moves.reserve(last_move_count + last_move_count / 10); // +10% buffer

    // Counters for move generation analysis.
    size_t num_cells_with_velocity = 0;
    size_t num_boundary_crossings = 0;
    size_t num_moves_generated = 0;
    size_t num_transfers_generated = 0;
    size_t num_collisions_generated = 0;

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            Cell& cell = data.at(x, y);

            // Skip empty, wall, and air cells - they don't generate material moves.
            if (cell.isEmpty() || cell.isWall() || cell.isAir()) {
                continue;
            }

            // Skip rigid body organism cells - they control their own position.
            Vector2i pos{ static_cast<int>(x), static_cast<int>(y) };
            OrganismId org_id = organism_manager_->at(pos);
            if (org_id != INVALID_ORGANISM_ID) {
                auto* organism = organism_manager_->getOrganism(org_id);
                if (organism && organism->usesRigidBodyPhysics()) {
                    continue;
                }
            }

            // Debug: Check if cell has any velocity or interesting COM.
            Vector2d current_velocity = cell.velocity;
            Vector2d oldCOM = cell.com;
            if (current_velocity.length() > 0.01 || std::abs(oldCOM.x) > 0.5
                || std::abs(oldCOM.y) > 0.5) {
                spdlog::debug(
                    "Cell ({},{}) {} - Velocity: ({:.3f},{:.3f}), COM: ({:.3f},{:.3f})",
                    x,
                    y,
                    toString(cell.material_type),
                    current_velocity.x,
                    current_velocity.y,
                    oldCOM.x,
                    oldCOM.y);
            }

            // Update COM based on velocity (with proper deltaTime integration).
            Vector2d newCOM = cell.com + cell.velocity * deltaTime;

            // Enhanced: Check if COM crosses any boundary [-1,1] for universal collision detection.
            BoundaryCrossings crossed_boundaries = collision_calc.getAllBoundaryCrossings(newCOM);

            if (!crossed_boundaries.empty()) {
                num_cells_with_velocity++;
                num_boundary_crossings += crossed_boundaries.count;

                spdlog::debug(
                    "Boundary crossings detected for {} at ({},{}) with COM ({:.2f},{:.2f}) -> {} "
                    "crossings",
                    toString(cell.material_type),
                    x,
                    y,
                    newCOM.x,
                    newCOM.y,
                    crossed_boundaries.count);
            }

            bool boundary_reflection_applied = false;

            // Corner crossings must pick ONE dominant direction.
            uint8_t num_moves_to_process = crossed_boundaries.count;
            if (crossed_boundaries.count > 1) {
                uint8_t keep_idx = (std::abs(cell.velocity.x) > std::abs(cell.velocity.y))
                    ? 0
                    : (crossed_boundaries.count - 1);
                crossed_boundaries.dirs[0] = crossed_boundaries.dirs[keep_idx];
                num_moves_to_process = 1;
            }

            for (uint8_t i = 0; i < num_moves_to_process; ++i) {
                const Vector2i& direction = crossed_boundaries.dirs[i];
                Vector2i targetPos = Vector2i(x, y) + direction;

                if (isValidCell(targetPos)) {
                    // Create enhanced MaterialMove with collision physics data.
                    MaterialMove move = collision_calc.createCollisionAwareMove(
                        *this,
                        cell,
                        data.at(targetPos.x, targetPos.y),
                        Vector2i(x, y),
                        targetPos,
                        deltaTime);

                    num_moves_generated++;
                    if (move.collision_type == CollisionType::TRANSFER_ONLY) {
                        num_transfers_generated++;
                    }
                    else {
                        num_collisions_generated++;
                    }

                    // Debug logging for collision detection.
                    if (move.collision_type != CollisionType::TRANSFER_ONLY) {
                        spdlog::debug(
                            "Collision detected: {} vs {} at ({},{}) -> ({},{}) - Type: {}, "
                            "Energy: {:.3f}",
                            toString(move.material),
                            toString(data.at(targetPos.x, targetPos.y).material_type),
                            x,
                            y,
                            targetPos.x,
                            targetPos.y,
                            static_cast<int>(move.collision_type),
                            move.collision_energy);
                    }

                    moves.push_back(move);
                }
                else {
                    // Hit world boundary - apply elastic reflection immediately.
                    spdlog::debug(
                        "World boundary hit: {} at ({},{}) direction=({},{}) - applying reflection",
                        toString(cell.material_type),
                        x,
                        y,
                        direction.x,
                        direction.y);

                    collision_calc.applyBoundaryReflection(cell, direction);
                    boundary_reflection_applied = true;
                }
            }

            // Always update the COM components that didn't cross boundaries.
            // This allows water to move horizontally even when hitting vertical boundaries.
            if (!boundary_reflection_applied) {
                // No reflections, update entire COM.
                cell.setCOM(newCOM);
            }
            else {
                // Reflections occurred. Update non-reflected components.
                Vector2d currentCOM = cell.com;
                Vector2d updatedCOM = currentCOM;

                // Check which boundaries were NOT crossed and update those components.
                bool x_reflected = false;
                bool y_reflected = false;

                for (uint8_t i = 0; i < crossed_boundaries.count; ++i) {
                    const Vector2i& dir = crossed_boundaries.dirs[i];
                    if (dir.x != 0) x_reflected = true;
                    if (dir.y != 0) y_reflected = true;
                }

                // Update components that didn't cross boundaries.
                if (!x_reflected && std::abs(newCOM.x) < 1.0) {
                    updatedCOM.x = newCOM.x;
                }
                if (!y_reflected && std::abs(newCOM.y) < 1.0) {
                    updatedCOM.y = newCOM.y;
                }

                cell.setCOM(updatedCOM);
            }
        }
    }

    // Log move generation statistics.
    spdlog::debug(
        "computeMaterialMoves: {} cells moving, {} boundary crossings, {} moves generated ({} "
        "transfers, {} collisions)",
        num_cells_with_velocity,
        num_boundary_crossings,
        num_moves_generated,
        num_transfers_generated,
        num_collisions_generated);

    // Update last move count for next frame's pre-allocation.
    last_move_count = moves.size();

    return moves;
}

void World::processMaterialMoves()
{
    // Cache pImpl members as local references.
    Timers& timers = pImpl->timers_;
    WorldCollisionCalculator& collision_calc = pImpl->collision_calculator_;
    PhysicsSettings& settings = pImpl->physicsSettings_;
    WorldData& data = pImpl->data_;
    std::vector<MaterialMove>& pending_moves = pImpl->pending_moves_;

    ScopeTimer timer(timers, "process_moves");

    // Counters for analysis.
    size_t num_moves = pending_moves.size();
    size_t num_swaps = 0;
    size_t num_swaps_from_transfers = 0;
    size_t num_swaps_from_collisions = 0;
    size_t num_transfers = 0;
    size_t num_elastic = 0;
    size_t num_inelastic = 0;

    // Shuffle moves to handle conflicts randomly.
    {
        ScopeTimer shuffleTimer(timers, "process_moves_shuffle");
        std::shuffle(pending_moves.begin(), pending_moves.end(), *rng_);
    }

    for (const auto& move : pending_moves) {
        Cell& fromCell = data.at(move.from.x, move.from.y);
        Cell& toCell = data.at(move.to.x, move.to.y);

        // Apply any pressure from excess that couldn't transfer.
        if (move.pressure_from_excess > 0.0) {
            if (toCell.material_type == Material::EnumType::Wall) {
                fromCell.pressure += move.pressure_from_excess;

                spdlog::debug(
                    "Wall blocked transfer: source cell({},{}) pressure increased by {:.3f}",
                    move.from.x,
                    move.from.y,
                    move.pressure_from_excess);
            }
            else {
                toCell.pressure += move.pressure_from_excess;

                spdlog::debug(
                    "Applied pressure from excess: cell({},{}) pressure increased by {:.3f}",
                    move.to.x,
                    move.to.y,
                    move.pressure_from_excess);
            }
        }

        // Check if materials should swap instead of colliding (if enabled).
        if (settings.swap_enabled && move.collision_type != CollisionType::TRANSFER_ONLY) {
            Vector2i direction(move.to.x - move.from.x, move.to.y - move.from.y);
            bool should_swap = collision_calc.shouldSwapMaterials(
                *this, move.from.x, move.from.y, fromCell, toCell, direction, move);

            if (should_swap) {
                num_swaps++;
                if (move.collision_type == CollisionType::TRANSFER_ONLY) {
                    num_swaps_from_transfers++;
                }
                else {
                    num_swaps_from_collisions++;
                }

                // Capture organism ownership BEFORE the swap.
                Vector2i from_pos{ static_cast<int>(move.from.x), static_cast<int>(move.from.y) };
                Vector2i to_pos{ static_cast<int>(move.to.x), static_cast<int>(move.to.y) };
                OrganismId from_org_id = organism_manager_->at(from_pos);
                OrganismId to_org_id = organism_manager_->at(to_pos);

                collision_calc.swapCounterMovingMaterials(fromCell, toCell, direction, move);

                // Update organism tracking (swap happened).
                organism_manager_->swapOrganisms(from_pos, to_pos);

                if (to_org_id != INVALID_ORGANISM_ID || from_org_id != INVALID_ORGANISM_ID) {
                    LOG_INFO(
                        Swap,
                        "Material swap: ({},{}) ↔ ({},{}) - organisms: {} ↔ {}",
                        from_pos.x,
                        from_pos.y,
                        to_pos.x,
                        to_pos.y,
                        from_org_id,
                        to_org_id);
                }

                continue;
            }
        }

        // Track organism_id before transfer (in case source cell becomes empty).
        Vector2i from_pos{ static_cast<int>(move.from.x), static_cast<int>(move.from.y) };
        OrganismId organism_id = organism_manager_->at(from_pos);

        // Determine effective collision type (may be overridden for organism cells).
        CollisionType effective_collision_type = move.collision_type;

        // Organism cells are all-or-nothing - no partial transfers allowed.
        // This prevents orphaned organism material from "leaking" into adjacent cells.
        // Guard 1: Move must be for the full cell (generation-time should enforce this too).
        // Guard 2: Target must still have room (might have filled since move was scheduled).
        if (organism_id != INVALID_ORGANISM_ID) {
            bool move_is_partial = move.amount < fromCell.fill_ratio - 0.001;
            bool target_cant_fit = toCell.getCapacity() < fromCell.fill_ratio;
            if (move_is_partial || target_cant_fit) {
                effective_collision_type = CollisionType::ELASTIC_REFLECTION;
            }
        }

        // Handle collision during the move based on collision_type.
        switch (effective_collision_type) {
            case CollisionType::TRANSFER_ONLY:
                num_transfers++;
                collision_calc.handleTransferMove(*this, fromCell, toCell, move);
                break;
            case CollisionType::ELASTIC_REFLECTION:
                num_elastic++;
                collision_calc.handleElasticCollision(fromCell, toCell, move);
                break;
            case CollisionType::INELASTIC_COLLISION:
                num_inelastic++;
                // Try water fragmentation first - if it handles the collision, skip normal
                // inelastic.
                if (!collision_calc.handleWaterFragmentation(
                        *this, fromCell, toCell, move, *rng_)) {
                    collision_calc.handleInelasticCollision(*this, fromCell, toCell, move);
                }
                break;
            case CollisionType::FRAGMENTATION:
                collision_calc.handleFragmentation(*this, fromCell, toCell, move);
                break;
            case CollisionType::ABSORPTION:
                collision_calc.handleAbsorption(*this, fromCell, toCell, move);
                break;
        }

        // Update organism tracking if material actually transferred.
        // Check: organism owned the source AND source is now empty (transfer succeeded).
        // Note: This applies to ALL collision types that can transfer material, not just
        // TRANSFER_ONLY. INELASTIC_COLLISION, FRAGMENTATION, and ABSORPTION can all empty
        // an organism's cell via transferToWithPhysics.
        if (organism_id != INVALID_ORGANISM_ID && fromCell.isEmpty()) {
            // Material fully transferred - update organism tracking.
            Vector2i to_pos{ static_cast<int>(move.to.x), static_cast<int>(move.to.y) };
            spdlog::info(
                "Organism tracking: organism {} moved ({},{}) → ({},{}) via {}",
                organism_id,
                from_pos.x,
                from_pos.y,
                to_pos.x,
                to_pos.y,
                static_cast<int>(move.collision_type));
            organism_manager_->moveOrganismCell(from_pos, to_pos, organism_id);
        }
    }

    // Log move statistics.
    spdlog::debug(
        "processMaterialMoves: {} total moves, {} swaps ({:.1f}% - {} from transfers, {} from "
        "collisions), {} transfers, {} elastic, {} inelastic",
        num_moves,
        num_swaps,
        num_moves > 0 ? (100.0 * num_swaps / num_moves) : 0.0,
        num_swaps_from_transfers,
        num_swaps_from_collisions,
        num_transfers,
        num_elastic,
        num_inelastic);

    pImpl->pending_moves_.clear();
}

void World::setupBoundaryWalls()
{
    spdlog::info("Setting up boundary walls for World");

    // Top and bottom walls.
    for (int x = 0; x < pImpl->data_.width; ++x) {
        pImpl->data_.at(x, 0).replaceMaterial(Material::EnumType::Wall, 1.0);
        pImpl->data_.at(x, pImpl->data_.height - 1).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    // Left and right walls.
    for (int y = 0; y < pImpl->data_.height; ++y) {
        pImpl->data_.at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0);
        pImpl->data_.at(pImpl->data_.width - 1, y).replaceMaterial(Material::EnumType::Wall, 1.0);
    }

    spdlog::info("Boundary walls setup complete");
}

// =================================================================.
// HELPER METHODS.
// =================================================================.

void World::pixelToCell(int pixelX, int pixelY, int& cellX, int& cellY) const
{
    // Convert pixel coordinates to cell coordinates.
    // Each cell is Cell::WIDTH x Cell::HEIGHT pixels.
    cellX = pixelX / Cell::WIDTH;
    cellY = pixelY / Cell::HEIGHT;
}

bool World::isValidCell(int x, int y) const
{
    return pImpl->data_.inBounds(x, y);
}

Vector2i World::pixelToCell(int pixelX, int pixelY) const
{
    return Vector2i(pixelX / Cell::WIDTH, pixelY / Cell::HEIGHT);
}

bool World::isValidCell(const Vector2i& pos) const
{
    return isValidCell(pos.x, pos.y);
}

// =================================================================.
// WORLD SETUP CONTROL METHODS.
// =================================================================.

// DEPRECATED: Wall management now handled by scenarios.
void World::setWallsEnabled(bool enabled)
{
    // Rebuild walls if needed.
    if (enabled) {
        setupBoundaryWalls();
    }
    else {
        // Clear existing walls by resetting boundary cells to air.
        for (int x = 0; x < pImpl->data_.width; ++x) {
            pImpl->data_.at(x, 0).clear();                       // Top wall.
            pImpl->data_.at(x, pImpl->data_.height - 1).clear(); // Bottom wall.
        }
        for (int y = 0; y < pImpl->data_.height; ++y) {
            pImpl->data_.at(0, y).clear();                      // Left wall.
            pImpl->data_.at(pImpl->data_.width - 1, y).clear(); // Right wall.
        }
    }
}

std::string World::settingsToString() const
{
    std::stringstream ss;
    ss << "=== World Settings ===\n";
    ss << "Grid size: " << pImpl->data_.width << "x" << pImpl->data_.height << "\n";
    ss << "Gravity: " << pImpl->physicsSettings_.gravity << "\n";
    ss << "Hydrostatic pressure enabled: "
       << (getPhysicsSettings().pressure_hydrostatic_strength > 0 ? "true" : "false") << "\n";
    ss << "Dynamic pressure enabled: "
       << (getPhysicsSettings().pressure_dynamic_strength > 0 ? "true" : "false") << "\n";
    ss << "Pressure scale: " << pImpl->physicsSettings_.pressure_scale << "\n";
    ss << "Elasticity factor: " << pImpl->physicsSettings_.elasticity << "\n";
    ss << "Add particles enabled: " << (pImpl->data_.add_particles_enabled ? "true" : "false")
       << "\n";
    // Note: Scenario-specific settings (quadrant, throws, rain) now in Scenario, not World.
    ss << "Cohesion COM force enabled: "
       << (pImpl->physicsSettings_.cohesion_strength > 0.0 ? "true" : "false") << "\n";
    ss << "Cohesion bind force enabled: " << (isCohesionBindForceEnabled() ? "true" : "false")
       << "\n";
    ss << "Adhesion enabled: "
       << (pImpl->physicsSettings_.adhesion_strength > 0.0 ? "true" : "false") << "\n";
    ss << "Air resistance enabled: " << (air_resistance_enabled_ ? "true" : "false") << "\n";
    ss << "Air resistance strength: " << air_resistance_strength_ << "\n";
    ss << "Material removal threshold: " << MIN_MATTER_THRESHOLD << "\n";
    return ss.str();
}

// DEPRECATED: WorldEventGenerator removed - scenarios now handle setup/tick directly.

// =================================================================
// JSON SERIALIZATION
// =================================================================

nlohmann::json World::toJSON() const
{
    // Automatic serialization via ReflectSerializer!
    return ReflectSerializer::to_json(pImpl->data_);
}

void World::fromJSON(const nlohmann::json& doc)
{
    // Automatic deserialization via ReflectSerializer!
    pImpl->data_ = ReflectSerializer::from_json<WorldData>(doc);
    spdlog::info("World deserialized: {}x{} grid", pImpl->data_.width, pImpl->data_.height);
}

// Stub implementations for WorldInterface methods.
void World::onPreResize(int16_t newWidth, int16_t newHeight)
{
    spdlog::debug(
        "World::onPreResize: {}x{} -> {}x{}",
        pImpl->data_.width,
        pImpl->data_.height,
        newWidth,
        newHeight);
}

bool World::shouldResize(int16_t newWidth, int16_t newHeight) const
{
    return pImpl->data_.width != newWidth || pImpl->data_.height != newHeight;
}

// ADL functions for MotionState JSON serialization.
void to_json(nlohmann::json& j, World::MotionState state)
{
    switch (state) {
        case World::MotionState::STATIC:
            j = "STATIC";
            break;
        case World::MotionState::FALLING:
            j = "FALLING";
            break;
        case World::MotionState::SLIDING:
            j = "SLIDING";
            break;
        case World::MotionState::TURBULENT:
            j = "TURBULENT";
            break;
    }
}

void from_json(const nlohmann::json& j, World::MotionState& state)
{
    std::string str = j.get<std::string>();
    if (str == "STATIC") {
        state = World::MotionState::STATIC;
    }
    else if (str == "FALLING") {
        state = World::MotionState::FALLING;
    }
    else if (str == "SLIDING") {
        state = World::MotionState::SLIDING;
    }
    else if (str == "TURBULENT") {
        state = World::MotionState::TURBULENT;
    }
    else {
        throw std::runtime_error("Unknown MotionState: " + str);
    }
}

void World::spawnMaterialBall(Material::EnumType material, Vector2s center)
{
    // Calculate radius as 15% of world width (diameter = 15% of width).
    float diameter = pImpl->data_.width * 0.15f;
    float radius = diameter / 2.0f;

    // Round up to ensure at least 1 cell for very small worlds.
    int16_t radiusInt = static_cast<int16_t>(std::ceil(radius));
    if (radiusInt < 1) {
        radiusInt = 1;
    }

    // Clamp center position to ensure ball fits within walls.
    // Walls occupy the outermost layer (x=0, x=width-1, y=0, y=height-1).
    // Valid interior range: [1, width-2] for x, [1, height-2] for y.
    int16_t minX = 1 + radiusInt;
    int16_t maxX = static_cast<int16_t>(pImpl->data_.width) >= 2 + radiusInt
        ? static_cast<int16_t>(pImpl->data_.width - 1 - radiusInt)
        : 1;
    int16_t minY = 1 + radiusInt;
    int16_t maxY = static_cast<int16_t>(pImpl->data_.height) >= 2 + radiusInt
        ? static_cast<int16_t>(pImpl->data_.height - 1 - radiusInt)
        : 1;

    // Clamp the provided center to valid range.
    int16_t clampedCenterX = std::max(minX, std::min(center.x, maxX));
    int16_t clampedCenterY = std::max(minY, std::min(center.y, maxY));

    // Only scan bounding box for efficiency.
    int16_t scanMinX = clampedCenterX > radiusInt ? clampedCenterX - radiusInt : 0;
    int16_t scanMaxX =
        std::min<int16_t>(clampedCenterX + radiusInt, static_cast<int16_t>(pImpl->data_.width - 1));
    int16_t scanMinY = clampedCenterY > radiusInt ? clampedCenterY - radiusInt : 0;
    int16_t scanMaxY = std::min<int16_t>(
        clampedCenterY + radiusInt, static_cast<int16_t>(pImpl->data_.height - 1));

    // Spawn a ball of material centered at the clamped position.
    for (int16_t y = scanMinY; y <= scanMaxY; ++y) {
        for (int16_t x = scanMinX; x <= scanMaxX; ++x) {
            // Calculate distance from center.
            int16_t dx = x - clampedCenterX;
            int16_t dy = y - clampedCenterY;
            float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));

            // If within radius, fill the cell.
            if (distance <= radius) {
                addMaterialAtCell({ x, y }, material, 1.0f);
            }
        }
    }
}

} // namespace DirtSim
