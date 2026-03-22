#include "World.h"
#include "core/LoggingChannels.h"

#include "Assert.h"
#include "Cell.h"
#include "GridOfCells.h"
#include "LightCalculatorBase.h"
#include "LightManager.h"
#include "LightPropagator.h"
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

#include "WorldPressureCalculator.h"
#include "WorldRegionActivityTracker.h"
#include "WorldRigidBodyCalculator.h"
#include "WorldStaticLoadCalculator.h"
#include "WorldVelocityLimitCalculator.h"
#include "WorldViscosityCalculator.h"
#include "organisms/OrganismManager.h"
#include "scenarios/Scenario.h"
#include "spdlog/spdlog.h"
#include "water/WaterSimSystem.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
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

int computeRegionBlockCount(int cells)
{
    return std::max(0, (cells + 7) / 8);
}

constexpr float kGranularCompressionCandidateCapacityEpsilon = 0.02f;
constexpr float kGranularCompressionCandidateFillRatioMinimum = 0.95f;
constexpr float kGranularCompressionCandidateLoadEpsilon = 0.001f;
constexpr float kFluidSupportCandidateCapacityEpsilon = 0.02f;
constexpr float kFluidSupportCandidateFillRatioMinimum = 0.95f;
constexpr float kBulkWaterVolumeEpsilon = 0.0001f;
constexpr float kGeneratedMoveZeroAmountEpsilon = 0.0001f;

bool shouldEnforceRegionSleep(
    const DirtSim::WorldRegionActivityTracker& tracker, const DirtSim::Cell& cell, int x, int y)
{
    if (cell.isEmpty() || cell.isWall()) {
        return false;
    }

    if (cell.material_type == DirtSim::Material::EnumType::Water) {
        return false;
    }

    return !tracker.isCellActive(x, y);
}

bool isLoadBearingGranularCell(const DirtSim::Cell& cell)
{
    if (cell.isEmpty()) {
        return false;
    }

    switch (cell.material_type) {
        case DirtSim::Material::EnumType::Dirt:
        case DirtSim::Material::EnumType::Sand:
            return true;
        case DirtSim::Material::EnumType::Air:
        case DirtSim::Material::EnumType::Leaf:
        case DirtSim::Material::EnumType::Metal:
        case DirtSim::Material::EnumType::Root:
        case DirtSim::Material::EnumType::Seed:
        case DirtSim::Material::EnumType::Wall:
        case DirtSim::Material::EnumType::Water:
        case DirtSim::Material::EnumType::Wood:
            return false;
    }

    return false;
}

bool isGranularSupportSinkMaterial(DirtSim::Material::EnumType material)
{
    switch (material) {
        case DirtSim::Material::EnumType::Metal:
        case DirtSim::Material::EnumType::Wall:
        case DirtSim::Material::EnumType::Wood:
            return true;
        case DirtSim::Material::EnumType::Air:
        case DirtSim::Material::EnumType::Dirt:
        case DirtSim::Material::EnumType::Leaf:
        case DirtSim::Material::EnumType::Root:
        case DirtSim::Material::EnumType::Sand:
        case DirtSim::Material::EnumType::Seed:
        case DirtSim::Material::EnumType::Water:
            return false;
    }

    return false;
}

bool isFluidSupportSinkMaterial(DirtSim::Material::EnumType material)
{
    switch (material) {
        case DirtSim::Material::EnumType::Metal:
        case DirtSim::Material::EnumType::Wall:
        case DirtSim::Material::EnumType::Wood:
            return true;
        case DirtSim::Material::EnumType::Air:
        case DirtSim::Material::EnumType::Dirt:
        case DirtSim::Material::EnumType::Leaf:
        case DirtSim::Material::EnumType::Root:
        case DirtSim::Material::EnumType::Sand:
        case DirtSim::Material::EnumType::Seed:
        case DirtSim::Material::EnumType::Water:
            return false;
    }

    return false;
}

bool isGranularSupportSinkCell(const DirtSim::Cell& cell)
{
    if (cell.isEmpty()) {
        return false;
    }

    return isGranularSupportSinkMaterial(cell.material_type);
}

bool isFluidSupportSinkCell(const DirtSim::Cell& cell)
{
    if (cell.isEmpty()) {
        return false;
    }

    return isFluidSupportSinkMaterial(cell.material_type);
}

bool isSupportedWaterCell(const DirtSim::Cell& cell)
{
    if (cell.isEmpty() || cell.material_type != DirtSim::Material::EnumType::Water) {
        return false;
    }

    if (cell.fill_ratio < kFluidSupportCandidateFillRatioMinimum) {
        return false;
    }

    return cell.getCapacity() <= kFluidSupportCandidateCapacityEpsilon;
}

bool hasBuriedFluidExposure(const DirtSim::WorldData& data, int x, int y)
{
    constexpr std::array<DirtSim::Vector2i, 4> directions{ {
        DirtSim::Vector2i{ -1, 0 },
        DirtSim::Vector2i{ 1, 0 },
        DirtSim::Vector2i{ 0, -1 },
        DirtSim::Vector2i{ 0, 1 },
    } };

    for (const DirtSim::Vector2i& direction : directions) {
        const int neighborX = x + direction.x;
        const int neighborY = y + direction.y;
        if (!data.inBounds(neighborX, neighborY)) {
            return true;
        }

        const DirtSim::Cell& neighbor = data.at(neighborX, neighborY);
        if (neighbor.isEmpty() || neighbor.material_type == DirtSim::Material::EnumType::Air) {
            return true;
        }
    }

    return false;
}

bool hasSupportedFluidPath(
    const DirtSim::WorldData& data,
    int x,
    int y,
    int supportOffsetY,
    std::vector<int8_t>& supportCache)
{
    if (!data.inBounds(x, y)) {
        return false;
    }

    const size_t cellIndex = static_cast<size_t>(y) * data.width + x;
    int8_t& cachedResult = supportCache[cellIndex];
    if (cachedResult >= 0) {
        return cachedResult != 0;
    }

    cachedResult = 0;

    const DirtSim::Cell& cell = data.at(x, y);
    if (!isSupportedWaterCell(cell)) {
        return false;
    }

    const int supportY = y + supportOffsetY;
    if (!data.inBounds(x, supportY)) {
        return false;
    }

    const DirtSim::Cell& directSupport = data.at(x, supportY);
    if (isFluidSupportSinkCell(directSupport)) {
        cachedResult = 1;
        return true;
    }

    if (!isSupportedWaterCell(directSupport)) {
        return false;
    }

    if (hasSupportedFluidPath(data, x, supportY, supportOffsetY, supportCache)) {
        cachedResult = 1;
        return true;
    }

    return false;
}

bool hasBlockedFluidNormalSupport(const DirtSim::WorldData& data, int x, int y, int supportOffsetY)
{
    const int supportY = y + supportOffsetY;
    if (!data.inBounds(x, supportY)) {
        return true;
    }

    const DirtSim::Cell& directSupport = data.at(x, supportY);
    if (isFluidSupportSinkCell(directSupport)) {
        return true;
    }

    return isSupportedWaterCell(directSupport);
}

bool hasSupportedGranularPath(
    const DirtSim::WorldData& data,
    int x,
    int y,
    int supportOffsetY,
    std::vector<int8_t>& supportCache)
{
    if (!data.inBounds(x, y)) {
        return false;
    }

    const size_t cellIndex = static_cast<size_t>(y) * data.width + x;
    int8_t& cachedResult = supportCache[cellIndex];
    if (cachedResult >= 0) {
        return cachedResult != 0;
    }

    cachedResult = 0;

    const DirtSim::Cell& cell = data.at(x, y);
    if (!isLoadBearingGranularCell(cell)) {
        return false;
    }

    const int supportY = y + supportOffsetY;
    if (!data.inBounds(x, supportY)) {
        return false;
    }

    const DirtSim::Cell& directSupport = data.at(x, supportY);
    if (isGranularSupportSinkCell(directSupport)) {
        cachedResult = 1;
        return true;
    }

    if (isLoadBearingGranularCell(directSupport)
        && hasSupportedGranularPath(data, x, supportY, supportOffsetY, supportCache)) {
        cachedResult = 1;
        return true;
    }

    for (const int diagonalX : { x - 1, x + 1 }) {
        if (!data.inBounds(diagonalX, supportY)) {
            continue;
        }

        const DirtSim::Cell& diagonalSupport = data.at(diagonalX, supportY);
        if (!isLoadBearingGranularCell(diagonalSupport)) {
            continue;
        }

        if (hasSupportedGranularPath(data, diagonalX, supportY, supportOffsetY, supportCache)) {
            cachedResult = 1;
            return true;
        }
    }

    return false;
}

uint8_t directionToMask(const DirtSim::Vector2i& direction)
{
    if (direction.x < 0) {
        return DirtSim::CellDebug::DirectionLeft;
    }
    if (direction.x > 0) {
        return DirtSim::CellDebug::DirectionRight;
    }
    if (direction.y < 0) {
        return DirtSim::CellDebug::DirectionUp;
    }
    if (direction.y > 0) {
        return DirtSim::CellDebug::DirectionDown;
    }

    return DirtSim::CellDebug::DirectionNone;
}

bool carriesTransmittedGranularLoad(const DirtSim::Cell& cell, float gravityMagnitude)
{
    if (gravityMagnitude <= 0.0001f || !isLoadBearingGranularCell(cell)) {
        return false;
    }

    const float selfWeight = cell.getMass() * gravityMagnitude;
    return cell.static_load > selfWeight + kGranularCompressionCandidateLoadEpsilon;
}

bool isSupportedGranularCompressionTarget(
    const DirtSim::WorldData& data,
    int x,
    int y,
    int supportOffsetY,
    float gravityMagnitude,
    std::vector<int8_t>& supportCache)
{
    if (!data.inBounds(x, y)) {
        return false;
    }

    const DirtSim::Cell& cell = data.at(x, y);
    if (cell.isEmpty()) {
        return false;
    }

    if (isGranularSupportSinkCell(cell)) {
        return true;
    }

    if (!isLoadBearingGranularCell(cell)) {
        return false;
    }

    if (cell.fill_ratio < kGranularCompressionCandidateFillRatioMinimum) {
        return false;
    }

    if (cell.getCapacity() > kGranularCompressionCandidateCapacityEpsilon) {
        return false;
    }

    if (!carriesTransmittedGranularLoad(cell, gravityMagnitude)) {
        return false;
    }

    return hasSupportedGranularPath(data, x, y, supportOffsetY, supportCache);
}

bool isSupportedGranularCompressionCandidate(
    const DirtSim::WorldData& data,
    const DirtSim::Cell& fromCell,
    const DirtSim::Vector2i& fromPos,
    const DirtSim::Vector2i& direction,
    float gravityMagnitude,
    int supportOffsetY,
    std::vector<int8_t>& supportCache,
    bool requireGravityAlignment)
{
    if (gravityMagnitude <= 0.0001f) {
        return false;
    }

    if (requireGravityAlignment && (direction.x != 0 || direction.y != supportOffsetY)) {
        return false;
    }

    if (!isLoadBearingGranularCell(fromCell)) {
        return false;
    }

    if (fromCell.fill_ratio < kGranularCompressionCandidateFillRatioMinimum) {
        return false;
    }

    const bool hasSupportPath =
        hasSupportedGranularPath(data, fromPos.x, fromPos.y, supportOffsetY, supportCache);
    if (!hasSupportPath) {
        return false;
    }

    const bool carriesTransmittedLoad = carriesTransmittedGranularLoad(fromCell, gravityMagnitude);
    if (!carriesTransmittedLoad && !requireGravityAlignment) {
        return false;
    }

    const double inwardNormalSpeed = fromCell.velocity.dot(
        DirtSim::Vector2d{
            static_cast<double>(direction.x),
            static_cast<double>(direction.y),
        });
    if (inwardNormalSpeed <= 0.0) {
        return false;
    }

    const DirtSim::Vector2i targetPos = fromPos + direction;
    if (!data.inBounds(targetPos.x, targetPos.y)) {
        return false;
    }

    const DirtSim::Cell& toCell = data.at(targetPos.x, targetPos.y);
    if (toCell.isEmpty()) {
        return false;
    }

    if (toCell.getCapacity() > kGranularCompressionCandidateCapacityEpsilon) {
        return false;
    }

    return isSupportedGranularCompressionTarget(
        data, targetPos.x, targetPos.y, supportOffsetY, gravityMagnitude, supportCache);
}

void incrementSaturating(uint16_t& value)
{
    if (value != std::numeric_limits<uint16_t>::max()) {
        value++;
    }
}

void noteGeneratedMoveClassification(
    DirtSim::CellDebug& debug, const DirtSim::MaterialMove& move, const DirtSim::Cell& toCell)
{
    const DirtSim::Vector2i direction{
        static_cast<int>(move.to.x - move.from.x),
        static_cast<int>(move.to.y - move.from.y),
    };
    if (direction.y <= 0) {
        return;
    }

    incrementSaturating(debug.downward_generated_move_count);

    if (move.amount <= kGeneratedMoveZeroAmountEpsilon) {
        incrementSaturating(debug.downward_zero_amount_move_count);
    }

    if (toCell.material_type == DirtSim::Material::EnumType::Air) {
        incrementSaturating(debug.downward_air_target_count);
    }
    if (toCell.material_type == move.material) {
        incrementSaturating(debug.downward_same_material_target_count);
    }
    if (toCell.material_type == DirtSim::Material::EnumType::Wall) {
        incrementSaturating(debug.downward_wall_target_count);
    }

    switch (move.collision_type) {
        case DirtSim::CollisionType::TRANSFER_ONLY:
            incrementSaturating(debug.downward_transfer_only_count);
            break;
        case DirtSim::CollisionType::FLUID_BLOCKED_CONTACT:
            incrementSaturating(debug.downward_fluid_blocked_contact_count);
            break;
        case DirtSim::CollisionType::ELASTIC_REFLECTION:
            incrementSaturating(debug.downward_elastic_collision_count);
            break;
        case DirtSim::CollisionType::INELASTIC_COLLISION:
            incrementSaturating(debug.downward_inelastic_collision_count);
            break;
        case DirtSim::CollisionType::ABSORPTION:
            incrementSaturating(debug.downward_absorption_count);
            break;
        case DirtSim::CollisionType::COMPRESSION_CONTACT:
        case DirtSim::CollisionType::FRAGMENTATION:
            break;
    }
}

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
    bool is_grid_cache_dirty_ = false;
    bool is_static_load_dirty_ = true;
    double static_load_gravity_ = std::numeric_limits<double>::quiet_NaN();

    // Calculators. WorldFrictionCalculator is constructed locally with GridOfCells reference.
    WorldAdhesionCalculator adhesion_calculator_;
    WorldCollisionCalculator collision_calculator_;
    WorldPressureCalculator pressure_calculator_;
    WorldRegionActivityTracker region_activity_tracker_;
    std::vector<uint8_t> sleep_force_processing_skipped_;
    std::vector<uint8_t> sleep_move_generation_skipped_;
    WorldStaticLoadCalculator static_load_calculator_;
    WorldViscosityCalculator viscosity_calculator_;
    WaterSimSystem water_sim_system_;
    std::vector<float> mac_water_surface_scratch_;

    // Light calculator (unique_ptr for runtime swappability).
    std::unique_ptr<LightCalculatorBase> light_calculator_;

    // Material transfer queue (internal simulation state).
    std::vector<MaterialMove> pending_moves_;

    // Light sources.
    LightManager light_manager_;

    // Performance timing.
    mutable Timers timers_;

    // Constructor.
    Impl()
        : physicsSettings_(getDefaultPhysicsSettings()),
          light_calculator_(std::make_unique<LightPropagator>())
    {
        timers_.startTimer("total_simulation");
    }

    // Destructor.
    ~Impl() { timers_.stopTimer("total_simulation"); }
};

void exportRegionDebugInfo(World::Impl& impl)
{
    impl.data_.region_debug_blocks_x =
        static_cast<int16_t>(impl.region_activity_tracker_.getBlocksX());
    impl.data_.region_debug_blocks_y =
        static_cast<int16_t>(impl.region_activity_tracker_.getBlocksY());
    impl.region_activity_tracker_.populateDebugInfo(impl.data_.region_debug);
}

void resizeRegionDebugTracking(World::Impl& impl, int world_width, int world_height)
{
    const int blocks_x = computeRegionBlockCount(world_width);
    const int blocks_y = computeRegionBlockCount(world_height);
    impl.region_activity_tracker_.resize(world_width, world_height, blocks_x, blocks_y);
    exportRegionDebugInfo(impl);
}

void resetSleepEnforcementDebugTracking(World::Impl& impl)
{
    std::fill(
        impl.sleep_force_processing_skipped_.begin(),
        impl.sleep_force_processing_skipped_.end(),
        0);
    std::fill(
        impl.sleep_move_generation_skipped_.begin(), impl.sleep_move_generation_skipped_.end(), 0);
}

void resizeSleepEnforcementDebugTracking(World::Impl& impl, int world_width, int world_height)
{
    const size_t cell_count = static_cast<size_t>(world_width) * world_height;
    impl.sleep_force_processing_skipped_.assign(cell_count, 0);
    impl.sleep_move_generation_skipped_.assign(cell_count, 0);
}

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
    pImpl->light_calculator_->resize(width, height);

    // Initialize with empty air.
    for (auto& cell : pImpl->data_.cells) {
        cell = Cell{ Material::EnumType::Air, 0.0 };
    }

    // Note: Boundary walls are now set up by Scenarios in their setup() method.
    // Each scenario controls whether it wants walls or not.

    // Initialize persistent GridOfCells for debug info and caching.
    pImpl->grid_.emplace(
        pImpl->data_.cells, pImpl->data_.debug_info, pImpl->data_.width, pImpl->data_.height);
    resizeRegionDebugTracking(*pImpl, pImpl->data_.width, pImpl->data_.height);
    resizeSleepEnforcementDebugTracking(*pImpl, pImpl->data_.width, pImpl->data_.height);
    // Force refresh on first simulation step after scenario setup mutates world data.
    pImpl->is_grid_cache_dirty_ = true;

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

LightCalculatorBase& World::getLightCalculator()
{
    return *pImpl->light_calculator_;
}

const LightCalculatorBase& World::getLightCalculator() const
{
    return *pImpl->light_calculator_;
}

const LightBuffer& World::getRawLightBuffer() const
{
    return pImpl->light_calculator_->getRawLightBuffer();
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
    ensureGridCacheFresh("grid_cache_rebuild_on_access");
    return *pImpl->grid_;
}

const GridOfCells& World::getGrid() const
{
    const_cast<World*>(this)->ensureGridCacheFresh("grid_cache_rebuild_on_access");
    return *pImpl->grid_;
}

const WorldRegionActivityTracker& World::getRegionActivityTracker() const
{
    return pImpl->region_activity_tracker_;
}

bool World::wasSleepForceProcessingSkippedAtCell(int x, int y) const
{
    if (!pImpl->data_.inBounds(x, y)) {
        return false;
    }

    const size_t idx = static_cast<size_t>(y) * pImpl->data_.width + x;
    return idx < pImpl->sleep_force_processing_skipped_.size()
        && pImpl->sleep_force_processing_skipped_[idx] != 0;
}

bool World::wasSleepMoveGenerationSkippedAtCell(int x, int y) const
{
    if (!pImpl->data_.inBounds(x, y)) {
        return false;
    }

    const size_t idx = static_cast<size_t>(y) * pImpl->data_.width + x;
    return idx < pImpl->sleep_move_generation_skipped_.size()
        && pImpl->sleep_move_generation_skipped_[idx] != 0;
}

void World::ensureGridCacheFresh(const char* timerName)
{
    if (!pImpl->is_grid_cache_dirty_) {
        return;
    }

    rebuildGridCache(timerName);
}

void World::rebuildGridCache(const char* timerName)
{
    ScopeTimer timer(pImpl->timers_, timerName);
    pImpl->grid_.emplace(
        pImpl->data_.cells, pImpl->data_.debug_info, pImpl->data_.width, pImpl->data_.height);
    pImpl->is_grid_cache_dirty_ = false;
}

void World::markGridCacheDirty()
{
    pImpl->is_grid_cache_dirty_ = true;
    pImpl->is_static_load_dirty_ = true;
}

bool World::isStaticLoadRecomputeNeeded() const
{
    return pImpl->is_static_load_dirty_
        || pImpl->static_load_gravity_ != pImpl->physicsSettings_.gravity;
}

void World::recomputeStaticLoad(const char* timerName)
{
    ScopeTimer timer(pImpl->timers_, timerName);
    pImpl->static_load_calculator_.recomputeAll(*this);
    pImpl->is_static_load_dirty_ = false;
    pImpl->static_load_gravity_ = pImpl->physicsSettings_.gravity;
}

PhysicsSettings& World::getPhysicsSettings()
{
    return pImpl->physicsSettings_;
}

const PhysicsSettings& World::getPhysicsSettings() const
{
    return pImpl->physicsSettings_;
}

void World::addBulkWaterAtCell(Vector2s pos, float amount)
{
    if (amount <= 0.0f) {
        return;
    }

    setBulkWaterAmountAtCell(pos, getBulkWaterAmountAtCell(pos) + amount);
}

void World::addBulkWaterAtCell(int x, int y, float amount)
{
    addBulkWaterAtCell(Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(y) }, amount);
}

void World::clearAllBulkWater()
{
    const int width = pImpl->data_.width;
    const int height = pImpl->data_.height;
    bool cellChanged = false;

    if (pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        pImpl->water_sim_system_.syncToSettings(pImpl->physicsSettings_, width, height);

        WaterVolumeMutableView volumeMutable{};
        if (pImpl->water_sim_system_.tryGetMutableWaterVolumeView(volumeMutable)
            && volumeMutable.width == width && volumeMutable.height == height) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t idx = static_cast<size_t>(y) * width + x;
                    if (volumeMutable.volume[idx] <= kBulkWaterVolumeEpsilon) {
                        continue;
                    }

                    volumeMutable.volume[idx] = 0.0f;
                    pImpl->region_activity_tracker_.noteWakeAtCell(
                        x, y, WakeReason::ExternalMutation);
                }
            }
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell = pImpl->data_.at(x, y);
            if (cell.material_type != Material::EnumType::Water) {
                continue;
            }

            cell.clear();
            cellChanged = true;
            pImpl->region_activity_tracker_.noteWakeAtCell(x, y, WakeReason::ExternalMutation);
        }
    }

    if (cellChanged) {
        markGridCacheDirty();
    }
}

float World::getBulkWaterAmountAtCell(Vector2s pos) const
{
    return getBulkWaterAmountAtCell(pos.x, pos.y);
}

float World::getBulkWaterAmountAtCell(int x, int y) const
{
    if (!pImpl->data_.inBounds(x, y)) {
        return 0.0f;
    }

    if (pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        WaterVolumeView waterVolume{};
        if (pImpl->water_sim_system_.tryGetWaterVolumeView(waterVolume)
            && waterVolume.width == pImpl->data_.width
            && waterVolume.height == pImpl->data_.height) {
            const size_t idx = static_cast<size_t>(y) * pImpl->data_.width + x;
            if (idx < waterVolume.volume.size()) {
                const float amount = std::clamp(waterVolume.volume[idx], 0.0f, 1.0f);
                if (amount > kBulkWaterVolumeEpsilon) {
                    return amount;
                }
            }
        }
    }

    const Cell& cell = pImpl->data_.at(x, y);
    if (cell.material_type != Material::EnumType::Water) {
        return 0.0f;
    }

    return std::clamp(cell.fill_ratio, 0.0f, 1.0f);
}

bool World::hasBulkWaterAtCell(Vector2s pos, float minAmount) const
{
    return hasBulkWaterAtCell(pos.x, pos.y, minAmount);
}

bool World::hasBulkWaterAtCell(int x, int y, float minAmount) const
{
    if (!pImpl->data_.inBounds(x, y)) {
        return false;
    }

    if (pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        WaterVolumeView waterVolume{};
        if (pImpl->water_sim_system_.tryGetWaterVolumeView(waterVolume)
            && waterVolume.width == pImpl->data_.width
            && waterVolume.height == pImpl->data_.height) {
            const size_t idx = static_cast<size_t>(y) * pImpl->data_.width + x;
            if (idx < waterVolume.volume.size()) {
                const float amount = std::clamp(waterVolume.volume[idx], 0.0f, 1.0f);
                return amount > minAmount;
            }
        }
    }

    return getBulkWaterAmountAtCell(x, y) > minAmount;
}

void World::queueGuidedWaterDrain(const GuidedWaterDrain& drain)
{
    if (pImpl->physicsSettings_.water_sim_mode != WaterSimMode::MacProjection) {
        return;
    }

    pImpl->water_sim_system_.syncToSettings(
        pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);
    pImpl->water_sim_system_.queueGuidedWaterDrain(drain);

    const auto noteWakeSpan = [&](int startX, int endX, int y) {
        if (y < 0 || y >= pImpl->data_.height) {
            return;
        }

        const int clampedStartX = std::max(0, startX);
        const int clampedEndX = std::min(pImpl->data_.width - 1, endX);
        if (clampedStartX > clampedEndX) {
            return;
        }

        for (int x = clampedStartX; x <= clampedEndX; ++x) {
            pImpl->region_activity_tracker_.noteWakeAtCell(x, y, WakeReason::ExternalMutation);
        }
    };

    if (drain.guideDownwardSpeed > 0.0f || drain.guideLateralSpeed > 0.0f) {
        const int topY = std::min(drain.guideTopY, drain.guideBottomY);
        const int bottomY = std::max(drain.guideTopY, drain.guideBottomY);
        for (int y = topY; y <= bottomY; ++y) {
            noteWakeSpan(drain.guideStartX, drain.guideEndX, y);
        }
    }

    if (drain.mouthDownwardSpeed > 0.0f || drain.drainRatePerSecond > 0.0f) {
        noteWakeSpan(drain.mouthStartX, drain.mouthEndX, drain.mouthY);
    }
}

void World::setBulkWaterAmountAtCell(Vector2s pos, float amount)
{
    if (!isValidCell(pos)) {
        return;
    }

    if (organism_manager_->at(pos) != INVALID_ORGANISM_ID) {
        return;
    }

    amount = std::clamp(amount, 0.0f, 1.0f);
    bool changed = false;
    bool cellChanged = false;
    Cell& cell = pImpl->data_.at(pos.x, pos.y);

    if (pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        pImpl->water_sim_system_.syncToSettings(
            pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);

        WaterVolumeMutableView volumeMutable{};
        if (pImpl->water_sim_system_.tryGetMutableWaterVolumeView(volumeMutable)
            && volumeMutable.width == pImpl->data_.width
            && volumeMutable.height == pImpl->data_.height) {
            const bool blockedBySolid = amount > kBulkWaterVolumeEpsilon && !cell.isEmpty()
                && cell.material_type != Material::EnumType::Air
                && cell.material_type != Material::EnumType::Water;
            if (blockedBySolid) {
                return;
            }

            const size_t idx = static_cast<size_t>(pos.y) * pImpl->data_.width + pos.x;
            if (idx < volumeMutable.volume.size()) {
                const float current = std::clamp(volumeMutable.volume[idx], 0.0f, 1.0f);
                if (std::abs(current - amount) > kBulkWaterVolumeEpsilon) {
                    volumeMutable.volume[idx] = amount;
                    changed = true;
                }
            }

            if (cell.material_type == Material::EnumType::Water) {
                cell.clear();
                cellChanged = true;
                changed = true;
            }
        }
    }
    else {
        if (amount <= kBulkWaterVolumeEpsilon) {
            if (cell.material_type == Material::EnumType::Water) {
                cell.clear();
                cellChanged = true;
                changed = true;
            }
        }
        else {
            const bool blockedBySolid = !cell.isEmpty()
                && cell.material_type != Material::EnumType::Air
                && cell.material_type != Material::EnumType::Water;
            if (blockedBySolid) {
                return;
            }

            if (cell.material_type != Material::EnumType::Water
                || std::abs(cell.fill_ratio - amount) > kBulkWaterVolumeEpsilon) {
                cell.replaceMaterial(Material::EnumType::Water, amount);
                cellChanged = true;
                changed = true;
            }
        }
    }

    if (!changed) {
        return;
    }

    if (cellChanged) {
        markGridCacheDirty();
    }

    pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
}

void World::setBulkWaterAmountAtCell(int x, int y, float amount)
{
    setBulkWaterAmountAtCell(Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(y) }, amount);
}

bool World::tryGetWaterVolumeView(WaterVolumeView& out) const
{
    return pImpl->water_sim_system_.tryGetWaterVolumeView(out);
}

bool World::tryGetWaterActivityView(WaterActivityView& out) const
{
    return pImpl->water_sim_system_.tryGetWaterActivityView(out);
}

bool World::tryGetMutableWaterVolumeView(WaterVolumeMutableView& out)
{
    return pImpl->water_sim_system_.tryGetMutableWaterVolumeView(out);
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

    pImpl->water_sim_system_.syncToSettings(
        pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);
    pImpl->water_sim_system_.advanceTime(*this, scaledDeltaTime);

    pImpl->light_calculator_->clearAllEmissive();

    // Rebuild grid cache only when external or prior-frame mutations invalidated it.
    ensureGridCacheFresh("grid_cache_rebuild");
    GridOfCells& grid = *pImpl->grid_;

    for (const auto& blocked_transfer : pImpl->pressure_calculator_.blocked_transfers_) {
        pImpl->region_activity_tracker_.noteBlockedTransfer(
            blocked_transfer.fromX, blocked_transfer.fromY);
        pImpl->region_activity_tracker_.noteWakeAtCell(
            blocked_transfer.toX, blocked_transfer.toY, WakeReason::BlockedTransfer);
    }
    pImpl->region_activity_tracker_.beginFrame(
        *this, grid, static_cast<uint32_t>(pImpl->data_.timestep));
    resetSleepEnforcementDebugTracking(*pImpl);

    for (CellDebug& debug : pImpl->data_.debug_info) {
        debug.dynamic_pressure_reflection_injection_amount = 0.0f;
        debug.dynamic_pressure_reflection_injection_count = 0;
        debug.dynamic_pressure_target_injection_amount = 0.0f;
        debug.dynamic_pressure_target_injection_count = 0;
        debug.excess_move_pressure_injection_amount = 0.0f;
        debug.excess_move_pressure_injection_count = 0;
        debug.hydrostatic_pressure_injection_amount = 0.0f;
        debug.hydrostatic_pressure_injection_count = 0;
    }

    pImpl->pressure_calculator_.beginPressureFrame(*this);

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

    pImpl->pressure_calculator_.finalizePressureFrame(*this);

    // Update organisms before force accumulation so new cells participate in physics.
    {
        ScopeTimer organismTimer(pImpl->timers_, "organisms");
        organism_manager_->update(*this, scaledDeltaTime);
    }

    if (isStaticLoadRecomputeNeeded()) {
        recomputeStaticLoad("static_load_preforce_recompute");
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

    // Snapshot duck velocities before collision resolution (material moves).
    organism_manager_->snapshotPreCollisionState(*this);

    {
        ScopeTimer transfersTimer(pImpl->timers_, "update_transfers");
        pImpl->pending_moves_ = computeMaterialMoves(scaledDeltaTime);
    }

    // Inject organism emissions and calculate lighting before material moves.
    // Lighting is cosmetic and doesn't feed back into physics, so it can use the
    // already-fresh grid cache from the start of the frame.
    organism_manager_->injectEmissions(*pImpl->light_calculator_);
    {
        pImpl->light_calculator_->calculate(
            *this, *pImpl->grid_, pImpl->physicsSettings_.light, pImpl->timers_);
    }

    // Process material moves - detects collisions for next frame's dynamic pressure.
    processMaterialMoves();

    if (isStaticLoadRecomputeNeeded()) {
        recomputeStaticLoad("static_load_postmove_recompute");
    }

    ensureGridCacheFresh("region_activity_grid_cache");
    pImpl->region_activity_tracker_.summarizeFrame(
        *this, *pImpl->grid_, static_cast<uint32_t>(pImpl->data_.timestep));
    exportRegionDebugInfo(*pImpl);

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

    if (type == Material::EnumType::Water
        && pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        pImpl->water_sim_system_.syncToSettings(
            pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);

        WaterVolumeMutableView volumeMutable{};
        if (!pImpl->water_sim_system_.tryGetMutableWaterVolumeView(volumeMutable)) {
            return;
        }

        const Cell& cell = pImpl->data_.at(pos.x, pos.y);
        const bool isBlockedBySolid =
            !cell.isEmpty() && cell.material_type != Material::EnumType::Air;
        if (isBlockedBySolid) {
            return;
        }

        const size_t idx = static_cast<size_t>(pos.y) * pImpl->data_.width + pos.x;
        const float current = volumeMutable.volume[idx];
        const float added = std::min(amount, std::max(0.0f, 1.0f - current));
        if (added <= 0.0f) {
            return;
        }

        volumeMutable.volume[idx] = current + added;
        pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
        spdlog::trace("Added {:.3f} {} (mac) at cell ({},{})", added, toString(type), pos.x, pos.y);
        return;
    }

    Cell& cell = pImpl->data_.at(pos.x, pos.y);
    const float added = cell.addMaterial(type, amount);

    if (added > 0.0f) {
        markGridCacheDirty();
        pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
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
    markGridCacheDirty();
    pImpl->region_activity_tracker_.noteWakeAtCell(pos1.x, pos1.y, WakeReason::ExternalMutation);
    pImpl->region_activity_tracker_.noteWakeAtCell(pos2.x, pos2.y, WakeReason::ExternalMutation);

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

    if (material == Material::EnumType::Water
        && pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        if (organism_manager_->at(pos) != INVALID_ORGANISM_ID) {
            return;
        }

        pImpl->water_sim_system_.syncToSettings(
            pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);

        WaterVolumeMutableView volumeMutable{};
        if (!pImpl->water_sim_system_.tryGetMutableWaterVolumeView(volumeMutable)) {
            return;
        }

        const Cell& cell = pImpl->data_.at(pos.x, pos.y);
        const bool isBlockedBySolid =
            !cell.isEmpty() && cell.material_type != Material::EnumType::Air;
        if (isBlockedBySolid) {
            return;
        }

        const size_t idx = static_cast<size_t>(pos.y) * pImpl->data_.width + pos.x;
        if (idx >= volumeMutable.volume.size()) {
            return;
        }

        volumeMutable.volume[idx] = 1.0f;
        pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
        spdlog::trace("Replaced with {} (mac) at cell ({},{})", toString(material), pos.x, pos.y);
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
        markGridCacheDirty();
        pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
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
        markGridCacheDirty();
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
    markGridCacheDirty();
    pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
    pImpl->region_activity_tracker_.noteWakeAtCell(
        empty_pos.x, empty_pos.y, WakeReason::ExternalMutation);
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

    bool changed = false;
    if (pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection) {
        pImpl->water_sim_system_.syncToSettings(
            pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);

        WaterVolumeMutableView volumeMutable{};
        if (pImpl->water_sim_system_.tryGetMutableWaterVolumeView(volumeMutable)) {
            const size_t idx = static_cast<size_t>(pos.y) * pImpl->data_.width + pos.x;
            if (idx < volumeMutable.volume.size() && volumeMutable.volume[idx] > 0.0f) {
                volumeMutable.volume[idx] = 0.0f;
                changed = true;
            }
        }
    }

    Cell& cell = pImpl->data_.at(pos.x, pos.y);
    if (cell.isEmpty()) {
        if (!changed) {
            return;
        }
        pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
        return;
    }

    cell.clear();
    markGridCacheDirty();
    changed = true;
    pImpl->region_activity_tracker_.noteWakeAtCell(pos.x, pos.y, WakeReason::ExternalMutation);
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
    resizeRegionDebugTracking(*pImpl, newWidth, newHeight);
    resizeSleepEnforcementDebugTracking(*pImpl, newWidth, newHeight);

    // Resize light calculator emissive overlay to match new dimensions.
    pImpl->light_calculator_->resize(newWidth, newHeight);

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

    markGridCacheDirty();
    spdlog::info("World bilinear resize complete");
}

// =================================================================.
// INTERNAL PHYSICS METHODS.
// =================================================================.

void World::applyGravity()
{
    // Cache pImpl members as local references.
    WorldData& data = pImpl->data_;
    std::vector<CellDebug>& debug_info = data.debug_info;
    const PhysicsSettings& settings = pImpl->physicsSettings_;
    const WorldRegionActivityTracker& region_activity_tracker = pImpl->region_activity_tracker_;
    const double gravity = settings.gravity;
    const float gravityMagnitude = static_cast<float>(std::abs(gravity));

    std::vector<int8_t> granularSupportCache(data.cells.size(), -1);
    std::vector<int8_t> fluidSupportCache(data.cells.size(), -1);

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            Cell& cell = data.at(x, y);
            CellDebug& debug = debug_info[idx];
            debug.accumulated_gravity_force = {};
            debug.carries_transmitted_granular_load = false;
            debug.gravity_skipped_for_support = false;
            debug.has_granular_support_path = false;

            if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                pImpl->sleep_force_processing_skipped_[idx] = 1;
                continue;
            }

            if (!cell.isEmpty() && !cell.isWall()) {
                if (gravityMagnitude > 0.0001f && isLoadBearingGranularCell(cell)) {
                    const float selfWeight = cell.getMass() * gravityMagnitude;
                    const bool carriesTransmittedLoad = cell.static_load > selfWeight + 0.001f;
                    const int supportOffsetY = gravity > 0.0 ? 1 : -1;
                    const bool hasSupportPath =
                        hasSupportedGranularPath(data, x, y, supportOffsetY, granularSupportCache);

                    debug.carries_transmitted_granular_load = carriesTransmittedLoad;
                    debug.has_granular_support_path = hasSupportPath;

                    if (carriesTransmittedLoad && hasSupportPath) {
                        debug.gravity_skipped_for_support = true;
                        continue;
                    }
                }

                if (settings.water_sim_mode == WaterSimMode::MacProjection
                    && gravityMagnitude > 0.0001f
                    && cell.material_type == Material::EnumType::Water) {
                    const int supportOffsetY = gravity > 0.0 ? 1 : -1;
                    const bool hasSupportPath =
                        hasSupportedFluidPath(data, x, y, supportOffsetY, fluidSupportCache);
                    const bool isBuriedWater = !hasBuriedFluidExposure(data, x, y);
                    const bool hasBlockedNormalSupport =
                        hasBlockedFluidNormalSupport(data, x, y, supportOffsetY);
                    const float normalVelocity =
                        supportOffsetY > 0 ? cell.velocity.y : -cell.velocity.y;
                    const bool isRestingSurfaceWater = hasBlockedNormalSupport
                        && std::abs(cell.velocity.x) < 0.2f && std::abs(normalVelocity) < 0.2f;

                    if (hasSupportPath && (isBuriedWater || isRestingSurfaceWater)) {
                        debug.gravity_skipped_for_support = true;
                        continue;
                    }
                }

                // Gravity force is proportional to material density (F = m × g).
                // This enables buoyancy: denser materials sink, lighter materials float.
                const Material::Properties& props = Material::getProperties(cell.material_type);
                Vector2d gravityForce(0.0, props.density * gravity);

                // Accumulate gravity force instead of applying directly.
                cell.addPendingForce(gravityForce);

                // Debug tracking.
                debug.accumulated_gravity_force = gravityForce;
            }
        }
    }
}

void World::applyMacWaterCouplingForces()
{
    const PhysicsSettings& settings = pImpl->physicsSettings_;
    if (settings.water_sim_mode != WaterSimMode::MacProjection) {
        return;
    }

    const double buoyancyStrength = settings.mac_water_buoyancy_strength;
    const double dragRate = settings.mac_water_drag_rate;
    if (buoyancyStrength <= 0.0 && dragRate <= 0.0) {
        return;
    }

    WaterVolumeView waterView{};
    if (!tryGetWaterVolumeView(waterView)) {
        return;
    }

    WorldData& data = pImpl->data_;
    if (waterView.width != data.width || waterView.height != data.height) {
        return;
    }

    constexpr float kVolumeEpsilon = 0.0001f;
    const int width = data.width;
    const int height = data.height;
    const size_t cellCount = static_cast<size_t>(width) * height;

    auto& surfaceScratch = pImpl->mac_water_surface_scratch_;
    if (surfaceScratch.size() != cellCount) {
        surfaceScratch.resize(cellCount);
    }
    std::fill(
        surfaceScratch.begin(), surfaceScratch.end(), std::numeric_limits<float>::quiet_NaN());

    const bool gravityDown = settings.gravity >= 0.0;
    if (gravityDown) {
        for (int x = 0; x < width; ++x) {
            bool inSegment = false;
            float segmentSurface = 0.0f;

            for (int y = 0; y < height; ++y) {
                const size_t idx = static_cast<size_t>(y) * width + x;
                const float v = waterView.volume[idx];
                if (v <= kVolumeEpsilon) {
                    inSegment = false;
                    continue;
                }

                if (!inSegment) {
                    segmentSurface = static_cast<float>(y) + (1.0f - std::clamp(v, 0.0f, 1.0f));
                    inSegment = true;
                }

                surfaceScratch[idx] = segmentSurface;
            }
        }
    }
    else {
        for (int x = 0; x < width; ++x) {
            bool inSegment = false;
            float segmentSurface = 0.0f;

            for (int y = height - 1; y >= 0; --y) {
                const size_t idx = static_cast<size_t>(y) * width + x;
                const float v = waterView.volume[idx];
                if (v <= kVolumeEpsilon) {
                    inSegment = false;
                    continue;
                }

                if (!inSegment) {
                    segmentSurface = static_cast<float>(y) + std::clamp(v, 0.0f, 1.0f);
                    inSegment = true;
                }

                surfaceScratch[idx] = segmentSurface;
            }
        }
    }

    const double gravity = settings.gravity;
    const double waterDensity = Material::getDensity(Material::EnumType::Water);

    const auto incorporateNeighborSurface = [&](int nx, int ny, float& surface, bool& hasSurface) {
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
            return;
        }

        const size_t nIdx = static_cast<size_t>(ny) * width + nx;
        if (waterView.volume[nIdx] <= kVolumeEpsilon) {
            return;
        }

        const float neighborSurface = surfaceScratch[nIdx];
        if (!std::isfinite(neighborSurface)) {
            return;
        }

        if (!hasSurface) {
            surface = neighborSurface;
            hasSurface = true;
            return;
        }

        surface =
            gravityDown ? std::min(surface, neighborSurface) : std::max(surface, neighborSurface);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.isEmpty() || cell.isWall()) {
                continue;
            }
            if (cell.material_type == Material::EnumType::Water) {
                continue;
            }

            float surface = 0.0f;
            bool hasSurface = false;
            incorporateNeighborSurface(x - 1, y, surface, hasSurface);
            incorporateNeighborSurface(x + 1, y, surface, hasSurface);
            incorporateNeighborSurface(x, y - 1, surface, hasSurface);
            incorporateNeighborSurface(x, y + 1, surface, hasSurface);

            if (!hasSurface) {
                continue;
            }

            const float submerged = gravityDown
                ? std::clamp(static_cast<float>(y) + 1.0f - surface, 0.0f, 1.0f)
                : std::clamp(surface - static_cast<float>(y), 0.0f, 1.0f);

            if (submerged <= 0.0f) {
                continue;
            }

            if (buoyancyStrength > 0.0 && std::abs(gravity) > 0.00001) {
                const double buoyancyForceY =
                    -buoyancyStrength * waterDensity * gravity * static_cast<double>(submerged);
                cell.addPendingForce(Vector2d{ 0.0, buoyancyForceY });
            }

            if (dragRate > 0.0) {
                const double mass = cell.getMass();
                if (mass > 0.0001) {
                    const Vector2d dragForce = Vector2d(cell.velocity)
                        * (-dragRate * static_cast<double>(submerged) * mass);
                    cell.addPendingForce(dragForce);
                }
            }
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
    const WorldRegionActivityTracker& region_activity_tracker = pImpl->region_activity_tracker_;

    WorldAirResistanceCalculator air_resistance_calculator{};

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            Cell& cell = data.at(x, y);

            if (!cell.isEmpty() && !cell.isWall()) {
                if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                    pImpl->sleep_force_processing_skipped_[idx] = 1;
                    continue;
                }

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
    const WorldRegionActivityTracker& region_activity_tracker = pImpl->region_activity_tracker_;

    for (CellDebug& debug : data.debug_info) {
        debug.accumulated_adhesion_force = {};
        debug.accumulated_com_cohesion_force = {};
    }

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
                CellDebug& debug = const_cast<GridOfCells&>(grid).debugAt(x, y);

                if (cell.isEmpty() || cell.isWall()) {
                    continue;
                }

                if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                    const size_t idx = static_cast<size_t>(y) * data.width + x;
                    pImpl->sleep_force_processing_skipped_[idx] = 1;
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
                debug.accumulated_com_cohesion_force = com_cohesion_force;
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
                CellDebug& cell_debug = const_cast<GridOfCells&>(grid).debugAt(x, y);

                if (cell.isEmpty() || cell.isWall()) {
                    continue;
                }

                if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                    const size_t idx = static_cast<size_t>(y) * data.width + x;
                    pImpl->sleep_force_processing_skipped_[idx] = 1;
                    continue;
                }

                // Use cache-optimized version with MaterialNeighborhood.
                const MaterialNeighborhood mat_n = grid.getMaterialNeighborhood(x, y);
                WorldAdhesionCalculator::AdhesionForce adhesion =
                    adhesion_calc.calculateAdhesionForce(*this, x, y, mat_n);
                Vector2d adhesion_force = adhesion.force_direction * adhesion.force_magnitude
                    * settings.adhesion_strength;
                if (isLoadBearingGranularCell(cell) && cell_debug.has_granular_support_path
                    && isGranularSupportSinkMaterial(adhesion.target_material)) {
                    adhesion_force = {};
                }
                if (cell.material_type == Material::EnumType::Water
                    && cell_debug.gravity_skipped_for_support
                    && isFluidSupportSinkMaterial(adhesion.target_material)) {
                    adhesion_force = {};
                }
                cell.addPendingForce(adhesion_force);
                // Store for visualization in GridOfCells debug info.
                cell_debug.accumulated_adhesion_force = adhesion_force;
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
    const WorldRegionActivityTracker& region_activity_tracker = pImpl->region_activity_tracker_;

    for (CellDebug& debug : data.debug_info) {
        debug.accumulated_pressure_force = {};
    }

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
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            Cell& cell = data.at(x, y);

            // Skip empty cells and walls.
            if (cell.isEmpty() || cell.isWall()) {
                continue;
            }

            if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                pImpl->sleep_force_processing_skipped_[idx] = 1;
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
                data.debug_info[idx].accumulated_pressure_force = pressure_force;

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
    const WorldRegionActivityTracker& region_activity_tracker = pImpl->region_activity_tracker_;

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

    // Apply buoyancy + drag from the separate-layer MAC water volume.
    {
        ScopeTimer waterTimer(timers, "resolve_forces_apply_mac_water");
        applyMacWaterCouplingForces();
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

    for (CellDebug& debug : data.debug_info) {
        debug.accumulated_viscous_force = {};
    }

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
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                Cell& cell = data.at(x, y);
                CellDebug& debug = data.debug_info[idx];

                if (cell.isEmpty() || cell.isWall()) {
                    continue;
                }

                if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                    pImpl->sleep_force_processing_skipped_[idx] = 1;
                    continue;
                }

                if (debug.gravity_skipped_for_support) {
                    continue;
                }

                // Calculate viscous force from neighbor velocity averaging.
                auto viscous_result =
                    viscosity_calc.calculateViscousForce(*this, x, y, visc_strength, &grid);
                cell.addPendingForce(viscous_result.force);

                // Store for visualization in GridOfCells debug info.
                debug.accumulated_viscous_force = viscous_result.force;
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

                const size_t idx = static_cast<size_t>(y) * data.width + x;
                Cell& cell = data.at(x, y);

                // Skip organism cells - they're handled by resolveRigidBodies().
                if (organism_manager_->hasOrganism(
                        Vector2i{ static_cast<int>(x), static_cast<int>(y) })) {
                    continue;
                }

                if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                    pImpl->sleep_force_processing_skipped_[idx] = 1;
                    continue;
                }

                // Get the total pending force (includes gravity, pressure, cohesion,
                // adhesion, friction, viscosity, etc).
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
    const double gravity = pImpl->physicsSettings_.gravity;
    const float gravityMagnitude = static_cast<float>(std::abs(gravity));
    const int supportOffsetY = gravity > 0.0 ? 1 : -1;
    const WorldRegionActivityTracker& region_activity_tracker = pImpl->region_activity_tracker_;

    // Pre-allocate moves vector based on previous frame's count.
    static size_t last_move_count = 0;
    std::vector<MaterialMove> moves;
    moves.reserve(last_move_count + last_move_count / 10); // +10% buffer

    // Counters for move generation analysis.
    size_t num_cells_with_velocity = 0;
    size_t num_boundary_crossings = 0;
    size_t num_moves_generated = 0;
    size_t num_transfers_generated = 0;
    size_t num_compression_generated = 0;
    size_t num_collisions_generated = 0;
    std::vector<int8_t> granularSupportCache(data.cells.size(), -1);

    for (CellDebug& debug : data.debug_info) {
        debug.blocked_outgoing_transfer_amount = 0.0f;
        debug.blocked_outgoing_transfer_count = 0;
        debug.downward_absorption_count = 0;
        debug.downward_air_target_count = 0;
        debug.downward_elastic_collision_count = 0;
        debug.downward_fluid_blocked_contact_count = 0;
        debug.downward_generated_move_count = 0;
        debug.downward_inelastic_collision_count = 0;
        debug.downward_same_material_target_count = 0;
        debug.downward_transfer_only_count = 0;
        debug.downward_wall_target_count = 0;
        debug.downward_zero_amount_move_count = 0;
        debug.generated_move_count = 0;
        debug.generated_move_direction_mask = CellDebug::DirectionNone;
        debug.gravity_compression_candidate_count = 0;
        debug.gravity_compression_candidate_direction_mask = CellDebug::DirectionNone;
        debug.incoming_compression_branch_mask = CellDebug::CompressionBranchNone;
        debug.incoming_compression_contact_count = 0;
        debug.jammed_contact_candidate_count = 0;
        debug.jammed_contact_candidate_direction_mask = CellDebug::DirectionNone;
        debug.max_incoming_compression_normal_after = 0.0;
        debug.max_incoming_compression_normal_before = 0.0;
        debug.max_outgoing_compression_normal_after = 0.0;
        debug.max_outgoing_compression_normal_before = 0.0;
        debug.outgoing_compression_branch_mask = CellDebug::CompressionBranchNone;
        debug.outgoing_compression_contact_count = 0;
        debug.received_move_count = 0;
        debug.received_move_direction_mask = CellDebug::DirectionNone;
        debug.successful_incoming_transfer_amount = 0.0f;
        debug.successful_incoming_transfer_count = 0;
        debug.successful_outgoing_transfer_amount = 0.0f;
        debug.successful_outgoing_transfer_count = 0;
    }

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t cellIndex = static_cast<size_t>(y) * data.width + x;
            Cell& cell = data.at(x, y);
            CellDebug& debug = data.debug_info[cellIndex];

            // Skip empty, wall, and air cells - they don't generate material moves.
            if (cell.isEmpty() || cell.isWall() || cell.isAir()) {
                continue;
            }

            if (shouldEnforceRegionSleep(region_activity_tracker, cell, x, y)) {
                pImpl->sleep_move_generation_skipped_[cellIndex] = 1;
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
                    const uint8_t directionMask = directionToMask(direction);
                    const bool isGravityCompressionCandidate =
                        isSupportedGranularCompressionCandidate(
                            data,
                            cell,
                            Vector2i(x, y),
                            direction,
                            gravityMagnitude,
                            supportOffsetY,
                            granularSupportCache,
                            true);
                    const bool isJammedContactCandidate = isSupportedGranularCompressionCandidate(
                        data,
                        cell,
                        Vector2i(x, y),
                        direction,
                        gravityMagnitude,
                        supportOffsetY,
                        granularSupportCache,
                        false);

                    if (isGravityCompressionCandidate) {
                        incrementSaturating(debug.gravity_compression_candidate_count);
                        debug.gravity_compression_candidate_direction_mask |= directionMask;
                    }

                    if (isJammedContactCandidate) {
                        incrementSaturating(debug.jammed_contact_candidate_count);
                        debug.jammed_contact_candidate_direction_mask |= directionMask;
                    }

                    // Create enhanced MaterialMove with collision physics data.
                    MaterialMove move = collision_calc.createCollisionAwareMove(
                        *this,
                        cell,
                        data.at(targetPos.x, targetPos.y),
                        Vector2i(x, y),
                        targetPos,
                        deltaTime);

                    if (isGravityCompressionCandidate) {
                        move.amount = 0.0f;
                        move.collision_type = CollisionType::COMPRESSION_CONTACT;
                        move.pressure_from_excess = 0.0f;
                        move.restitution_coefficient = 0.0f;
                    }

                    noteGeneratedMoveClassification(debug, move, data.at(targetPos.x, targetPos.y));

                    num_moves_generated++;
                    switch (move.collision_type) {
                        case CollisionType::TRANSFER_ONLY:
                            num_transfers_generated++;
                            break;
                        case CollisionType::COMPRESSION_CONTACT:
                        case CollisionType::FLUID_BLOCKED_CONTACT:
                            num_compression_generated++;
                            break;
                        case CollisionType::ELASTIC_REFLECTION:
                        case CollisionType::INELASTIC_COLLISION:
                        case CollisionType::FRAGMENTATION:
                        case CollisionType::ABSORPTION:
                            num_collisions_generated++;
                            break;
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
        "transfers, {} compression contacts, {} collisions)",
        num_cells_with_velocity,
        num_boundary_crossings,
        num_moves_generated,
        num_transfers_generated,
        num_compression_generated,
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
    size_t num_compression = 0;
    size_t num_transfers = 0;
    size_t num_elastic = 0;
    size_t num_inelastic = 0;

    // Shuffle moves to handle conflicts randomly.
    {
        ScopeTimer shuffleTimer(timers, "process_moves_shuffle");
        std::shuffle(pending_moves.begin(), pending_moves.end(), *rng_);
    }

    for (const auto& move : pending_moves) {
        const size_t fromIndex = static_cast<size_t>(move.from.y) * data.width + move.from.x;
        const size_t toIndex = static_cast<size_t>(move.to.y) * data.width + move.to.x;
        const uint8_t generatedDirectionMask =
            directionToMask(Vector2i{ move.to.x - move.from.x, move.to.y - move.from.y });
        const uint8_t receivedDirectionMask =
            directionToMask(Vector2i{ move.from.x - move.to.x, move.from.y - move.to.y });
        if (fromIndex < data.debug_info.size()) {
            CellDebug& fromDebug = data.debug_info[fromIndex];
            incrementSaturating(fromDebug.generated_move_count);
            fromDebug.generated_move_direction_mask |= generatedDirectionMask;
        }
        if (toIndex < data.debug_info.size()) {
            CellDebug& toDebug = data.debug_info[toIndex];
            incrementSaturating(toDebug.received_move_count);
            toDebug.received_move_direction_mask |= receivedDirectionMask;
        }

        if (move.collision_type != CollisionType::COMPRESSION_CONTACT
            && move.collision_type != CollisionType::FLUID_BLOCKED_CONTACT) {
            pImpl->region_activity_tracker_.noteMaterialMove(
                move.from.x, move.from.y, move.to.x, move.to.y);
        }

        Cell& fromCell = data.at(move.from.x, move.from.y);
        Cell& toCell = data.at(move.to.x, move.to.y);
        const float fromFillBefore = fromCell.fill_ratio;

        // Apply any pressure from excess that couldn't transfer.
        if (move.collision_type != CollisionType::COMPRESSION_CONTACT
            && move.collision_type != CollisionType::FLUID_BLOCKED_CONTACT
            && move.pressure_from_excess > 0.0) {
            if (toCell.material_type == Material::EnumType::Wall) {
                pImpl->pressure_calculator_.accumulateDynamicPressure(
                    *this, move.from.x, move.from.y, move.pressure_from_excess);
                if (fromIndex < data.debug_info.size()) {
                    CellDebug& fromDebug = data.debug_info[fromIndex];
                    incrementSaturating(fromDebug.excess_move_pressure_injection_count);
                    fromDebug.excess_move_pressure_injection_amount += move.pressure_from_excess;
                }

                spdlog::debug(
                    "Wall blocked transfer: source cell({},{}) pressure increased by {:.3f}",
                    move.from.x,
                    move.from.y,
                    move.pressure_from_excess);
            }
            else {
                pImpl->pressure_calculator_.accumulateDynamicPressure(
                    *this, move.to.x, move.to.y, move.pressure_from_excess);
                if (toIndex < data.debug_info.size()) {
                    CellDebug& toDebug = data.debug_info[toIndex];
                    incrementSaturating(toDebug.excess_move_pressure_injection_count);
                    toDebug.excess_move_pressure_injection_amount += move.pressure_from_excess;
                }

                spdlog::debug(
                    "Applied pressure from excess: cell({},{}) pressure increased by {:.3f}",
                    move.to.x,
                    move.to.y,
                    move.pressure_from_excess);
            }
        }

        // Check if materials should swap instead of colliding (if enabled).
        if (settings.swap_enabled && move.collision_type != CollisionType::TRANSFER_ONLY
            && move.collision_type != CollisionType::COMPRESSION_CONTACT
            && move.collision_type != CollisionType::FLUID_BLOCKED_CONTACT) {
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
            case CollisionType::COMPRESSION_CONTACT:
                num_compression++;
                collision_calc.handleCompressionContact(*this, fromCell, toCell, move);
                break;
            case CollisionType::FLUID_BLOCKED_CONTACT:
                num_compression++;
                collision_calc.handleFluidBlockedContact(fromCell, toCell, move);
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

        const float actualTransferred =
            std::clamp(fromFillBefore - fromCell.fill_ratio, 0.0f, move.amount);
        const float blockedTransfer = std::max(0.0f, move.amount - actualTransferred);
        if (fromIndex < data.debug_info.size()) {
            CellDebug& fromDebug = data.debug_info[fromIndex];
            if (actualTransferred > MIN_MATTER_THRESHOLD) {
                incrementSaturating(fromDebug.successful_outgoing_transfer_count);
                fromDebug.successful_outgoing_transfer_amount += actualTransferred;
            }
            if (blockedTransfer > MIN_MATTER_THRESHOLD) {
                incrementSaturating(fromDebug.blocked_outgoing_transfer_count);
                fromDebug.blocked_outgoing_transfer_amount += blockedTransfer;
            }
        }
        if (toIndex < data.debug_info.size() && actualTransferred > MIN_MATTER_THRESHOLD) {
            CellDebug& toDebug = data.debug_info[toIndex];
            incrementSaturating(toDebug.successful_incoming_transfer_count);
            toDebug.successful_incoming_transfer_amount += actualTransferred;
        }

        // Update organism tracking if material actually transferred.
        // Check: organism owned the source AND source is now empty (transfer succeeded).
        // Note: This applies to ALL collision types that can transfer material, not just
        // TRANSFER_ONLY. INELASTIC_COLLISION, FRAGMENTATION, and ABSORPTION can all empty
        // an organism's cell via transferToWithPhysics.
        if (organism_id != INVALID_ORGANISM_ID && fromCell.isEmpty()) {
            // Material fully transferred - update organism tracking.
            Vector2i to_pos{ static_cast<int>(move.to.x), static_cast<int>(move.to.y) };
            organism_manager_->moveOrganismCell(from_pos, to_pos, organism_id);
        }
    }

    // Log move statistics.
    spdlog::debug(
        "processMaterialMoves: {} total moves, {} swaps ({:.1f}% - {} from transfers, {} from "
        "collisions), {} transfers, {} compression, {} elastic, {} inelastic",
        num_moves,
        num_swaps,
        num_moves > 0 ? (100.0 * num_swaps / num_moves) : 0.0,
        num_swaps_from_transfers,
        num_swaps_from_collisions,
        num_transfers,
        num_compression,
        num_elastic,
        num_inelastic);

    if (num_moves > 0) {
        markGridCacheDirty();
    }

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

    markGridCacheDirty();
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
        markGridCacheDirty();
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
    WorldData dataCopy = pImpl->data_;

    WaterVolumeView waterVolumeView{};
    if (tryGetWaterVolumeView(waterVolumeView) && waterVolumeView.width == dataCopy.width
        && waterVolumeView.height == dataCopy.height) {
        dataCopy.water_volume =
            std::vector<float>(waterVolumeView.volume.begin(), waterVolumeView.volume.end());
    }

    return ReflectSerializer::to_json(dataCopy);
}

void World::fromJSON(const nlohmann::json& doc)
{
    pImpl->data_ = ReflectSerializer::from_json<WorldData>(doc);
    resizeRegionDebugTracking(*pImpl, pImpl->data_.width, pImpl->data_.height);
    markGridCacheDirty();
    recomputeStaticLoad("static_load_deserialize_recompute");

    pImpl->water_sim_system_.syncToSettings(
        pImpl->physicsSettings_, pImpl->data_.width, pImpl->data_.height);
    if (pImpl->physicsSettings_.water_sim_mode == WaterSimMode::MacProjection
        && pImpl->data_.water_volume.has_value()) {
        WaterVolumeMutableView volumeMutable{};
        if (pImpl->water_sim_system_.tryGetMutableWaterVolumeView(volumeMutable)
            && volumeMutable.width == pImpl->data_.width
            && volumeMutable.height == pImpl->data_.height
            && volumeMutable.volume.size() == pImpl->data_.water_volume->size()) {
            std::copy(
                pImpl->data_.water_volume->begin(),
                pImpl->data_.water_volume->end(),
                volumeMutable.volume.begin());
            pImpl->data_.water_volume.reset();
        }
    }

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
