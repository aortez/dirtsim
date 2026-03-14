#include "core/GridOfCells.h"
#include "core/PhysicsSettings.h"
#include "core/RegionDebugInfo.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldRegionActivityTracker.h"
#include "core/scenarios/DamBreakScenario.h"
#include "core/scenarios/SandboxScenario.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace DirtSim;

namespace {

constexpr double kDt = 0.016;
constexpr int kWorldWidth = 64;
constexpr int kWorldHeight = 40;
constexpr int kSandboxWorldWidth = 50;
constexpr int kSandboxWorldHeight = 30;
constexpr int kSettleSteps = 180;
constexpr int kWaterSettleSteps = 600;
constexpr int kPileCenterX = kWorldWidth / 2;
constexpr int kPileBaseY = kWorldHeight - 2;
constexpr int kPileBaseHalfWidth = 22;
constexpr int kPileTopHalfWidth = 6;
constexpr int kPileHeight = 18;
constexpr int kRegionSize = 8;
constexpr int kDiagnosticFrames = 16;
constexpr int kTrackedCellsPerRegion = 4;

struct GravityCase {
    const char* name = "";
    double gravity = 0.0;
    bool expect_quiet_buried_region = false;
};

struct RegionCoord {
    int x = 0;
    int y = 0;
};

struct RegionFrameSample {
    int frame = 0;
    RegionState state = RegionState::Awake;
    WakeReason wake_reason = WakeReason::None;
    int material_cells = 0;
    int moving_cells = 0;
    int near_bottom_cells = 0;
    int near_top_cells = 0;
    int gravity_skipped_cells = 0;
    int support_path_cells = 0;
    int carrying_load_cells = 0;
    int generated_move_cells = 0;
    int generated_horizontal_move_cells = 0;
    int generated_vertical_move_cells = 0;
    int gravity_compression_candidate_cells = 0;
    int granular_compression_contact_cells = 0;
    int incoming_compression_contact_cells = 0;
    int jammed_contact_candidate_cells = 0;
    int outgoing_compression_contact_cells = 0;
    int received_move_cells = 0;
    int received_horizontal_move_cells = 0;
    int received_vertical_move_cells = 0;
    int hydrostatic_pressure_injection_cells = 0;
    int dynamic_pressure_target_injection_cells = 0;
    int dynamic_pressure_reflection_injection_cells = 0;
    int excess_move_pressure_injection_cells = 0;
    int successful_outgoing_transfer_cells = 0;
    int successful_incoming_transfer_cells = 0;
    int support_compression_contact_cells = 0;
    int blocked_outgoing_transfer_cells = 0;
    int viscous_force_cells = 0;
    int generated_moves = 0;
    int gravity_compression_candidates = 0;
    int incoming_compression_contacts = 0;
    int jammed_contact_candidates = 0;
    int outgoing_compression_contacts = 0;
    int received_moves = 0;
    int successful_outgoing_transfers = 0;
    int successful_incoming_transfers = 0;
    int blocked_outgoing_transfers = 0;
    int hydrostatic_pressure_injection_events = 0;
    int dynamic_pressure_target_injection_events = 0;
    int dynamic_pressure_reflection_injection_events = 0;
    int excess_move_pressure_injection_events = 0;
    double hydrostatic_pressure_injection_amount = 0.0;
    double dynamic_pressure_target_injection_amount = 0.0;
    double dynamic_pressure_reflection_injection_amount = 0.0;
    double excess_move_pressure_injection_amount = 0.0;
    double successful_outgoing_transfer_amount = 0.0;
    double successful_incoming_transfer_amount = 0.0;
    double blocked_outgoing_transfer_amount = 0.0;
    double max_incoming_compression_normal_after = 0.0;
    double max_incoming_compression_normal_before = 0.0;
    double max_outgoing_compression_normal_after = 0.0;
    double max_outgoing_compression_normal_before = 0.0;
    double max_velocity = 0.0;
    double mean_velocity = 0.0;
    double max_abs_velocity_y = 0.0;
    double mean_abs_velocity_y = 0.0;
    double max_abs_viscous_force = 0.0;
    double mean_abs_viscous_force = 0.0;
    double max_abs_viscous_force_y = 0.0;
    double mean_abs_viscous_force_y = 0.0;
    double max_abs_pressure_gradient_y = 0.0;
    double mean_abs_pressure_gradient_y = 0.0;
    double max_abs_pressure_force_y = 0.0;
    double mean_abs_pressure_force_y = 0.0;
    double mean_pressure = 0.0;
};

struct TransferQuietHeuristicConfig {
    int quiet_frames_required = 8;
    double max_live_pressure_delta = 0.03;
    double max_static_load_delta = 0.001;
    double max_max_velocity = 0.60;
    double max_mean_velocity = 0.20;
};

struct TransferQuietFrameSample {
    int frame = 0;
    bool qualifies = false;
    int quiet_streak = 0;
    RegionFrameSample sample{};
    RegionSummary summary{};
};

struct TransferQuietRegionObservation {
    RegionCoord region{};
    bool ever_candidate = false;
    int first_candidate_frame = -1;
    int max_generated_moves = 0;
    int max_quiet_streak = 0;
    int max_successful_transfers = 0;
};

struct TransferQuietExperimentResult {
    std::optional<RegionCoord> pure_region;
    std::vector<TransferQuietFrameSample> pure_region_frames;
    std::vector<TransferQuietRegionObservation> regions;
};

struct ForceIsolationProfile {
    const char* name = "";
    double gravity = 10.0;
    bool pressure = true;
    bool cohesion = true;
    bool adhesion = true;
    bool friction = true;
    bool viscosity = true;
    bool air_resistance = true;
    bool swap = true;
};

struct ForceIsolationObservation {
    std::string name;
    int quiet_buried_regions = 0;
    int transfer_quiet_candidate_regions = 0;
    bool pure_region_found = false;
    bool pure_region_candidate = false;
    int pure_region_first_candidate_frame = -1;
    int pure_region_max_generated_moves = 0;
    RegionState final_state = RegionState::Awake;
    WakeReason final_wake_reason = WakeReason::None;
    double final_mean_velocity = 0.0;
    double final_max_velocity = 0.0;
    double final_mean_abs_pressure_gradient_y = 0.0;
    double final_mean_abs_pressure_force_y = 0.0;
    double final_mean_pressure = 0.0;
};

struct SandboxRegionBuckets {
    std::vector<RegionCoord> dirt_regions;
    std::vector<RegionCoord> exposed_dirt_regions;
    std::vector<RegionCoord> interior_dirt_regions;
    std::vector<RegionCoord> mixed_dirt_regions;
};

struct WaterRegionBuckets {
    std::vector<RegionCoord> water_regions;
    std::vector<RegionCoord> exposed_water_regions;
    std::vector<RegionCoord> interior_water_regions;
};

struct SandboxForceIsolationObservation {
    std::string name;
    int dirt_regions = 0;
    int exposed_dirt_regions = 0;
    int interior_dirt_regions = 0;
    int mixed_dirt_regions = 0;
    int quiet_interior_regions = 0;
    bool representative_interior_found = false;
    RegionState representative_interior_state = RegionState::Awake;
    WakeReason representative_interior_wake_reason = WakeReason::None;
    double representative_interior_mean_velocity = 0.0;
    double representative_interior_max_velocity = 0.0;
};

struct WaterForceIsolationObservation {
    std::string name;
    int water_regions = 0;
    int interior_water_regions = 0;
    int quiet_interior_regions = 0;
    bool representative_interior_found = false;
    RegionState representative_interior_state = RegionState::Awake;
    WakeReason representative_interior_wake_reason = WakeReason::None;
    double representative_interior_mean_velocity = 0.0;
    double representative_interior_max_velocity = 0.0;
    double representative_interior_mean_pressure_force_y = 0.0;
    double representative_interior_mean_viscous_force_y = 0.0;
};

struct NamedRegion {
    const char* label = "";
    RegionCoord region{};
};

struct CellTrack {
    int x = 0;
    int y = 0;
};

struct WorldPressureSourceFrameSample {
    int frame = 0;
    int source_cells = 0;
    int surface_adjacent_source_cells = 0;
    int buried_source_cells = 0;
    int hydrostatic_source_cells = 0;
    int dynamic_target_source_cells = 0;
    int dynamic_reflection_source_cells = 0;
    int excess_move_source_cells = 0;
    int hydrostatic_source_events = 0;
    int dynamic_target_source_events = 0;
    int dynamic_reflection_source_events = 0;
    int excess_move_source_events = 0;
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;
    double hydrostatic_source_amount = 0.0;
    double dynamic_target_source_amount = 0.0;
    double dynamic_reflection_source_amount = 0.0;
    double excess_move_source_amount = 0.0;
    double strongest_source_amount = 0.0;
    int strongest_source_x = -1;
    int strongest_source_y = -1;
    double strongest_source_pressure = 0.0;
    double strongest_source_gradient_y = 0.0;
    double strongest_source_force_y = 0.0;
};

RegionFrameSample sampleRegionFrame(const World& world, RegionCoord region, int frame);
std::string dumpPureBuriedRegionInitialTimeSeries(const World& world, RegionCoord region);
std::string dumpPureBuriedRegionTimeSeries(const World& world, RegionCoord region);
std::string dumpInitialWorldPressureSourceTimeSeries(const World& world);
std::string dumpWorldPressureSourceSettleScan(
    const World& world, std::optional<RegionCoord> region);
std::string dumpWorldPressureSourceTimeSeries(const World& world);
std::string dumpRegionStates(const World& world);
std::string dumpWakeReasons(const World& world);
std::string dumpBuriedRegionCellDetails(
    const World& world, const std::vector<RegionCoord>& buried_regions);
std::string dumpBuriedRegionCellMaps(
    const World& world, const std::vector<RegionCoord>& buried_regions);
std::string dumpSandboxTrackedRegionTimeSeries(
    const World& world, const std::vector<NamedRegion>& tracked_regions);
std::string dumpSettlingDiagnostics(
    const World& world, const GridOfCells& grid, const std::vector<RegionCoord>& buriedRegions);
void initializeSleepAnalysisWorld(World& world);
void initializeSandboxQuadrantWorld(World& world);
void initializeStillWaterPoolWorld(World& world);
void initializeDamBreakWorld(World& world, DamBreakScenario& scenario);
bool cellHasEmptyAdjacency(const GridOfCells& grid, int x, int y);
char regionStateToChar(RegionState state);
char wakeReasonToChar(WakeReason reason);
std::vector<CellTrack> selectTrackedCells(const World& world, RegionCoord region);

bool isLoadBearingGranularCell(const Cell& cell)
{
    if (cell.isEmpty()) {
        return false;
    }

    switch (cell.material_type) {
        case Material::EnumType::Dirt:
        case Material::EnumType::Sand:
            return true;
        case Material::EnumType::Air:
        case Material::EnumType::Leaf:
        case Material::EnumType::Metal:
        case Material::EnumType::Root:
        case Material::EnumType::Seed:
        case Material::EnumType::Wall:
        case Material::EnumType::Water:
        case Material::EnumType::Wood:
            return false;
    }

    return false;
}

GridOfCells makeGrid(World& world)
{
    WorldData& data = world.getData();
    return GridOfCells(data.cells, data.debug_info, data.width, data.height);
}

void fillHorizontalSpan(
    World& world, int y, int x_begin, int x_end, Material::EnumType material_type)
{
    for (int x = x_begin; x <= x_end; ++x) {
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(y) }, material_type);
    }
}

void buildSymmetricDirtPile(World& world)
{
    for (int row = 0; row < kPileHeight; ++row) {
        const int y = kPileBaseY - row;
        const double t = kPileHeight <= 1 ? 0.0 : static_cast<double>(row) / (kPileHeight - 1);
        const int half_width =
            static_cast<int>(std::round((1.0 - t) * kPileBaseHalfWidth + t * kPileTopHalfWidth));
        fillHorizontalSpan(
            world,
            y,
            kPileCenterX - half_width,
            kPileCenterX + half_width,
            Material::EnumType::Dirt);
    }
}

void carveLeftToeNotch(World& world)
{
    fillHorizontalSpan(world, 36, 12, 14, Material::EnumType::Air);
    fillHorizontalSpan(world, 37, 11, 14, Material::EnumType::Air);
    fillHorizontalSpan(world, 38, 10, 14, Material::EnumType::Air);
}

void carveLargeLeftToeCut(World& world)
{
    fillHorizontalSpan(world, 33, 14, 18, Material::EnumType::Air);
    fillHorizontalSpan(world, 34, 13, 18, Material::EnumType::Air);
    fillHorizontalSpan(world, 35, 12, 18, Material::EnumType::Air);
    fillHorizontalSpan(world, 36, 11, 18, Material::EnumType::Air);
    fillHorizontalSpan(world, 37, 10, 18, Material::EnumType::Air);
    fillHorizontalSpan(world, 38, 9, 18, Material::EnumType::Air);
}

void buildSupportedGranularWedge(World& world)
{
    fillHorizontalSpan(world, 13, 28, 34, Material::EnumType::Dirt);
    fillHorizontalSpan(world, 14, 26, 36, Material::EnumType::Dirt);
    fillHorizontalSpan(world, 15, 24, 38, Material::EnumType::Dirt);
    fillHorizontalSpan(world, 16, 24, 38, Material::EnumType::Dirt);
    for (int y = 17; y <= 38; ++y) {
        fillHorizontalSpan(world, y, 30, 32, Material::EnumType::Dirt);
    }
}

void removeGranularWedgeSupport(World& world)
{
    fillHorizontalSpan(world, 16, 24, 38, Material::EnumType::Air);
}

void settleWorld(World& world, int steps = kSettleSteps, double dt = kDt)
{
    for (int step = 0; step < steps; ++step) {
        world.advanceTime(dt);
    }
}

void applyForceIsolationProfile(World& world, const ForceIsolationProfile& profile)
{
    PhysicsSettings& settings = world.getPhysicsSettings();
    const PhysicsSettings defaults = getDefaultPhysicsSettings();

    settings.gravity = profile.gravity;
    settings.pressure_hydrostatic_enabled = profile.pressure;
    settings.pressure_dynamic_enabled = profile.pressure;
    settings.pressure_hydrostatic_strength =
        profile.pressure ? defaults.pressure_hydrostatic_strength : 0.0;
    settings.pressure_dynamic_strength =
        profile.pressure ? defaults.pressure_dynamic_strength : 0.0;
    settings.pressure_scale = profile.pressure ? defaults.pressure_scale : 0.0;
    settings.pressure_diffusion_strength =
        profile.pressure ? defaults.pressure_diffusion_strength : 0.0;
    settings.pressure_diffusion_iterations =
        profile.pressure ? defaults.pressure_diffusion_iterations : 0;
    settings.pressure_decay_rate = profile.pressure ? defaults.pressure_decay_rate : 0.0;

    settings.cohesion_enabled = profile.cohesion;
    settings.cohesion_strength = profile.cohesion ? defaults.cohesion_strength : 0.0;

    settings.adhesion_enabled = profile.adhesion;
    settings.adhesion_strength = profile.adhesion ? defaults.adhesion_strength : 0.0;

    settings.friction_enabled = profile.friction;
    settings.friction_strength = profile.friction ? defaults.friction_strength : 0.0;

    settings.viscosity_enabled = profile.viscosity;
    settings.viscosity_strength = profile.viscosity ? defaults.viscosity_strength : 0.0;

    settings.swap_enabled = profile.swap;
    world.setAirResistanceEnabled(profile.air_resistance);
    world.setAirResistanceStrength(profile.air_resistance ? defaults.air_resistance : 0.0);
}

int countQuietBuriedRegions(const World& world, const std::vector<RegionCoord>& buried_regions)
{
    const WorldData& data = world.getData();
    int quiet_buried_regions = 0;

    for (const RegionCoord& region : buried_regions) {
        const int idx = region.y * data.region_debug_blocks_x + region.x;
        const RegionState state = static_cast<RegionState>(data.region_debug[idx].state);
        if (state != RegionState::Awake) {
            quiet_buried_regions++;
        }
    }

    return quiet_buried_regions;
}

int countExposedGranularSuccessfulTransfers(const World& world, const GridOfCells& grid)
{
    const WorldData& data = world.getData();
    int successful_transfers = 0;

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const Cell& cell = data.at(x, y);
            const CellDebug& debug = data.debug_info[idx];

            if (!isLoadBearingGranularCell(cell)) {
                continue;
            }

            if (!cellHasEmptyAdjacency(grid, x, y)) {
                continue;
            }

            successful_transfers += debug.successful_outgoing_transfer_count;
            successful_transfers += debug.successful_incoming_transfer_count;
        }
    }

    return successful_transfers;
}

int countMaterialCellsInBox(
    const World& world,
    int x_begin,
    int x_end,
    int y_begin,
    int y_end,
    Material::EnumType material_type)
{
    const WorldData& data = world.getData();
    int count = 0;

    for (int y = std::max(0, y_begin); y <= std::min(static_cast<int>(data.height) - 1, y_end);
         ++y) {
        for (int x = std::max(0, x_begin); x <= std::min(static_cast<int>(data.width) - 1, x_end);
             ++x) {
            if (data.at(x, y).material_type == material_type) {
                count++;
            }
        }
    }

    return count;
}

int countUnsupportedMovingGranularCells(
    const World& world, int x_begin, int x_end, int y_begin, int y_end, double min_velocity)
{
    const WorldData& data = world.getData();
    int count = 0;

    for (int y = std::max(0, y_begin); y <= std::min(static_cast<int>(data.height) - 1, y_end);
         ++y) {
        for (int x = std::max(0, x_begin); x <= std::min(static_cast<int>(data.width) - 1, x_end);
             ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const Cell& cell = data.at(x, y);
            const CellDebug& debug = data.debug_info[idx];

            if (!isLoadBearingGranularCell(cell)) {
                continue;
            }

            if (debug.has_granular_support_path || debug.gravity_skipped_for_support) {
                continue;
            }

            if (cell.velocity.magnitude() <= min_velocity) {
                continue;
            }

            count++;
        }
    }

    return count;
}

bool regionContainsMaterial(const World& world, int block_x, int block_y)
{
    const WorldData& data = world.getData();
    const int x_begin = block_x * kRegionSize;
    const int y_begin = block_y * kRegionSize;
    const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
    const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            if (!data.at(x, y).isEmpty()) {
                return true;
            }
        }
    }

    return false;
}

bool regionContainsMaterialType(
    const World& world, int block_x, int block_y, Material::EnumType material_type)
{
    const WorldData& data = world.getData();
    const int x_begin = block_x * kRegionSize;
    const int y_begin = block_y * kRegionSize;
    const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
    const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            if (data.at(x, y).material_type == material_type) {
                return true;
            }
        }
    }

    return false;
}

bool regionHasEmptyAdjacency(const World& world, const GridOfCells& grid, int block_x, int block_y)
{
    const WorldData& data = world.getData();
    const int x_begin = block_x * kRegionSize;
    const int y_begin = block_y * kRegionSize;
    const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
    const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.isEmpty()) {
                continue;
            }

            if (grid.getEmptyNeighborhood(x, y).countEmptyNeighbors() > 0) {
                return true;
            }
        }
    }

    return false;
}

bool cellHasEmptyAdjacency(const GridOfCells& grid, int x, int y)
{
    return grid.getEmptyNeighborhood(x, y).countEmptyNeighbors() > 0;
}

std::vector<RegionCoord> collectBuriedMaterialRegions(const World& world, const GridOfCells& grid)
{
    const WorldData& data = world.getData();
    std::vector<RegionCoord> buried_regions;

    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            if (!regionContainsMaterial(world, block_x, block_y)) {
                continue;
            }
            if (regionHasEmptyAdjacency(world, grid, block_x, block_y)) {
                continue;
            }

            buried_regions.push_back(RegionCoord{ .x = block_x, .y = block_y });
        }
    }

    return buried_regions;
}

SandboxRegionBuckets classifySandboxDirtRegions(const World& world, const GridOfCells& grid)
{
    const WorldData& data = world.getData();
    const WorldRegionActivityTracker& tracker = world.getRegionActivityTracker();
    SandboxRegionBuckets buckets;

    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            if (!regionContainsMaterialType(world, block_x, block_y, Material::EnumType::Dirt)) {
                continue;
            }

            const RegionCoord region{ .x = block_x, .y = block_y };
            const RegionSummary& summary = tracker.getRegionSummary(block_x, block_y);
            buckets.dirt_regions.push_back(region);

            if (summary.has_mixed_material) {
                buckets.mixed_dirt_regions.push_back(region);
                continue;
            }

            if (regionHasEmptyAdjacency(world, grid, block_x, block_y)) {
                buckets.exposed_dirt_regions.push_back(region);
                continue;
            }

            buckets.interior_dirt_regions.push_back(region);
        }
    }

    return buckets;
}

WaterRegionBuckets classifyWaterRegions(const World& world, const GridOfCells& grid)
{
    const WorldData& data = world.getData();
    WaterRegionBuckets buckets;

    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            if (!regionContainsMaterialType(world, block_x, block_y, Material::EnumType::Water)) {
                continue;
            }

            const RegionCoord region{ .x = block_x, .y = block_y };
            buckets.water_regions.push_back(region);

            if (regionHasEmptyAdjacency(world, grid, block_x, block_y)) {
                buckets.exposed_water_regions.push_back(region);
                continue;
            }

            buckets.interior_water_regions.push_back(region);
        }
    }

    return buckets;
}

int countQuietRegions(const World& world, const std::vector<RegionCoord>& regions)
{
    const WorldData& data = world.getData();
    int quiet_regions = 0;

    for (const RegionCoord& region : regions) {
        const int idx = region.y * data.region_debug_blocks_x + region.x;
        const RegionState state = static_cast<RegionState>(data.region_debug[idx].state);
        if (state != RegionState::Awake) {
            quiet_regions++;
        }
    }

    return quiet_regions;
}

int countQuietRegions(
    const WorldRegionActivityTracker& tracker, const std::vector<RegionCoord>& regions)
{
    int quiet_regions = 0;

    for (const RegionCoord& region : regions) {
        if (tracker.getRegionState(region.x, region.y) != RegionState::Awake) {
            quiet_regions++;
        }
    }

    return quiet_regions;
}

bool regionEquals(RegionCoord a, RegionCoord b)
{
    return a.x == b.x && a.y == b.y;
}

void addNamedRegionIfUnique(
    std::vector<NamedRegion>& tracked_regions, const char* label, std::optional<RegionCoord> region)
{
    if (!region.has_value()) {
        return;
    }

    const auto it = std::find_if(
        tracked_regions.begin(), tracked_regions.end(), [&region](const NamedRegion& candidate) {
            return regionEquals(candidate.region, *region);
        });
    if (it != tracked_regions.end()) {
        return;
    }

    tracked_regions.push_back(NamedRegion{ .label = label, .region = *region });
}

std::vector<RegionCoord> extractNamedRegionCoords(const std::vector<NamedRegion>& tracked_regions)
{
    std::vector<RegionCoord> coords;
    coords.reserve(tracked_regions.size());
    for (const NamedRegion& tracked_region : tracked_regions) {
        coords.push_back(tracked_region.region);
    }
    return coords;
}

WorldRegionActivityTracker runFrozenTrackerReplay(
    const World& world, const WorldRegionActivityTracker::Config& config, int frames)
{
    WorldData& data = const_cast<World&>(world).getData();
    GridOfCells grid(data.cells, data.debug_info, data.width, data.height);

    WorldRegionActivityTracker tracker;
    tracker.resize(data.width, data.height, grid.getBlocksX(), grid.getBlocksY());
    tracker.setConfig(config);

    for (int frame = 0; frame < frames; ++frame) {
        tracker.beginFrame(world, grid, static_cast<uint32_t>(frame));
        tracker.summarizeFrame(world, grid, static_cast<uint32_t>(frame));
    }

    return tracker;
}

std::vector<NamedRegion> selectSandboxDiagnosticRegions(const SandboxRegionBuckets& buckets)
{
    auto sorted_interiors = buckets.interior_dirt_regions;
    auto sorted_exposed = buckets.exposed_dirt_regions;
    auto sorted_mixed = buckets.mixed_dirt_regions;
    const auto sort_regions = [](std::vector<RegionCoord>& regions) {
        std::sort(regions.begin(), regions.end(), [](const RegionCoord& a, const RegionCoord& b) {
            return std::tie(a.y, a.x) < std::tie(b.y, b.x);
        });
    };
    sort_regions(sorted_interiors);
    sort_regions(sorted_exposed);
    sort_regions(sorted_mixed);

    std::vector<NamedRegion> tracked_regions;
    if (sorted_interiors.empty()) {
        return tracked_regions;
    }

    const RegionCoord interior = sorted_interiors.front();
    addNamedRegionIfUnique(tracked_regions, "interior", interior);
    if (sorted_interiors.size() > 1) {
        addNamedRegionIfUnique(tracked_regions, "interior_right", sorted_interiors[1]);
    }

    std::optional<RegionCoord> top_face;
    for (const RegionCoord& region : sorted_exposed) {
        if (region.x == interior.x && region.y < interior.y) {
            top_face = region;
            break;
        }
    }
    addNamedRegionIfUnique(tracked_regions, "top_face", top_face);

    std::optional<RegionCoord> side_face;
    for (const RegionCoord& region : sorted_exposed) {
        if (region.y == interior.y && region.x < interior.x) {
            side_face = region;
            break;
        }
    }
    addNamedRegionIfUnique(tracked_regions, "side_face", side_face);

    std::optional<RegionCoord> mixed_wall;
    for (const RegionCoord& region : sorted_mixed) {
        if (region.y == interior.y && region.x > interior.x) {
            mixed_wall = region;
            break;
        }
    }
    if (!mixed_wall.has_value() && !sorted_mixed.empty()) {
        mixed_wall = sorted_mixed.front();
    }
    addNamedRegionIfUnique(tracked_regions, "mixed_wall", mixed_wall);

    return tracked_regions;
}

std::string dumpNamedRegionList(const std::vector<NamedRegion>& tracked_regions)
{
    std::ostringstream out;
    out << "Tracked regions\n";
    if (tracked_regions.empty()) {
        out << "<none>\n";
        return out.str();
    }

    for (const NamedRegion& tracked_region : tracked_regions) {
        out << tracked_region.label << "=(" << tracked_region.region.x << ","
            << tracked_region.region.y << ")\n";
    }

    return out.str();
}

std::string dumpSandboxDirtRoleMap(const World& world, const SandboxRegionBuckets& buckets)
{
    const WorldData& data = world.getData();
    std::ostringstream out;

    out << "Sandbox dirt role map\n";
    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            const RegionCoord region{ .x = block_x, .y = block_y };
            const auto has_region = [&region](const std::vector<RegionCoord>& regions) {
                return std::find_if(
                           regions.begin(),
                           regions.end(),
                           [&region](const RegionCoord& candidate) {
                               return candidate.x == region.x && candidate.y == region.y;
                           })
                    != regions.end();
            };

            if (has_region(buckets.interior_dirt_regions)) {
                out << 'I';
            }
            else if (has_region(buckets.exposed_dirt_regions)) {
                out << 'E';
            }
            else if (has_region(buckets.mixed_dirt_regions)) {
                out << 'M';
            }
            else if (has_region(buckets.dirt_regions)) {
                out << 'D';
            }
            else {
                out << '.';
            }
        }
        out << '\n';
    }

    return out.str();
}

std::string dumpSandboxDirtRegionDetails(const World& world, const SandboxRegionBuckets& buckets)
{
    const WorldRegionActivityTracker& tracker = world.getRegionActivityTracker();
    std::ostringstream out;

    out << "Sandbox dirt region details\n";
    for (const RegionCoord& region : buckets.dirt_regions) {
        const RegionSummary& summary = tracker.getRegionSummary(region.x, region.y);
        const RegionFrameSample sample = sampleRegionFrame(world, region, kSettleSteps);
        const char role = [&]() -> char {
            if (std::find_if(
                    buckets.interior_dirt_regions.begin(),
                    buckets.interior_dirt_regions.end(),
                    [&region](const RegionCoord& candidate) {
                        return candidate.x == region.x && candidate.y == region.y;
                    })
                != buckets.interior_dirt_regions.end()) {
                return 'I';
            }
            if (std::find_if(
                    buckets.exposed_dirt_regions.begin(),
                    buckets.exposed_dirt_regions.end(),
                    [&region](const RegionCoord& candidate) {
                        return candidate.x == region.x && candidate.y == region.y;
                    })
                != buckets.exposed_dirt_regions.end()) {
                return 'E';
            }
            if (std::find_if(
                    buckets.mixed_dirt_regions.begin(),
                    buckets.mixed_dirt_regions.end(),
                    [&region](const RegionCoord& candidate) {
                        return candidate.x == region.x && candidate.y == region.y;
                    })
                != buckets.mixed_dirt_regions.end()) {
                return 'M';
            }
            return 'D';
        }();

        out << "(" << region.x << "," << region.y << ")"
            << " role=" << role << " state=" << regionStateToChar(sample.state)
            << " wake=" << wakeReasonToChar(sample.wake_reason) << " meanV=" << sample.mean_velocity
            << " maxV=" << sample.max_velocity
            << " moves=" << sample.generated_moves + sample.received_moves
            << " ok=" << sample.successful_outgoing_transfers + sample.successful_incoming_transfers
            << " empty=" << summary.has_empty_adjacency << " mixed=" << summary.has_mixed_material
            << " water=" << summary.has_water_adjacency << " touched=" << summary.touched_this_frame
            << "\n";
    }

    return out.str();
}

std::string dumpSandboxRepresentativeRegionTimeSeries(
    const World& world, const SandboxRegionBuckets& buckets)
{
    std::vector<std::pair<const char*, RegionCoord>> tracked_regions;
    if (!buckets.interior_dirt_regions.empty()) {
        tracked_regions.push_back({ "interior", buckets.interior_dirt_regions.front() });
    }
    if (!buckets.exposed_dirt_regions.empty()) {
        tracked_regions.push_back({ "exposed", buckets.exposed_dirt_regions.front() });
    }
    if (!buckets.mixed_dirt_regions.empty()) {
        tracked_regions.push_back({ "mixed", buckets.mixed_dirt_regions.front() });
    }

    std::ostringstream out;
    out << "Sandbox representative region time series\n";
    if (tracked_regions.empty()) {
        out << "<none>\n";
        return out.str();
    }

    World replay(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(replay);
    replay.getPhysicsSettings() = world.getPhysicsSettings();
    settleWorld(replay);

    for (const auto& [label, region] : tracked_regions) {
        out << label << " (" << region.x << "," << region.y << ")\n";
        out << "frame state wake meanV maxV meanForceY meanP moves ok touched\n";
        for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
            const RegionFrameSample sample = sampleRegionFrame(replay, region, frame);
            const RegionSummary& summary =
                replay.getRegionActivityTracker().getRegionSummary(region.x, region.y);
            out << frame << " " << regionStateToChar(sample.state) << " "
                << wakeReasonToChar(sample.wake_reason) << " " << sample.mean_velocity << " "
                << sample.max_velocity << " " << sample.mean_abs_pressure_force_y << " "
                << sample.mean_pressure << " " << sample.generated_moves + sample.received_moves
                << " "
                << sample.successful_outgoing_transfers + sample.successful_incoming_transfers
                << " " << summary.touched_this_frame << "\n";

            if (frame < kDiagnosticFrames) {
                replay.advanceTime(kDt);
            }
        }
    }

    return out.str();
}

SandboxForceIsolationObservation runSandboxForceIsolationProfile(
    const ForceIsolationProfile& profile)
{
    World world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(world);
    applyForceIsolationProfile(world, profile);

    settleWorld(world);
    const GridOfCells grid = makeGrid(world);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(world, grid);

    SandboxForceIsolationObservation observation{ .name = profile.name };
    observation.dirt_regions = static_cast<int>(buckets.dirt_regions.size());
    observation.exposed_dirt_regions = static_cast<int>(buckets.exposed_dirt_regions.size());
    observation.interior_dirt_regions = static_cast<int>(buckets.interior_dirt_regions.size());
    observation.mixed_dirt_regions = static_cast<int>(buckets.mixed_dirt_regions.size());
    observation.quiet_interior_regions = countQuietRegions(world, buckets.interior_dirt_regions);

    if (buckets.interior_dirt_regions.empty()) {
        return observation;
    }

    observation.representative_interior_found = true;
    const RegionCoord region = buckets.interior_dirt_regions.front();
    const RegionFrameSample sample = sampleRegionFrame(world, region, kSettleSteps);
    observation.representative_interior_state = sample.state;
    observation.representative_interior_wake_reason = sample.wake_reason;
    observation.representative_interior_mean_velocity = sample.mean_velocity;
    observation.representative_interior_max_velocity = sample.max_velocity;
    return observation;
}

WaterForceIsolationObservation runWaterForceIsolationProfile(const ForceIsolationProfile& profile)
{
    World world(kWorldWidth, kWorldHeight);
    initializeStillWaterPoolWorld(world);
    applyForceIsolationProfile(world, profile);

    settleWorld(world, kWaterSettleSteps);
    GridOfCells grid = makeGrid(world);
    const WaterRegionBuckets buckets = classifyWaterRegions(world, grid);

    WorldRegionActivityTracker::Config no_water_policy_config{};
    no_water_policy_config.keep_water_adjacent_awake = false;
    const WorldRegionActivityTracker tracker_without_water_policy =
        runFrozenTrackerReplay(world, no_water_policy_config, 16);

    WaterForceIsolationObservation observation{ .name = profile.name };
    observation.water_regions = static_cast<int>(buckets.water_regions.size());
    observation.interior_water_regions = static_cast<int>(buckets.interior_water_regions.size());
    observation.quiet_interior_regions =
        countQuietRegions(tracker_without_water_policy, buckets.interior_water_regions);

    if (buckets.interior_water_regions.empty()) {
        return observation;
    }

    auto sorted_interiors = buckets.interior_water_regions;
    std::sort(
        sorted_interiors.begin(),
        sorted_interiors.end(),
        [](const RegionCoord& a, const RegionCoord& b) {
            return std::tie(a.y, a.x) < std::tie(b.y, b.x);
        });

    observation.representative_interior_found = true;
    const RegionCoord region = sorted_interiors.front();
    const RegionFrameSample sample = sampleRegionFrame(world, region, kWaterSettleSteps);
    observation.representative_interior_state =
        tracker_without_water_policy.getRegionState(region.x, region.y);
    observation.representative_interior_wake_reason =
        tracker_without_water_policy.getLastWakeReason(region.x, region.y);
    observation.representative_interior_mean_velocity = sample.mean_velocity;
    observation.representative_interior_max_velocity = sample.max_velocity;
    observation.representative_interior_mean_pressure_force_y = sample.mean_abs_pressure_force_y;
    observation.representative_interior_mean_viscous_force_y = sample.mean_abs_viscous_force_y;
    return observation;
}

std::string dumpSandboxQuadrantDiagnostics(const ForceIsolationProfile& profile)
{
    World world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(world);
    applyForceIsolationProfile(world, profile);

    settleWorld(world);
    const GridOfCells grid = makeGrid(world);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(world, grid);

    std::ostringstream out;
    out << "Sandbox quadrant diagnostics\n";
    out << "Profile: " << profile.name << "\n";
    out << "World: " << world.getData().width << "x" << world.getData().height << "\n";
    out << "dirtRegions=" << buckets.dirt_regions.size()
        << " interior=" << buckets.interior_dirt_regions.size()
        << " exposed=" << buckets.exposed_dirt_regions.size()
        << " mixed=" << buckets.mixed_dirt_regions.size()
        << " quietInterior=" << countQuietRegions(world, buckets.interior_dirt_regions) << "\n";
    out << dumpRegionStates(world) << "\n";
    out << dumpWakeReasons(world) << "\n";
    out << dumpSandboxDirtRoleMap(world, buckets) << "\n";
    out << dumpSandboxDirtRegionDetails(world, buckets) << "\n";
    out << dumpSandboxRepresentativeRegionTimeSeries(world, buckets);
    return out.str();
}

std::string dumpSandboxQuadrantForceIsolationSweep()
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const std::vector<ForceIsolationProfile> profiles{
        ForceIsolationProfile{
            .name = "full",
            .gravity = defaults.gravity,
        },
        ForceIsolationProfile{
            .name = "gravity_contact_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "pressure_only",
            .gravity = 0.0,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_pressure_only",
            .gravity = defaults.gravity,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "cohesion_only",
            .gravity = 0.0,
            .pressure = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_cohesion_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "adhesion_only",
            .gravity = 0.0,
            .pressure = false,
            .cohesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_adhesion_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_cohesion_adhesion_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_cohesion_friction_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .adhesion = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_adhesion_friction_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_cohesion_adhesion_friction_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_swap_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
        },
        ForceIsolationProfile{
            .name = "gravity_air_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_air_swap_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
        },
        ForceIsolationProfile{
            .name = "friction_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "viscosity_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_pressure",
            .gravity = defaults.gravity,
            .pressure = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_cohesion",
            .gravity = defaults.gravity,
            .cohesion = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_adhesion",
            .gravity = defaults.gravity,
            .adhesion = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_friction",
            .gravity = defaults.gravity,
            .friction = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_viscosity",
            .gravity = defaults.gravity,
            .viscosity = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_cohesion_adhesion",
            .gravity = defaults.gravity,
            .cohesion = false,
            .adhesion = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_friction_viscosity",
            .gravity = defaults.gravity,
            .friction = false,
            .viscosity = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_cohesion_adhesion_friction_viscosity",
            .gravity = defaults.gravity,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_swap",
            .gravity = defaults.gravity,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_air",
            .gravity = defaults.gravity,
            .air_resistance = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_air_swap",
            .gravity = defaults.gravity,
            .air_resistance = false,
            .swap = false,
        },
    };

    std::ostringstream out;
    out << "Sandbox quadrant force isolation sweep\n";
    out << "profile dirt interior exposed mixed quietInterior repFound repState repWake meanV "
           "maxV\n";

    for (const ForceIsolationProfile& profile : profiles) {
        const SandboxForceIsolationObservation observation =
            runSandboxForceIsolationProfile(profile);
        out << observation.name << " " << observation.dirt_regions << " "
            << observation.interior_dirt_regions << " " << observation.exposed_dirt_regions << " "
            << observation.mixed_dirt_regions << " " << observation.quiet_interior_regions << " "
            << observation.representative_interior_found << " "
            << regionStateToChar(observation.representative_interior_state) << " "
            << wakeReasonToChar(observation.representative_interior_wake_reason) << " "
            << observation.representative_interior_mean_velocity << " "
            << observation.representative_interior_max_velocity << "\n";
    }

    return out.str();
}

std::string dumpSandboxQuadrantAsciiDiagrams()
{
    World initial_world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(initial_world);

    World settled_world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(settled_world);
    settleWorld(settled_world);

    std::ostringstream out;
    out << "Sandbox quadrant ASCII diagrams\n";
    out << "Initial frame\n";
    out << initial_world.toAsciiDiagram() << "\n";
    out << "After settle\n";
    out << settled_world.toAsciiDiagram();
    return out.str();
}

std::string dumpMaterialColumnCounts(const World& world, Material::EnumType material_type)
{
    const WorldData& data = world.getData();
    std::ostringstream out;

    out << "x count\n";
    for (int x = 0; x < data.width; ++x) {
        int count = 0;
        for (int y = 0; y < data.height; ++y) {
            if (data.at(x, y).material_type == material_type) {
                count++;
            }
        }
        out << x << " " << count << "\n";
    }

    return out.str();
}

std::string dumpMaterialRowCounts(const World& world, Material::EnumType material_type)
{
    const WorldData& data = world.getData();
    std::ostringstream out;

    out << "y count\n";
    for (int y = 0; y < data.height; ++y) {
        int count = 0;
        for (int x = 0; x < data.width; ++x) {
            if (data.at(x, y).material_type == material_type) {
                count++;
            }
        }
        out << y << " " << count << "\n";
    }

    return out.str();
}

std::string dumpWaterPoolDiagnostics()
{
    World world(kWorldWidth, kWorldHeight);
    initializeStillWaterPoolWorld(world);
    settleWorld(world, kWaterSettleSteps);

    GridOfCells grid = makeGrid(world);
    const WaterRegionBuckets buckets = classifyWaterRegions(world, grid);

    auto water_regions = buckets.water_regions;
    const auto sort_regions = [](std::vector<RegionCoord>& regions) {
        std::sort(regions.begin(), regions.end(), [](const RegionCoord& a, const RegionCoord& b) {
            return std::tie(a.y, a.x) < std::tie(b.y, b.x);
        });
    };
    sort_regions(water_regions);

    WorldRegionActivityTracker::Config no_water_policy_config{};
    no_water_policy_config.keep_water_adjacent_awake = false;
    const WorldRegionActivityTracker tracker_without_water_policy =
        runFrozenTrackerReplay(world, no_water_policy_config, 16);

    std::ostringstream out;
    out << "Still water pool diagnostics\n";
    out << "waterRegions=" << static_cast<int>(buckets.water_regions.size())
        << " interior=" << static_cast<int>(buckets.interior_water_regions.size())
        << " exposed=" << static_cast<int>(buckets.exposed_water_regions.size()) << "\n";
    out << "actualQuietWaterRegions=" << countQuietRegions(world, buckets.water_regions) << "\n";
    out << "actualQuietInteriorWaterRegions="
        << countQuietRegions(world, buckets.interior_water_regions) << "\n";
    out << "quietWaterRegionsNoWaterPolicy="
        << countQuietRegions(tracker_without_water_policy, buckets.water_regions) << "\n";
    out << "quietInteriorWaterRegionsNoWaterPolicy="
        << countQuietRegions(tracker_without_water_policy, buckets.interior_water_regions) << "\n";

    if (!water_regions.empty()) {
        const RegionCoord representative_region = buckets.interior_water_regions.empty()
            ? water_regions.front()
            : buckets.interior_water_regions.front();
        const RegionFrameSample sample =
            sampleRegionFrame(world, representative_region, kWaterSettleSteps);
        const RegionSummary& actual_summary = world.getRegionActivityTracker().getRegionSummary(
            representative_region.x, representative_region.y);
        const RegionSummary& alt_summary = tracker_without_water_policy.getRegionSummary(
            representative_region.x, representative_region.y);
        out << "representative=(" << representative_region.x << "," << representative_region.y
            << ") actualState="
            << regionStateToChar(world.getRegionActivityTracker().getRegionState(
                   representative_region.x, representative_region.y))
            << " actualWake="
            << wakeReasonToChar(world.getRegionActivityTracker().getLastWakeReason(
                   representative_region.x, representative_region.y))
            << " altState="
            << regionStateToChar(tracker_without_water_policy.getRegionState(
                   representative_region.x, representative_region.y))
            << " altWake="
            << wakeReasonToChar(tracker_without_water_policy.getLastWakeReason(
                   representative_region.x, representative_region.y))
            << " meanV=" << sample.mean_velocity << " maxV=" << sample.max_velocity
            << " actualHasWaterAdj=" << actual_summary.has_water_adjacency
            << " altHasWaterAdj=" << alt_summary.has_water_adjacency
            << " actualHasEmptyAdj=" << actual_summary.has_empty_adjacency
            << " altHasEmptyAdj=" << alt_summary.has_empty_adjacency << "\n";
    }

    out << "Initial frame\n";
    World initial_world(kWorldWidth, kWorldHeight);
    initializeStillWaterPoolWorld(initial_world);
    out << initial_world.toAsciiDiagram() << "\n";
    out << "After settle\n";
    out << world.toAsciiDiagram() << "\n";
    out << "Water columns\n";
    out << dumpMaterialColumnCounts(world, Material::EnumType::Water);
    return out.str();
}

std::string dumpWaterPoolForceIsolationSweep()
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const std::vector<ForceIsolationProfile> profiles = {
        ForceIsolationProfile{
            .name = "gravity_contact_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_pressure_only",
            .gravity = defaults.gravity,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_viscosity_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_pressure_viscosity_only",
            .gravity = defaults.gravity,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_pressure",
            .gravity = defaults.gravity,
            .pressure = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_adhesion",
            .gravity = defaults.gravity,
            .adhesion = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_friction",
            .gravity = defaults.gravity,
            .friction = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_viscosity",
            .gravity = defaults.gravity,
            .viscosity = false,
        },
        ForceIsolationProfile{
            .name = "full",
            .gravity = defaults.gravity,
        },
    };

    std::ostringstream out;
    out << "Still water pool force isolation sweep\n";
    out << "profile water interior quietInterior repFound repState repWake meanV maxV meanPFY "
           "meanVFY\n";

    for (const ForceIsolationProfile& profile : profiles) {
        const WaterForceIsolationObservation observation = runWaterForceIsolationProfile(profile);
        out << observation.name << " " << observation.water_regions << " "
            << observation.interior_water_regions << " " << observation.quiet_interior_regions
            << " " << observation.representative_interior_found << " "
            << regionStateToChar(observation.representative_interior_state) << " "
            << wakeReasonToChar(observation.representative_interior_wake_reason) << " "
            << observation.representative_interior_mean_velocity << " "
            << observation.representative_interior_max_velocity << " "
            << observation.representative_interior_mean_pressure_force_y << " "
            << observation.representative_interior_mean_viscous_force_y << "\n";
    }

    return out.str();
}

std::string dumpDamBreakDiagnostics()
{
    World initial_world(6, 6);
    DamBreakScenario initial_scenario;
    initializeDamBreakWorld(initial_world, initial_scenario);

    World released_world(6, 6);
    DamBreakScenario released_scenario;
    initializeDamBreakWorld(released_world, released_scenario);
    released_scenario.tick(released_world, kDt);
    released_world.advanceTime(kDt);

    World frame_30_world(6, 6);
    DamBreakScenario frame_30_scenario;
    initializeDamBreakWorld(frame_30_world, frame_30_scenario);
    for (int step = 0; step < 30; ++step) {
        frame_30_scenario.tick(frame_30_world, kDt);
        frame_30_world.advanceTime(kDt);
    }

    World settled_world(6, 6);
    DamBreakScenario settled_scenario;
    initializeDamBreakWorld(settled_world, settled_scenario);
    for (int step = 0; step < kWaterSettleSteps; ++step) {
        settled_scenario.tick(settled_world, kDt);
        settled_world.advanceTime(kDt);
    }

    std::ostringstream out;
    out << "Dam break diagnostics\n";
    out << "Initial frame\n";
    out << initial_world.toAsciiDiagram() << "\n";
    out << "After first released frame\n";
    out << released_world.toAsciiDiagram() << "\n";
    out << "After 30 frames\n";
    out << frame_30_world.toAsciiDiagram() << "\n";
    out << "After settle\n";
    out << settled_world.toAsciiDiagram() << "\n";
    out << "Water columns after settle\n";
    out << dumpMaterialColumnCounts(settled_world, Material::EnumType::Water);
    out << "Water rows after settle\n";
    out << dumpMaterialRowCounts(settled_world, Material::EnumType::Water);
    return out.str();
}

std::string dumpSandboxGravityContactDiagnostics()
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "gravity_contact_only",
        .gravity = defaults.gravity,
        .pressure = false,
        .cohesion = false,
        .adhesion = false,
        .friction = false,
        .viscosity = false,
        .air_resistance = false,
        .swap = false,
    };

    World world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(world);
    applyForceIsolationProfile(world, profile);
    settleWorld(world);

    const GridOfCells grid = makeGrid(world);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(world, grid);
    const std::vector<NamedRegion> tracked_regions = selectSandboxDiagnosticRegions(buckets);
    const std::vector<RegionCoord> tracked_region_coords =
        extractNamedRegionCoords(tracked_regions);

    std::ostringstream out;
    out << "Sandbox gravity-contact diagnostics\n";
    out << dumpSandboxQuadrantDiagnostics(profile) << "\n";
    out << dumpNamedRegionList(tracked_regions) << "\n";
    out << dumpBuriedRegionCellMaps(world, tracked_region_coords) << "\n";
    out << dumpBuriedRegionCellDetails(world, tracked_region_coords) << "\n";
    out << dumpSandboxTrackedRegionTimeSeries(world, tracked_regions);
    return out.str();
}

std::string dumpSandboxCohesionAdhesionFrictionDiagnostics()
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "gravity_cohesion_adhesion_friction_only",
        .gravity = defaults.gravity,
        .pressure = false,
        .viscosity = false,
        .air_resistance = false,
        .swap = false,
    };

    World world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(world);
    applyForceIsolationProfile(world, profile);
    settleWorld(world);

    const GridOfCells grid = makeGrid(world);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(world, grid);
    const std::vector<NamedRegion> tracked_regions = selectSandboxDiagnosticRegions(buckets);
    const std::vector<RegionCoord> tracked_region_coords =
        extractNamedRegionCoords(tracked_regions);

    std::ostringstream out;
    out << "Sandbox cohesion-adhesion-friction diagnostics\n";
    out << dumpSandboxQuadrantDiagnostics(profile) << "\n";
    out << dumpNamedRegionList(tracked_regions) << "\n";
    out << dumpBuriedRegionCellMaps(world, tracked_region_coords) << "\n";
    out << dumpBuriedRegionCellDetails(world, tracked_region_coords) << "\n";
    out << dumpSandboxTrackedRegionTimeSeries(world, tracked_regions);
    return out.str();
}

std::string dumpSandboxWallBackedInteriorDiagnostics()
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "full",
        .gravity = defaults.gravity,
    };

    World world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(world);
    applyForceIsolationProfile(world, profile);
    settleWorld(world);

    const GridOfCells grid = makeGrid(world);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(world, grid);
    auto sorted_interiors = buckets.interior_dirt_regions;
    auto sorted_exposed = buckets.exposed_dirt_regions;
    const auto sort_regions = [](std::vector<RegionCoord>& regions) {
        std::sort(regions.begin(), regions.end(), [](const RegionCoord& a, const RegionCoord& b) {
            return std::tie(a.y, a.x) < std::tie(b.y, b.x);
        });
    };
    sort_regions(sorted_interiors);
    sort_regions(sorted_exposed);

    std::vector<NamedRegion> tracked_regions;
    if (!sorted_interiors.empty()) {
        const int max_interior_x =
            std::max_element(
                sorted_interiors.begin(),
                sorted_interiors.end(),
                [](const RegionCoord& a, const RegionCoord& b) { return a.x < b.x; })
                ->x;
        const int max_interior_y =
            std::max_element(
                sorted_interiors.begin(),
                sorted_interiors.end(),
                [](const RegionCoord& a, const RegionCoord& b) { return a.y < b.y; })
                ->y;
        const auto find_interior = [&](int x, int y) -> std::optional<RegionCoord> {
            const auto it = std::find_if(
                sorted_interiors.begin(),
                sorted_interiors.end(),
                [x, y](const RegionCoord& region) { return region.x == x && region.y == y; });
            if (it == sorted_interiors.end()) {
                return std::nullopt;
            }

            return *it;
        };

        addNamedRegionIfUnique(
            tracked_regions,
            "bottom_left_wall_backed",
            find_interior(max_interior_x - 2, max_interior_y));
        addNamedRegionIfUnique(
            tracked_regions,
            "bottom_mid_wall_backed",
            find_interior(max_interior_x - 1, max_interior_y));
        addNamedRegionIfUnique(
            tracked_regions, "right_edge_upper", find_interior(max_interior_x, max_interior_y - 1));
        addNamedRegionIfUnique(
            tracked_regions, "right_edge_lower", find_interior(max_interior_x, max_interior_y));

        std::optional<RegionCoord> top_face_right;
        for (const RegionCoord& region : sorted_exposed) {
            if (region.x == max_interior_x && region.y < max_interior_y) {
                top_face_right = region;
                break;
            }
        }
        addNamedRegionIfUnique(tracked_regions, "top_face_right", top_face_right);

        std::optional<RegionCoord> left_exposed_lower;
        for (const RegionCoord& region : sorted_exposed) {
            if (region.y == max_interior_y && region.x < max_interior_x - 1) {
                left_exposed_lower = region;
                break;
            }
        }
        addNamedRegionIfUnique(tracked_regions, "left_exposed_lower", left_exposed_lower);
    }

    const std::vector<RegionCoord> tracked_region_coords =
        extractNamedRegionCoords(tracked_regions);

    std::ostringstream out;
    out << "Sandbox wall-backed interior diagnostics\n";
    out << dumpSandboxQuadrantDiagnostics(profile) << "\n";
    out << dumpNamedRegionList(tracked_regions) << "\n";
    out << dumpBuriedRegionCellMaps(world, tracked_region_coords) << "\n";
    out << dumpBuriedRegionCellDetails(world, tracked_region_coords) << "\n";
    out << dumpSandboxTrackedRegionTimeSeries(world, tracked_regions);
    return out.str();
}

std::string dumpSandboxQuadrantLongRunRegionTransitions()
{
    constexpr int kLongRunFrames = 96;

    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "full",
        .gravity = defaults.gravity,
    };

    World replay(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(replay);
    applyForceIsolationProfile(replay, profile);
    settleWorld(replay);

    const GridOfCells initialGrid = makeGrid(replay);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(replay, initialGrid);
    auto dirt_regions = buckets.dirt_regions;
    std::sort(
        dirt_regions.begin(), dirt_regions.end(), [](const RegionCoord& a, const RegionCoord& b) {
            return std::tie(a.y, a.x) < std::tie(b.y, b.x);
        });

    std::ostringstream out;
    out << "Sandbox long-run region transitions\n";
    out << "Profile: " << profile.name << "\n";
    out << "Tracked dirt regions:";
    for (const RegionCoord& region : dirt_regions) {
        out << " (" << region.x << "," << region.y << ")";
    }
    out << "\n";
    out << "frame quietInterior sleepingInterior states\n";

    for (int frame = 0; frame <= kLongRunFrames; ++frame) {
        const GridOfCells grid = makeGrid(replay);
        const SandboxRegionBuckets frameBuckets = classifySandboxDirtRegions(replay, grid);
        int quietInterior = 0;
        int sleepingInterior = 0;

        for (const RegionCoord& region : frameBuckets.interior_dirt_regions) {
            const RegionState state =
                replay.getRegionActivityTracker().getRegionState(region.x, region.y);
            if (state == RegionState::Sleeping) {
                quietInterior++;
                sleepingInterior++;
            }
        }

        out << frame << " " << quietInterior << " " << sleepingInterior;
        for (const RegionCoord& region : dirt_regions) {
            const RegionFrameSample sample = sampleRegionFrame(replay, region, frame);
            out << " (" << region.x << "," << region.y << "):" << regionStateToChar(sample.state)
                << wakeReasonToChar(sample.wake_reason) << ":" << sample.max_velocity;
        }
        out << "\n";

        if (frame < kLongRunFrames) {
            replay.advanceTime(kDt);
        }
    }

    return out.str();
}

std::optional<RegionCoord> findPureBuriedRegion(
    const World& world, const std::vector<RegionCoord>& buried_regions)
{
    const WorldRegionActivityTracker& tracker = world.getRegionActivityTracker();
    for (const RegionCoord& region : buried_regions) {
        if (!tracker.getRegionSummary(region.x, region.y).has_mixed_material) {
            return region;
        }
    }

    return std::nullopt;
}

std::string dumpOptionalPureBuriedRegionTimeSeries(
    const World& world, const std::vector<RegionCoord>& buried_regions)
{
    const std::optional<RegionCoord> pureRegion = findPureBuriedRegion(world, buried_regions);
    if (!pureRegion.has_value()) {
        return "Pure buried region time series\n<no pure buried region>";
    }

    return dumpPureBuriedRegionTimeSeries(world, *pureRegion);
}

bool regionQualifiesForTransferQuietHeuristic(
    const RegionFrameSample& sample,
    const RegionSummary& summary,
    const TransferQuietHeuristicConfig& config)
{
    if (summary.has_empty_adjacency || summary.has_water_adjacency || summary.has_mixed_material
        || summary.has_organism) {
        return false;
    }

    if (sample.successful_outgoing_transfers > 0 || sample.successful_incoming_transfers > 0) {
        return false;
    }

    if (sample.mean_velocity > config.max_mean_velocity
        || sample.max_velocity > config.max_max_velocity) {
        return false;
    }

    if (summary.max_live_pressure_delta > config.max_live_pressure_delta
        || summary.max_static_load_delta > config.max_static_load_delta) {
        return false;
    }

    return true;
}

TransferQuietExperimentResult runTransferQuietExperiment(
    World& world,
    const std::vector<RegionCoord>& buried_regions,
    const TransferQuietHeuristicConfig& config,
    int observation_frames = kDiagnosticFrames)
{
    TransferQuietExperimentResult result;
    result.pure_region = findPureBuriedRegion(world, buried_regions);
    std::vector<int> quiet_streaks(buried_regions.size(), 0);

    for (const RegionCoord& region : buried_regions) {
        result.regions.push_back(TransferQuietRegionObservation{ .region = region });
    }

    for (int frame = 0; frame <= observation_frames; ++frame) {
        const WorldRegionActivityTracker& tracker = world.getRegionActivityTracker();

        for (size_t i = 0; i < buried_regions.size(); ++i) {
            const RegionCoord& region = buried_regions[i];
            const RegionSummary& summary = tracker.getRegionSummary(region.x, region.y);
            const RegionFrameSample sample = sampleRegionFrame(world, region, frame);
            const bool qualifies =
                regionQualifiesForTransferQuietHeuristic(sample, summary, config);

            if (qualifies) {
                quiet_streaks[i]++;
            }
            else {
                quiet_streaks[i] = 0;
            }

            TransferQuietRegionObservation& observation = result.regions[i];
            observation.max_generated_moves = std::max(
                observation.max_generated_moves, sample.generated_moves + sample.received_moves);
            observation.max_quiet_streak = std::max(observation.max_quiet_streak, quiet_streaks[i]);
            observation.max_successful_transfers = std::max(
                observation.max_successful_transfers,
                sample.successful_outgoing_transfers + sample.successful_incoming_transfers);

            if (!observation.ever_candidate && quiet_streaks[i] >= config.quiet_frames_required) {
                observation.ever_candidate = true;
                observation.first_candidate_frame = frame;
            }

            if (result.pure_region.has_value() && region.x == result.pure_region->x
                && region.y == result.pure_region->y) {
                result.pure_region_frames.push_back(
                    TransferQuietFrameSample{
                        .frame = frame,
                        .qualifies = qualifies,
                        .quiet_streak = quiet_streaks[i],
                        .sample = sample,
                        .summary = summary,
                    });
            }
        }

        if (frame < observation_frames) {
            world.advanceTime(kDt);
        }
    }

    return result;
}

int countTransferQuietCandidateRegions(const TransferQuietExperimentResult& result)
{
    int count = 0;
    for (const TransferQuietRegionObservation& observation : result.regions) {
        if (observation.ever_candidate) {
            count++;
        }
    }
    return count;
}

const TransferQuietRegionObservation* findTransferQuietObservation(
    const TransferQuietExperimentResult& result, RegionCoord region)
{
    for (const TransferQuietRegionObservation& observation : result.regions) {
        if (observation.region.x == region.x && observation.region.y == region.y) {
            return &observation;
        }
    }

    return nullptr;
}

std::string dumpTransferQuietExperiment(
    const TransferQuietExperimentResult& result, const TransferQuietHeuristicConfig& config)
{
    std::ostringstream out;
    out << "Transfer-quiet heuristic\n";
    out << "quiet_frames=" << config.quiet_frames_required << " meanVel<"
        << config.max_mean_velocity << " maxVel<" << config.max_max_velocity << " dP<"
        << config.max_live_pressure_delta << " dL<" << config.max_static_load_delta << "\n";
    out << "Regions\n";
    for (const TransferQuietRegionObservation& observation : result.regions) {
        out << "  (" << observation.region.x << "," << observation.region.y << ")"
            << " candidate=" << observation.ever_candidate
            << " first=" << observation.first_candidate_frame
            << " maxStreak=" << observation.max_quiet_streak
            << " maxMoves=" << observation.max_generated_moves
            << " maxSuccess=" << observation.max_successful_transfers << "\n";
    }

    if (!result.pure_region.has_value()) {
        out << "Pure region\n<none>\n";
        return out.str();
    }

    out << "Pure region (" << result.pure_region->x << "," << result.pure_region->y << ")\n";
    out << "frame qualifies streak gen recv okOut okIn meanV maxV meanForceY dP dL empty water "
           "mixed org\n";
    for (const TransferQuietFrameSample& frame : result.pure_region_frames) {
        out << frame.frame << " " << frame.qualifies << " " << frame.quiet_streak << " "
            << frame.sample.generated_moves << " " << frame.sample.received_moves << " "
            << frame.sample.successful_outgoing_transfers << " "
            << frame.sample.successful_incoming_transfers << " " << frame.sample.mean_velocity
            << " " << frame.sample.max_velocity << " " << frame.sample.mean_abs_pressure_force_y
            << " " << frame.summary.max_live_pressure_delta << " "
            << frame.summary.max_static_load_delta << " " << frame.summary.has_empty_adjacency
            << " " << frame.summary.has_water_adjacency << " " << frame.summary.has_mixed_material
            << " " << frame.summary.has_organism << "\n";
    }

    return out.str();
}

char regionStateToChar(RegionState state)
{
    switch (state) {
        case RegionState::Awake:
            return 'A';
        case RegionState::LoadedQuiet:
            return 'Q';
        case RegionState::Sleeping:
            return 'S';
    }

    return '?';
}

char wakeReasonToChar(WakeReason reason)
{
    switch (reason) {
        case WakeReason::None:
            return '.';
        case WakeReason::ExternalMutation:
            return 'X';
        case WakeReason::Move:
            return 'M';
        case WakeReason::BlockedTransfer:
            return 'B';
        case WakeReason::NeighborTopologyChanged:
            return 'N';
        case WakeReason::GravityChanged:
            return 'G';
        case WakeReason::WaterInterface:
            return 'W';
    }

    return '?';
}

ForceIsolationObservation runForceIsolationProfile(const ForceIsolationProfile& profile)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);
    applyForceIsolationProfile(world, profile);

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);
    const TransferQuietHeuristicConfig config{};
    const TransferQuietExperimentResult experiment =
        runTransferQuietExperiment(world, buried_regions, config);

    ForceIsolationObservation observation{ .name = profile.name };
    observation.quiet_buried_regions = countQuietBuriedRegions(world, buried_regions);
    observation.transfer_quiet_candidate_regions = countTransferQuietCandidateRegions(experiment);

    if (!experiment.pure_region.has_value()) {
        return observation;
    }

    observation.pure_region_found = true;

    const TransferQuietRegionObservation* pure_region_observation =
        findTransferQuietObservation(experiment, *experiment.pure_region);
    if (pure_region_observation != nullptr) {
        observation.pure_region_candidate = pure_region_observation->ever_candidate;
        observation.pure_region_first_candidate_frame =
            pure_region_observation->first_candidate_frame;
        observation.pure_region_max_generated_moves = pure_region_observation->max_generated_moves;
    }

    if (experiment.pure_region_frames.empty()) {
        return observation;
    }

    const TransferQuietFrameSample& final_frame = experiment.pure_region_frames.back();
    observation.final_state = final_frame.sample.state;
    observation.final_wake_reason = final_frame.sample.wake_reason;
    observation.final_mean_velocity = final_frame.sample.mean_velocity;
    observation.final_max_velocity = final_frame.sample.max_velocity;
    observation.final_mean_abs_pressure_gradient_y =
        final_frame.sample.mean_abs_pressure_gradient_y;
    observation.final_mean_abs_pressure_force_y = final_frame.sample.mean_abs_pressure_force_y;
    observation.final_mean_pressure = final_frame.sample.mean_pressure;

    return observation;
}

std::string dumpForceIsolationProfileDiagnostics(const ForceIsolationProfile& profile)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);
    applyForceIsolationProfile(world, profile);

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);
    const TransferQuietHeuristicConfig config{};
    const TransferQuietExperimentResult experiment =
        runTransferQuietExperiment(world, buried_regions, config);

    std::ostringstream out;
    out << "Profile: " << profile.name << "\n";
    out << dumpSettlingDiagnostics(world, grid, buried_regions) << "\n";
    out << dumpTransferQuietExperiment(experiment, config);
    if (experiment.pure_region.has_value()) {
        out << "\n"
            << dumpPureBuriedRegionInitialTimeSeries(world, *experiment.pure_region) << "\n";
    }

    return out.str();
}

std::string dumpForceIsolationSweep()
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const std::vector<ForceIsolationProfile> profiles{
        ForceIsolationProfile{
            .name = "full",
            .gravity = defaults.gravity,
        },
        ForceIsolationProfile{
            .name = "zero_force_baseline",
            .gravity = 0.0,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_contact_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_contact_swap",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
        },
        ForceIsolationProfile{
            .name = "pressure_only",
            .gravity = 0.0,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "gravity_pressure_only",
            .gravity = defaults.gravity,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "cohesion_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .adhesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "adhesion_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .friction = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "friction_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .viscosity = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "viscosity_only",
            .gravity = defaults.gravity,
            .pressure = false,
            .cohesion = false,
            .adhesion = false,
            .friction = false,
            .air_resistance = false,
            .swap = false,
        },
        ForceIsolationProfile{
            .name = "full_minus_pressure",
            .gravity = defaults.gravity,
            .pressure = false,
        },
    };

    std::ostringstream out;
    out << "Force isolation sweep\n";
    out << "profile quietRegions candidateRegions pureFound pureCandidate firstCandidate "
           "pureMaxMoves finalState finalWake meanV maxV meanGradY meanForceY meanP\n";

    for (const ForceIsolationProfile& profile : profiles) {
        const ForceIsolationObservation observation = runForceIsolationProfile(profile);
        out << observation.name << " " << observation.quiet_buried_regions << " "
            << observation.transfer_quiet_candidate_regions << " " << observation.pure_region_found
            << " " << observation.pure_region_candidate << " "
            << observation.pure_region_first_candidate_frame << " "
            << observation.pure_region_max_generated_moves << " "
            << regionStateToChar(observation.final_state) << " "
            << wakeReasonToChar(observation.final_wake_reason) << " "
            << observation.final_mean_velocity << " " << observation.final_max_velocity << " "
            << observation.final_mean_abs_pressure_gradient_y << " "
            << observation.final_mean_abs_pressure_force_y << " " << observation.final_mean_pressure
            << "\n";
    }

    return out.str();
}

std::string dumpRegionStates(const World& world)
{
    const WorldData& data = world.getData();
    std::ostringstream out;

    out << "Region states\n";
    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            const int idx = block_y * data.region_debug_blocks_x + block_x;
            char state = regionStateToChar(static_cast<RegionState>(data.region_debug[idx].state));
            if (!regionContainsMaterial(world, block_x, block_y)) {
                state = static_cast<char>(std::tolower(state));
            }
            out << state;
        }
        out << '\n';
    }

    return out.str();
}

std::string dumpWakeReasons(const World& world)
{
    const WorldData& data = world.getData();
    std::ostringstream out;

    out << "Wake reasons\n";
    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            const int idx = block_y * data.region_debug_blocks_x + block_x;
            char reason =
                wakeReasonToChar(static_cast<WakeReason>(data.region_debug[idx].wake_reason));
            if (!regionContainsMaterial(world, block_x, block_y)) {
                reason = static_cast<char>(std::tolower(reason));
            }
            out << reason;
        }
        out << '\n';
    }

    return out.str();
}

std::string dumpBuriedExposureMap(const World& world, const GridOfCells& grid)
{
    const WorldData& data = world.getData();
    std::ostringstream out;

    out << "Buried map\n";
    for (int block_y = 0; block_y < data.region_debug_blocks_y; ++block_y) {
        for (int block_x = 0; block_x < data.region_debug_blocks_x; ++block_x) {
            if (!regionContainsMaterial(world, block_x, block_y)) {
                out << '.';
            }
            else if (regionHasEmptyAdjacency(world, grid, block_x, block_y)) {
                out << 'E';
            }
            else {
                out << 'B';
            }
        }
        out << '\n';
    }

    return out.str();
}

std::string dumpBuriedRegionDetails(
    const World& world, const std::vector<RegionCoord>& buried_regions)
{
    const WorldData& data = world.getData();
    const WorldRegionActivityTracker& tracker = world.getRegionActivityTracker();
    std::ostringstream out;

    out << "Buried region details\n";
    for (const RegionCoord& region : buried_regions) {
        const int idx = region.y * data.region_debug_blocks_x + region.x;
        const RegionState state = static_cast<RegionState>(data.region_debug[idx].state);
        const WakeReason wake_reason = static_cast<WakeReason>(data.region_debug[idx].wake_reason);
        const RegionSummary& summary = tracker.getRegionSummary(region.x, region.y);

        out << "(" << region.x << "," << region.y << ")"
            << " state=" << regionStateToChar(state) << " wake=" << wakeReasonToChar(wake_reason)
            << " vel=" << summary.max_velocity << " dP=" << summary.max_live_pressure_delta
            << " dL=" << summary.max_static_load_delta << " empty=" << summary.has_empty_adjacency
            << " water=" << summary.has_water_adjacency << " mixed=" << summary.has_mixed_material
            << " organism=" << summary.has_organism << " touched=" << summary.touched_this_frame
            << "\n";
    }

    return out.str();
}

char gravityStateToChar(const Cell& cell, const CellDebug& debug)
{
    if (cell.isEmpty()) {
        return '.';
    }
    if (!isLoadBearingGranularCell(cell)) {
        return '#';
    }
    if (debug.gravity_skipped_for_support) {
        return 'S';
    }
    if (debug.has_granular_support_path && debug.carries_transmitted_granular_load) {
        return 'B';
    }
    if (debug.has_granular_support_path) {
        return 'P';
    }
    if (debug.carries_transmitted_granular_load) {
        return 'L';
    }
    if (debug.accumulated_gravity_force.magnitude() > 0.001) {
        return 'G';
    }
    return '.';
}

char moveStateToChar(const CellDebug& debug)
{
    const bool generated = debug.generated_move_count > 0;
    const bool received = debug.received_move_count > 0;

    if (generated && received) {
        return '*';
    }
    if (generated) {
        return '>';
    }
    if (received) {
        return '<';
    }
    return '.';
}

bool hasHorizontalDirection(uint8_t direction_mask)
{
    return (direction_mask & (CellDebug::DirectionLeft | CellDebug::DirectionRight)) != 0;
}

bool hasVerticalDirection(uint8_t direction_mask)
{
    return (direction_mask & (CellDebug::DirectionUp | CellDebug::DirectionDown)) != 0;
}

bool hasCompressionBranch(uint8_t branch_mask, uint8_t branch)
{
    return (branch_mask & branch) != 0;
}

double totalPressureInjectionAmount(const CellDebug& debug)
{
    return debug.dynamic_pressure_reflection_injection_amount
        + debug.dynamic_pressure_target_injection_amount
        + debug.excess_move_pressure_injection_amount + debug.hydrostatic_pressure_injection_amount;
}

char pressureInjectionStateToChar(const CellDebug& debug);

WorldPressureSourceFrameSample sampleWorldPressureSourceFrame(const World& world, int frame)
{
    const WorldData& data = world.getData();
    const GridOfCells grid = makeGrid(const_cast<World&>(world));
    WorldPressureSourceFrameSample sample{ .frame = frame };

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const Cell& cell = data.at(x, y);
            const CellDebug& debug = data.debug_info[idx];
            const double source_amount = totalPressureInjectionAmount(debug);

            if (source_amount < 0.0001) {
                continue;
            }

            sample.source_cells++;
            if (cellHasEmptyAdjacency(grid, x, y)) {
                sample.surface_adjacent_source_cells++;
            }
            else {
                sample.buried_source_cells++;
            }

            if (debug.hydrostatic_pressure_injection_count > 0) {
                sample.hydrostatic_source_cells++;
            }
            if (debug.dynamic_pressure_target_injection_count > 0) {
                sample.dynamic_target_source_cells++;
            }
            if (debug.dynamic_pressure_reflection_injection_count > 0) {
                sample.dynamic_reflection_source_cells++;
            }
            if (debug.excess_move_pressure_injection_count > 0) {
                sample.excess_move_source_cells++;
            }

            sample.hydrostatic_source_events += debug.hydrostatic_pressure_injection_count;
            sample.dynamic_target_source_events += debug.dynamic_pressure_target_injection_count;
            sample.dynamic_reflection_source_events +=
                debug.dynamic_pressure_reflection_injection_count;
            sample.excess_move_source_events += debug.excess_move_pressure_injection_count;
            sample.hydrostatic_source_amount += debug.hydrostatic_pressure_injection_amount;
            sample.dynamic_target_source_amount += debug.dynamic_pressure_target_injection_amount;
            sample.dynamic_reflection_source_amount +=
                debug.dynamic_pressure_reflection_injection_amount;
            sample.excess_move_source_amount += debug.excess_move_pressure_injection_amount;

            if (sample.max_x < sample.min_x || sample.max_y < sample.min_y) {
                sample.min_x = x;
                sample.max_x = x;
                sample.min_y = y;
                sample.max_y = y;
            }
            else {
                sample.min_x = std::min(sample.min_x, x);
                sample.max_x = std::max(sample.max_x, x);
                sample.min_y = std::min(sample.min_y, y);
                sample.max_y = std::max(sample.max_y, y);
            }

            if (source_amount > sample.strongest_source_amount) {
                sample.strongest_source_amount = source_amount;
                sample.strongest_source_x = x;
                sample.strongest_source_y = y;
                sample.strongest_source_pressure = cell.pressure;
                sample.strongest_source_gradient_y = cell.pressure_gradient.y;
                sample.strongest_source_force_y = debug.accumulated_pressure_force.y;
            }
        }
    }

    return sample;
}

std::string dumpFramePressureSourceHotspots(const World& world, int limit = 8)
{
    struct PressureSourceCell {
        int x = 0;
        int y = 0;
        bool surface_adjacent = false;
        double amount = 0.0;
    };

    const WorldData& data = world.getData();
    const GridOfCells grid = makeGrid(const_cast<World&>(world));
    std::vector<PressureSourceCell> source_cells;

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const CellDebug& debug = data.debug_info[idx];
            const double amount = totalPressureInjectionAmount(debug);
            if (amount < 0.0001) {
                continue;
            }

            source_cells.push_back(
                PressureSourceCell{
                    .x = x,
                    .y = y,
                    .surface_adjacent = cellHasEmptyAdjacency(grid, x, y),
                    .amount = amount,
                });
        }
    }

    std::sort(
        source_cells.begin(),
        source_cells.end(),
        [](const PressureSourceCell& a, const PressureSourceCell& b) {
            return std::tie(a.amount, a.y, a.x) > std::tie(b.amount, b.y, b.x);
        });

    std::ostringstream out;
    const int count = std::min(limit, static_cast<int>(source_cells.size()));
    for (int i = 0; i < count; ++i) {
        const PressureSourceCell& source = source_cells[static_cast<size_t>(i)];
        const size_t idx = static_cast<size_t>(source.y) * data.width + source.x;
        const Cell& cell = data.at(source.x, source.y);
        const CellDebug& debug = data.debug_info[idx];
        out << "  (" << source.x << "," << source.y
            << ") src=" << pressureInjectionStateToChar(debug) << " amt=" << source.amount
            << " surf=" << source.surface_adjacent << " mat=" << toString(cell.material_type)
            << " p=" << cell.pressure << " gradY=" << cell.pressure_gradient.y
            << " pForceY=" << debug.accumulated_pressure_force.y << "\n";
    }

    if (count == 0) {
        out << "  <none>\n";
    }

    return out.str();
}

char candidateAxisStateToChar(uint16_t count, uint8_t direction_mask)
{
    if (count == 0) {
        return '.';
    }

    const bool horizontal = hasHorizontalDirection(direction_mask);
    const bool vertical = hasVerticalDirection(direction_mask);
    if (horizontal && vertical) {
        return '*';
    }
    if (horizontal) {
        return 'H';
    }
    if (vertical) {
        return 'V';
    }
    return 'C';
}

std::string directionMaskToString(uint8_t direction_mask)
{
    std::string out;
    if ((direction_mask & CellDebug::DirectionLeft) != 0) {
        out.push_back('L');
    }
    if ((direction_mask & CellDebug::DirectionRight) != 0) {
        out.push_back('R');
    }
    if ((direction_mask & CellDebug::DirectionUp) != 0) {
        out.push_back('U');
    }
    if ((direction_mask & CellDebug::DirectionDown) != 0) {
        out.push_back('D');
    }

    if (out.empty()) {
        out = ".";
    }

    return out;
}

std::string compressionBranchMaskToString(uint8_t branch_mask)
{
    std::string out;
    if (hasCompressionBranch(branch_mask, CellDebug::CompressionBranchGranular)) {
        out.push_back('G');
    }
    if (hasCompressionBranch(branch_mask, CellDebug::CompressionBranchSupport)) {
        out.push_back('S');
    }

    if (out.empty()) {
        out = ".";
    }

    return out;
}

char moveAxisStateToChar(const CellDebug& debug)
{
    const uint8_t direction_mask =
        debug.generated_move_direction_mask | debug.received_move_direction_mask;
    return candidateAxisStateToChar(
        static_cast<uint16_t>(debug.generated_move_count + debug.received_move_count),
        direction_mask);
}

char transferStateToChar(const CellDebug& debug)
{
    const bool successfulOutgoing = debug.successful_outgoing_transfer_count > 0;
    const bool successfulIncoming = debug.successful_incoming_transfer_count > 0;
    const bool blockedOutgoing = debug.blocked_outgoing_transfer_count > 0;

    if (successfulOutgoing && blockedOutgoing) {
        return 'P';
    }
    if (successfulOutgoing && successfulIncoming) {
        return 'X';
    }
    if (blockedOutgoing) {
        return 'B';
    }
    if (successfulOutgoing) {
        return 'S';
    }
    if (successfulIncoming) {
        return 'I';
    }
    return '.';
}

char pressureInjectionStateToChar(const CellDebug& debug)
{
    const bool hydrostatic = debug.hydrostatic_pressure_injection_count > 0;
    const bool dynamic_target = debug.dynamic_pressure_target_injection_count > 0;
    const bool dynamic_reflection = debug.dynamic_pressure_reflection_injection_count > 0;
    const bool excess_move = debug.excess_move_pressure_injection_count > 0;
    const int kind_count = static_cast<int>(hydrostatic) + static_cast<int>(dynamic_target)
        + static_cast<int>(dynamic_reflection) + static_cast<int>(excess_move);

    if (kind_count == 0) {
        return '.';
    }
    if (kind_count > 1) {
        return 'M';
    }
    if (hydrostatic) {
        return 'H';
    }
    if (dynamic_target) {
        return 'T';
    }
    if (excess_move) {
        return 'E';
    }

    return 'R';
}

char staticLoadBucketToChar(const Cell& cell, double gravity)
{
    if (!isLoadBearingGranularCell(cell)) {
        return '.';
    }

    const double self_weight = cell.getMass() * std::abs(gravity);
    if (self_weight < 0.0001) {
        return '0';
    }

    const double ratio = cell.static_load / self_weight;
    const int bucket = std::clamp(static_cast<int>(std::round(std::min(ratio, 9.0))), 0, 9);
    return static_cast<char>('0' + bucket);
}

char pressureInjectionBucketToChar(const CellDebug& debug)
{
    const double amount = totalPressureInjectionAmount(debug);
    if (amount < 0.0001) {
        return '.';
    }
    if (amount < 0.001) {
        return '1';
    }
    if (amount < 0.005) {
        return '2';
    }
    if (amount < 0.01) {
        return '3';
    }
    if (amount < 0.02) {
        return '4';
    }
    if (amount < 0.05) {
        return '5';
    }
    if (amount < 0.10) {
        return '6';
    }
    if (amount < 0.20) {
        return '7';
    }
    if (amount < 0.40) {
        return '8';
    }
    return '9';
}

char velocityBucketToChar(double velocity)
{
    if (velocity < 0.01) {
        return '0';
    }
    if (velocity < 0.05) {
        return '1';
    }
    if (velocity < 0.10) {
        return '2';
    }
    if (velocity < 0.25) {
        return '3';
    }
    if (velocity < 0.50) {
        return '4';
    }
    if (velocity < 1.00) {
        return '5';
    }
    if (velocity < 2.00) {
        return '6';
    }
    if (velocity < 4.00) {
        return '7';
    }
    if (velocity < 8.00) {
        return '8';
    }
    return '9';
}

std::string dumpBuriedRegionCellMaps(
    const World& world, const std::vector<RegionCoord>& buried_regions)
{
    const WorldData& data = world.getData();
    const double gravity = world.getPhysicsSettings().gravity;
    std::ostringstream out;

    out << "Buried region cell maps\n";
    for (const RegionCoord& region : buried_regions) {
        out << "Region (" << region.x << "," << region.y << ")\n";
        const int x_begin = region.x * kRegionSize;
        const int y_begin = region.y * kRegionSize;
        const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
        const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);

        out << "  velocity\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const Cell& cell = data.at(x, y);
                out << velocityBucketToChar(cell.velocity.magnitude());
            }
            out << '\n';
        }

        out << "  gravity\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                out << gravityStateToChar(data.at(x, y), data.debug_info[idx]);
            }
            out << '\n';
        }

        out << "  moves\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                out << moveStateToChar(data.debug_info[idx]);
            }
            out << '\n';
        }

        out << "  moveAxis\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                out << moveAxisStateToChar(data.debug_info[idx]);
            }
            out << '\n';
        }

        out << "  transfer\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                out << transferStateToChar(data.debug_info[idx]);
            }
            out << '\n';
        }

        out << "  gCand\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                const CellDebug& debug = data.debug_info[idx];
                out << candidateAxisStateToChar(
                    debug.gravity_compression_candidate_count,
                    debug.gravity_compression_candidate_direction_mask);
            }
            out << '\n';
        }

        out << "  jamCand\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                const CellDebug& debug = data.debug_info[idx];
                out << candidateAxisStateToChar(
                    debug.jammed_contact_candidate_count,
                    debug.jammed_contact_candidate_direction_mask);
            }
            out << '\n';
        }

        out << "  pSrc\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                out << pressureInjectionStateToChar(data.debug_info[idx]);
            }
            out << '\n';
        }

        out << "  pAmt\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                out << pressureInjectionBucketToChar(data.debug_info[idx]);
            }
            out << '\n';
        }

        out << "  load\n";
        for (int y = y_begin; y < y_end; ++y) {
            out << "  ";
            for (int x = x_begin; x < x_end; ++x) {
                out << staticLoadBucketToChar(data.at(x, y), gravity);
            }
            out << '\n';
        }
    }

    return out.str();
}

std::string dumpBuriedRegionCellDetails(
    const World& world, const std::vector<RegionCoord>& buried_regions)
{
    const WorldData& data = world.getData();
    const double gravity = world.getPhysicsSettings().gravity;
    std::ostringstream out;

    out << "Buried region cell details\n";
    for (const RegionCoord& region : buried_regions) {
        out << "Region (" << region.x << "," << region.y << ")\n";
        const int x_begin = region.x * kRegionSize;
        const int y_begin = region.y * kRegionSize;
        const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
        const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);
        bool found_interesting_cell = false;

        for (int y = y_begin; y < y_end; ++y) {
            for (int x = x_begin; x < x_end; ++x) {
                const size_t idx = static_cast<size_t>(y) * data.width + x;
                const Cell& cell = data.at(x, y);
                const CellDebug& debug = data.debug_info[idx];
                const double velocity = cell.velocity.magnitude();
                const double pressure_injection_amount = totalPressureInjectionAmount(debug);
                const bool interesting = velocity > 0.05 || debug.generated_move_count > 0
                    || debug.received_move_count > 0 || debug.successful_outgoing_transfer_count > 0
                    || debug.successful_incoming_transfer_count > 0
                    || debug.blocked_outgoing_transfer_count > 0
                    || debug.gravity_compression_candidate_count > 0
                    || debug.incoming_compression_contact_count > 0
                    || debug.jammed_contact_candidate_count > 0
                    || debug.outgoing_compression_contact_count > 0
                    || debug.accumulated_viscous_force.magnitude() > 0.0001
                    || pressure_injection_amount > 0.0001 || debug.gravity_skipped_for_support
                    || debug.has_granular_support_path || debug.carries_transmitted_granular_load;

                if (!interesting) {
                    continue;
                }

                found_interesting_cell = true;
                const double self_weight =
                    isLoadBearingGranularCell(cell) ? cell.getMass() * std::abs(gravity) : 0.0;
                out << "  (" << x << "," << y << ") mat=" << toString(cell.material_type)
                    << " vel=" << velocity << " com=(" << cell.com.x << "," << cell.com.y << ")"
                    << " load=" << cell.static_load << " self=" << self_weight << " vForce=("
                    << debug.accumulated_viscous_force.x << "," << debug.accumulated_viscous_force.y
                    << ")"
                    << " grav=" << gravityStateToChar(cell, debug)
                    << " moves=" << moveStateToChar(debug) << " gen=" << debug.generated_move_count
                    << " outDir=" << directionMaskToString(debug.generated_move_direction_mask)
                    << " recv=" << debug.received_move_count
                    << " inDir=" << directionMaskToString(debug.received_move_direction_mask)
                    << " gCand=" << debug.gravity_compression_candidate_count << ":"
                    << directionMaskToString(debug.gravity_compression_candidate_direction_mask)
                    << " jamCand=" << debug.jammed_contact_candidate_count << ":"
                    << directionMaskToString(debug.jammed_contact_candidate_direction_mask)
                    << " outComp=" << debug.outgoing_compression_contact_count << ":"
                    << compressionBranchMaskToString(debug.outgoing_compression_branch_mask) << ":"
                    << debug.max_outgoing_compression_normal_before << "->"
                    << debug.max_outgoing_compression_normal_after
                    << " inComp=" << debug.incoming_compression_contact_count << ":"
                    << compressionBranchMaskToString(debug.incoming_compression_branch_mask) << ":"
                    << debug.max_incoming_compression_normal_before << "->"
                    << debug.max_incoming_compression_normal_after
                    << " xfer=" << transferStateToChar(debug)
                    << " okOut=" << debug.successful_outgoing_transfer_count
                    << " okIn=" << debug.successful_incoming_transfer_count
                    << " blk=" << debug.blocked_outgoing_transfer_count
                    << " pSrc=" << pressureInjectionStateToChar(debug)
                    << " pHydC=" << debug.hydrostatic_pressure_injection_count
                    << " pDynTC=" << debug.dynamic_pressure_target_injection_count
                    << " pDynRC=" << debug.dynamic_pressure_reflection_injection_count
                    << " pExC=" << debug.excess_move_pressure_injection_count
                    << " okOutAmt=" << debug.successful_outgoing_transfer_amount
                    << " okInAmt=" << debug.successful_incoming_transfer_amount
                    << " blkAmt=" << debug.blocked_outgoing_transfer_amount
                    << " pHydAmt=" << debug.hydrostatic_pressure_injection_amount
                    << " pDynTAmt=" << debug.dynamic_pressure_target_injection_amount
                    << " pDynRAmt=" << debug.dynamic_pressure_reflection_injection_amount
                    << " pExAmt=" << debug.excess_move_pressure_injection_amount << "\n";
            }
        }

        if (!found_interesting_cell) {
            out << "  <none>\n";
        }
    }

    return out.str();
}

RegionFrameSample sampleRegionFrame(const World& world, RegionCoord region, int frame)
{
    const WorldData& data = world.getData();
    const int region_index = region.y * data.region_debug_blocks_x + region.x;
    RegionFrameSample sample{
        .frame = frame,
        .state = static_cast<RegionState>(data.region_debug[region_index].state),
        .wake_reason = static_cast<WakeReason>(data.region_debug[region_index].wake_reason),
    };

    const int x_begin = region.x * kRegionSize;
    const int y_begin = region.y * kRegionSize;
    const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
    const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);

    double velocity_sum = 0.0;
    double abs_velocity_y_sum = 0.0;
    double abs_viscous_force_sum = 0.0;
    double abs_viscous_force_y_sum = 0.0;
    double abs_pressure_gradient_y_sum = 0.0;
    double abs_pressure_force_y_sum = 0.0;
    double pressure_sum = 0.0;

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const Cell& cell = data.at(x, y);
            const CellDebug& debug = data.debug_info[idx];
            if (cell.isEmpty()) {
                continue;
            }

            const double velocity = cell.velocity.magnitude();
            const double abs_velocity_y = std::abs(cell.velocity.y);
            const double abs_viscous_force = debug.accumulated_viscous_force.magnitude();
            const double abs_viscous_force_y = std::abs(debug.accumulated_viscous_force.y);
            const double abs_pressure_gradient_y = std::abs(cell.pressure_gradient.y);
            const double abs_pressure_force_y = std::abs(debug.accumulated_pressure_force.y);

            sample.material_cells++;
            velocity_sum += velocity;
            abs_velocity_y_sum += abs_velocity_y;
            abs_viscous_force_sum += abs_viscous_force;
            abs_viscous_force_y_sum += abs_viscous_force_y;
            abs_pressure_gradient_y_sum += abs_pressure_gradient_y;
            abs_pressure_force_y_sum += abs_pressure_force_y;
            pressure_sum += cell.pressure;
            sample.max_velocity = std::max(sample.max_velocity, velocity);
            sample.max_abs_velocity_y = std::max(sample.max_abs_velocity_y, abs_velocity_y);
            sample.max_abs_viscous_force =
                std::max(sample.max_abs_viscous_force, abs_viscous_force);
            sample.max_abs_viscous_force_y =
                std::max(sample.max_abs_viscous_force_y, abs_viscous_force_y);
            sample.max_abs_pressure_gradient_y =
                std::max(sample.max_abs_pressure_gradient_y, abs_pressure_gradient_y);
            sample.max_abs_pressure_force_y =
                std::max(sample.max_abs_pressure_force_y, abs_pressure_force_y);

            if (velocity > 0.05) {
                sample.moving_cells++;
            }
            if (cell.com.y > 0.95f) {
                sample.near_bottom_cells++;
            }
            if (cell.com.y < -0.95f) {
                sample.near_top_cells++;
            }
            if (debug.gravity_skipped_for_support) {
                sample.gravity_skipped_cells++;
            }
            if (debug.has_granular_support_path) {
                sample.support_path_cells++;
            }
            if (debug.carries_transmitted_granular_load) {
                sample.carrying_load_cells++;
            }
            if (debug.generated_move_count > 0) {
                sample.generated_move_cells++;
            }
            if (hasHorizontalDirection(debug.generated_move_direction_mask)) {
                sample.generated_horizontal_move_cells++;
            }
            if (hasVerticalDirection(debug.generated_move_direction_mask)) {
                sample.generated_vertical_move_cells++;
            }
            if (debug.gravity_compression_candidate_count > 0) {
                sample.gravity_compression_candidate_cells++;
            }
            if (debug.incoming_compression_contact_count > 0) {
                sample.incoming_compression_contact_cells++;
            }
            if (debug.jammed_contact_candidate_count > 0) {
                sample.jammed_contact_candidate_cells++;
            }
            if (debug.outgoing_compression_contact_count > 0) {
                sample.outgoing_compression_contact_cells++;
            }
            if (debug.received_move_count > 0) {
                sample.received_move_cells++;
            }
            if (hasHorizontalDirection(debug.received_move_direction_mask)) {
                sample.received_horizontal_move_cells++;
            }
            if (hasVerticalDirection(debug.received_move_direction_mask)) {
                sample.received_vertical_move_cells++;
            }
            if (debug.hydrostatic_pressure_injection_count > 0) {
                sample.hydrostatic_pressure_injection_cells++;
            }
            if (debug.dynamic_pressure_target_injection_count > 0) {
                sample.dynamic_pressure_target_injection_cells++;
            }
            if (debug.dynamic_pressure_reflection_injection_count > 0) {
                sample.dynamic_pressure_reflection_injection_cells++;
            }
            if (debug.excess_move_pressure_injection_count > 0) {
                sample.excess_move_pressure_injection_cells++;
            }
            if (debug.successful_outgoing_transfer_count > 0) {
                sample.successful_outgoing_transfer_cells++;
            }
            if (debug.successful_incoming_transfer_count > 0) {
                sample.successful_incoming_transfer_cells++;
            }
            if (hasCompressionBranch(
                    debug.incoming_compression_branch_mask | debug.outgoing_compression_branch_mask,
                    CellDebug::CompressionBranchGranular)) {
                sample.granular_compression_contact_cells++;
            }
            if (hasCompressionBranch(
                    debug.incoming_compression_branch_mask | debug.outgoing_compression_branch_mask,
                    CellDebug::CompressionBranchSupport)) {
                sample.support_compression_contact_cells++;
            }
            if (debug.blocked_outgoing_transfer_count > 0) {
                sample.blocked_outgoing_transfer_cells++;
            }
            if (abs_viscous_force > 0.0001) {
                sample.viscous_force_cells++;
            }

            sample.generated_moves += debug.generated_move_count;
            sample.gravity_compression_candidates += debug.gravity_compression_candidate_count;
            sample.incoming_compression_contacts += debug.incoming_compression_contact_count;
            sample.jammed_contact_candidates += debug.jammed_contact_candidate_count;
            sample.outgoing_compression_contacts += debug.outgoing_compression_contact_count;
            sample.received_moves += debug.received_move_count;
            sample.hydrostatic_pressure_injection_events +=
                debug.hydrostatic_pressure_injection_count;
            sample.dynamic_pressure_target_injection_events +=
                debug.dynamic_pressure_target_injection_count;
            sample.dynamic_pressure_reflection_injection_events +=
                debug.dynamic_pressure_reflection_injection_count;
            sample.excess_move_pressure_injection_events +=
                debug.excess_move_pressure_injection_count;
            sample.successful_outgoing_transfers += debug.successful_outgoing_transfer_count;
            sample.successful_incoming_transfers += debug.successful_incoming_transfer_count;
            sample.blocked_outgoing_transfers += debug.blocked_outgoing_transfer_count;
            sample.hydrostatic_pressure_injection_amount +=
                debug.hydrostatic_pressure_injection_amount;
            sample.dynamic_pressure_target_injection_amount +=
                debug.dynamic_pressure_target_injection_amount;
            sample.dynamic_pressure_reflection_injection_amount +=
                debug.dynamic_pressure_reflection_injection_amount;
            sample.excess_move_pressure_injection_amount +=
                debug.excess_move_pressure_injection_amount;
            sample.successful_outgoing_transfer_amount += debug.successful_outgoing_transfer_amount;
            sample.successful_incoming_transfer_amount += debug.successful_incoming_transfer_amount;
            sample.blocked_outgoing_transfer_amount += debug.blocked_outgoing_transfer_amount;
            sample.max_incoming_compression_normal_after = std::max(
                sample.max_incoming_compression_normal_after,
                debug.max_incoming_compression_normal_after);
            sample.max_incoming_compression_normal_before = std::max(
                sample.max_incoming_compression_normal_before,
                debug.max_incoming_compression_normal_before);
            sample.max_outgoing_compression_normal_after = std::max(
                sample.max_outgoing_compression_normal_after,
                debug.max_outgoing_compression_normal_after);
            sample.max_outgoing_compression_normal_before = std::max(
                sample.max_outgoing_compression_normal_before,
                debug.max_outgoing_compression_normal_before);
        }
    }

    if (sample.material_cells > 0) {
        const double material_cells = static_cast<double>(sample.material_cells);
        sample.mean_velocity = velocity_sum / material_cells;
        sample.mean_abs_velocity_y = abs_velocity_y_sum / material_cells;
        sample.mean_abs_viscous_force = abs_viscous_force_sum / material_cells;
        sample.mean_abs_viscous_force_y = abs_viscous_force_y_sum / material_cells;
        sample.mean_abs_pressure_gradient_y = abs_pressure_gradient_y_sum / material_cells;
        sample.mean_abs_pressure_force_y = abs_pressure_force_y_sum / material_cells;
        sample.mean_pressure = pressure_sum / material_cells;
    }

    return sample;
}

std::vector<CellTrack> selectTrackedCells(const World& world, RegionCoord region)
{
    struct Candidate {
        int x = 0;
        int y = 0;
        double score = 0.0;
    };

    const WorldData& data = world.getData();
    const int x_begin = region.x * kRegionSize;
    const int y_begin = region.y * kRegionSize;
    const int x_end = std::min(static_cast<int>(data.width), x_begin + kRegionSize);
    const int y_end = std::min(static_cast<int>(data.height), y_begin + kRegionSize);
    std::vector<Candidate> candidates;

    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const Cell& cell = data.at(x, y);
            const CellDebug& debug = data.debug_info[idx];
            if (cell.isEmpty()) {
                continue;
            }

            const double score = cell.velocity.magnitude() * 100.0
                + static_cast<double>(
                      debug.generated_move_count + debug.received_move_count
                      + debug.gravity_compression_candidate_count
                      + debug.jammed_contact_candidate_count
                      + debug.successful_outgoing_transfer_count
                      + debug.successful_incoming_transfer_count
                      + debug.blocked_outgoing_transfer_count)
                    * 10.0
                + (cell.com.y > 0.95f ? 5.0 : 0.0) + std::abs(cell.pressure_gradient.y);
            candidates.push_back(Candidate{ .x = x, .y = y, .score = score });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return std::tie(a.score, a.y, a.x) > std::tie(b.score, b.y, b.x);
    });

    std::vector<CellTrack> tracks;
    for (const Candidate& candidate : candidates) {
        tracks.push_back(CellTrack{ .x = candidate.x, .y = candidate.y });
        if (static_cast<int>(tracks.size()) >= kTrackedCellsPerRegion) {
            break;
        }
    }

    return tracks;
}

std::string dumpPureBuriedRegionTimeSeriesWithInitialSettle(
    const World& world, RegionCoord region, int initial_settle_steps, const char* title)
{
    World replay(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(replay);
    replay.getPhysicsSettings() = world.getPhysicsSettings();

    if (initial_settle_steps > 0) {
        settleWorld(replay, initial_settle_steps);
    }
    const std::vector<CellTrack> tracks = selectTrackedCells(replay, region);
    std::ostringstream out;

    out << title << " for (" << region.x << "," << region.y << ")\n";
    out << "frame state wake cells move cBottom cTop skip support carry genC genHC genVC recvC "
           "recvHC recvVC pHydC pDynTC pDynRC pExC gCandC jamCandC compOutC compInC compGranC "
           "compSupC okOutC okInC blkC viscC gen recv pHydE pDynTE pDynRE pExE gCand jamCand "
           "compOut compIn okOut okIn blk compOutB compOutA compInB compInA pHydAmt pDynTAmt "
           "pDynRAmt pExAmt okOutAmt okInAmt blkAmt maxV meanV maxVy meanVy maxVForce meanVForce "
           "maxVForceY meanVForceY maxGradY meanGradY maxForceY meanForceY meanP\n";

    for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
        const RegionFrameSample sample = sampleRegionFrame(replay, region, frame);
        out << sample.frame << " " << regionStateToChar(sample.state) << " "
            << wakeReasonToChar(sample.wake_reason) << " " << sample.material_cells << " "
            << sample.moving_cells << " " << sample.near_bottom_cells << " "
            << sample.near_top_cells << " " << sample.gravity_skipped_cells << " "
            << sample.support_path_cells << " " << sample.carrying_load_cells << " "
            << sample.generated_move_cells << " " << sample.generated_horizontal_move_cells << " "
            << sample.generated_vertical_move_cells << " " << sample.received_move_cells << " "
            << sample.received_horizontal_move_cells << " " << sample.received_vertical_move_cells
            << " " << sample.hydrostatic_pressure_injection_cells << " "
            << sample.dynamic_pressure_target_injection_cells << " "
            << sample.dynamic_pressure_reflection_injection_cells << " "
            << sample.excess_move_pressure_injection_cells << " "
            << sample.gravity_compression_candidate_cells << " "
            << sample.jammed_contact_candidate_cells << " "
            << sample.outgoing_compression_contact_cells << " "
            << sample.incoming_compression_contact_cells << " "
            << sample.granular_compression_contact_cells << " "
            << sample.support_compression_contact_cells << " "
            << sample.successful_outgoing_transfer_cells << " "
            << sample.successful_incoming_transfer_cells << " "
            << sample.blocked_outgoing_transfer_cells << " " << sample.viscous_force_cells << " "
            << sample.generated_moves << " " << sample.received_moves << " "
            << sample.hydrostatic_pressure_injection_events << " "
            << sample.dynamic_pressure_target_injection_events << " "
            << sample.dynamic_pressure_reflection_injection_events << " "
            << sample.excess_move_pressure_injection_events << " "
            << sample.gravity_compression_candidates << " " << sample.jammed_contact_candidates
            << " " << sample.outgoing_compression_contacts << " "
            << sample.incoming_compression_contacts << " " << sample.successful_outgoing_transfers
            << " " << sample.successful_incoming_transfers << " "
            << sample.blocked_outgoing_transfers << " "
            << sample.max_outgoing_compression_normal_before << " "
            << sample.max_outgoing_compression_normal_after << " "
            << sample.max_incoming_compression_normal_before << " "
            << sample.max_incoming_compression_normal_after << " "
            << sample.hydrostatic_pressure_injection_amount << " "
            << sample.dynamic_pressure_target_injection_amount << " "
            << sample.dynamic_pressure_reflection_injection_amount << " "
            << sample.excess_move_pressure_injection_amount << " "
            << sample.successful_outgoing_transfer_amount << " "
            << sample.successful_incoming_transfer_amount << " "
            << sample.blocked_outgoing_transfer_amount << " " << sample.max_velocity << " "
            << sample.mean_velocity << " " << sample.max_abs_velocity_y << " "
            << sample.mean_abs_velocity_y << " " << sample.max_abs_viscous_force << " "
            << sample.mean_abs_viscous_force << " " << sample.max_abs_viscous_force_y << " "
            << sample.mean_abs_viscous_force_y << " " << sample.max_abs_pressure_gradient_y << " "
            << sample.mean_abs_pressure_gradient_y << " " << sample.max_abs_pressure_force_y << " "
            << sample.mean_abs_pressure_force_y << " " << sample.mean_pressure << "\n";

        if (frame < kDiagnosticFrames) {
            replay.advanceTime(kDt);
        }
    }

    out << "Tracked cells\n";
    for (const CellTrack& track : tracks) {
        out << "(" << track.x << "," << track.y << ")\n";
        out << "  frame vel vy comY pressure gradY pForceY vForceX vForceY grav moves gen outDir "
               "recv inDir pSrc gCand jamCand outComp inComp xfer okOut okIn blk pHydC pDynTC "
               "pDynRC pExC okOutAmt okInAmt blkAmt pHydAmt pDynTAmt pDynRAmt pExAmt\n";

        World trackedReplay(kWorldWidth, kWorldHeight);
        initializeSleepAnalysisWorld(trackedReplay);
        trackedReplay.getPhysicsSettings() = world.getPhysicsSettings();
        if (initial_settle_steps > 0) {
            settleWorld(trackedReplay, initial_settle_steps);
        }

        for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
            const WorldData& replayData = trackedReplay.getData();
            const size_t idx = static_cast<size_t>(track.y) * replayData.width + track.x;
            const Cell& cell = replayData.at(track.x, track.y);
            const CellDebug& debug = replayData.debug_info[idx];

            out << "  " << frame << " " << cell.velocity.magnitude() << " " << cell.velocity.y
                << " " << cell.com.y << " " << cell.pressure << " " << cell.pressure_gradient.y
                << " " << debug.accumulated_pressure_force.y << " "
                << debug.accumulated_viscous_force.x << " " << debug.accumulated_viscous_force.y
                << " " << gravityStateToChar(cell, debug) << " " << moveStateToChar(debug) << " "
                << debug.generated_move_count << " "
                << directionMaskToString(debug.generated_move_direction_mask) << " "
                << debug.received_move_count << " "
                << directionMaskToString(debug.received_move_direction_mask) << " "
                << pressureInjectionStateToChar(debug) << " "
                << debug.gravity_compression_candidate_count << ":"
                << directionMaskToString(debug.gravity_compression_candidate_direction_mask) << " "
                << debug.jammed_contact_candidate_count << ":"
                << directionMaskToString(debug.jammed_contact_candidate_direction_mask) << " "
                << debug.outgoing_compression_contact_count << ":"
                << compressionBranchMaskToString(debug.outgoing_compression_branch_mask) << ":"
                << debug.max_outgoing_compression_normal_before << "->"
                << debug.max_outgoing_compression_normal_after << " "
                << debug.incoming_compression_contact_count << ":"
                << compressionBranchMaskToString(debug.incoming_compression_branch_mask) << ":"
                << debug.max_incoming_compression_normal_before << "->"
                << debug.max_incoming_compression_normal_after << " " << transferStateToChar(debug)
                << " " << debug.successful_outgoing_transfer_count << " "
                << debug.successful_incoming_transfer_count << " "
                << debug.blocked_outgoing_transfer_count << " "
                << debug.hydrostatic_pressure_injection_count << " "
                << debug.dynamic_pressure_target_injection_count << " "
                << debug.dynamic_pressure_reflection_injection_count << " "
                << debug.excess_move_pressure_injection_count << " "
                << debug.successful_outgoing_transfer_amount << " "
                << debug.successful_incoming_transfer_amount << " "
                << debug.blocked_outgoing_transfer_amount << " "
                << debug.hydrostatic_pressure_injection_amount << " "
                << debug.dynamic_pressure_target_injection_amount << " "
                << debug.dynamic_pressure_reflection_injection_amount << " "
                << debug.excess_move_pressure_injection_amount << "\n";

            if (frame < kDiagnosticFrames) {
                trackedReplay.advanceTime(kDt);
            }
        }
    }

    return out.str();
}

std::string dumpPureBuriedRegionInitialTimeSeries(const World& world, RegionCoord region)
{
    return dumpPureBuriedRegionTimeSeriesWithInitialSettle(
        world, region, 0, "Pure buried region initial time series");
}

std::string dumpPureBuriedRegionTimeSeries(const World& world, RegionCoord region)
{
    return dumpPureBuriedRegionTimeSeriesWithInitialSettle(
        world, region, kSettleSteps, "Pure buried region time series");
}

std::string dumpSandboxTrackedRegionTimeSeries(
    const World& world, const std::vector<NamedRegion>& tracked_regions)
{
    std::ostringstream out;
    out << "Sandbox tracked region time series\n";
    if (tracked_regions.empty()) {
        out << "<none>\n";
        return out.str();
    }

    for (const NamedRegion& tracked_region : tracked_regions) {
        World replay(kSandboxWorldWidth, kSandboxWorldHeight);
        initializeSandboxQuadrantWorld(replay);
        replay.getPhysicsSettings() = world.getPhysicsSettings();
        settleWorld(replay);
        const std::vector<CellTrack> tracks = selectTrackedCells(replay, tracked_region.region);

        out << tracked_region.label << " (" << tracked_region.region.x << ","
            << tracked_region.region.y << ")\n";
        out << "frame state wake cells move skip support carry genC genVC recvC recvVC gCandC "
               "jamCandC compOutC compInC gen recv gCand jamCand compOut compIn okOut okIn blk "
               "maxV meanV maxVy meanVy\n";

        for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
            const RegionFrameSample sample =
                sampleRegionFrame(replay, tracked_region.region, frame);
            out << sample.frame << " " << regionStateToChar(sample.state) << " "
                << wakeReasonToChar(sample.wake_reason) << " " << sample.material_cells << " "
                << sample.moving_cells << " " << sample.gravity_skipped_cells << " "
                << sample.support_path_cells << " " << sample.carrying_load_cells << " "
                << sample.generated_move_cells << " " << sample.generated_vertical_move_cells << " "
                << sample.received_move_cells << " " << sample.received_vertical_move_cells << " "
                << sample.gravity_compression_candidate_cells << " "
                << sample.jammed_contact_candidate_cells << " "
                << sample.outgoing_compression_contact_cells << " "
                << sample.incoming_compression_contact_cells << " " << sample.generated_moves << " "
                << sample.received_moves << " " << sample.gravity_compression_candidates << " "
                << sample.jammed_contact_candidates << " " << sample.outgoing_compression_contacts
                << " " << sample.incoming_compression_contacts << " "
                << sample.successful_outgoing_transfers << " "
                << sample.successful_incoming_transfers << " " << sample.blocked_outgoing_transfers
                << " " << sample.max_velocity << " " << sample.mean_velocity << " "
                << sample.max_abs_velocity_y << " " << sample.mean_abs_velocity_y << "\n";

            if (frame < kDiagnosticFrames) {
                replay.advanceTime(kDt);
            }
        }

        out << "Tracked cells\n";
        for (const CellTrack& track : tracks) {
            out << "(" << track.x << "," << track.y << ")\n";
            out << "  frame vel vy comY cForceX cForceY aForceX aForceY fForceX fForceY grav "
                   "moves gen outDir recv inDir gCand jamCand outComp inComp xfer okOut okIn "
                   "blk fNbrX fNbrY fMu fNorm fTan fPairFx fPairFy\n";

            World tracked_replay(kSandboxWorldWidth, kSandboxWorldHeight);
            initializeSandboxQuadrantWorld(tracked_replay);
            tracked_replay.getPhysicsSettings() = world.getPhysicsSettings();
            settleWorld(tracked_replay);

            for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
                const WorldData& replay_data = tracked_replay.getData();
                const size_t idx = static_cast<size_t>(track.y) * replay_data.width + track.x;
                const Cell& cell = replay_data.at(track.x, track.y);
                const CellDebug& debug = replay_data.debug_info[idx];

                out << "  " << frame << " " << cell.velocity.magnitude() << " " << cell.velocity.y
                    << " " << cell.com.y << " " << debug.accumulated_com_cohesion_force.x << " "
                    << debug.accumulated_com_cohesion_force.y << " "
                    << debug.accumulated_adhesion_force.x << " "
                    << debug.accumulated_adhesion_force.y << " "
                    << debug.accumulated_friction_force.x << " "
                    << debug.accumulated_friction_force.y << " " << gravityStateToChar(cell, debug)
                    << " " << moveStateToChar(debug) << " " << debug.generated_move_count << " "
                    << directionMaskToString(debug.generated_move_direction_mask) << " "
                    << debug.received_move_count << " "
                    << directionMaskToString(debug.received_move_direction_mask) << " "
                    << debug.gravity_compression_candidate_count << ":"
                    << directionMaskToString(debug.gravity_compression_candidate_direction_mask)
                    << " " << debug.jammed_contact_candidate_count << ":"
                    << directionMaskToString(debug.jammed_contact_candidate_direction_mask) << " "
                    << debug.outgoing_compression_contact_count << ":"
                    << compressionBranchMaskToString(debug.outgoing_compression_branch_mask) << ":"
                    << debug.max_outgoing_compression_normal_before << "->"
                    << debug.max_outgoing_compression_normal_after << " "
                    << debug.incoming_compression_contact_count << ":"
                    << compressionBranchMaskToString(debug.incoming_compression_branch_mask) << ":"
                    << debug.max_incoming_compression_normal_before << "->"
                    << debug.max_incoming_compression_normal_after << " "
                    << transferStateToChar(debug) << " " << debug.successful_outgoing_transfer_count
                    << " " << debug.successful_incoming_transfer_count << " "
                    << debug.blocked_outgoing_transfer_count << " "
                    << debug.strongest_friction_contact_neighbor_x << " "
                    << debug.strongest_friction_contact_neighbor_y << " "
                    << debug.strongest_friction_contact_coefficient << " "
                    << debug.strongest_friction_contact_normal_force << " "
                    << debug.strongest_friction_contact_tangential_speed << " "
                    << debug.strongest_friction_contact_force.x << " "
                    << debug.strongest_friction_contact_force.y << "\n";

                if (frame < kDiagnosticFrames) {
                    tracked_replay.advanceTime(kDt);
                }
            }
        }
    }

    return out.str();
}

std::string dumpWorldPressureSourceTimeSeriesWithInitialSettle(
    const World& world, int initial_settle_steps, const char* title)
{
    World replay(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(replay);
    replay.getPhysicsSettings() = world.getPhysicsSettings();
    if (initial_settle_steps > 0) {
        settleWorld(replay, initial_settle_steps);
    }

    std::ostringstream out;
    out << title << "\n";
    out << "frame src surf buried hydC dynTC dynRC exC hydE dynTE dynRE exE hydAmt dynTAmt "
           "dynRAmt exAmt bbox maxAmt maxX maxY maxP maxGradY maxForceY\n";

    for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
        const WorldPressureSourceFrameSample sample = sampleWorldPressureSourceFrame(replay, frame);

        out << sample.frame << " " << sample.source_cells << " "
            << sample.surface_adjacent_source_cells << " " << sample.buried_source_cells << " "
            << sample.hydrostatic_source_cells << " " << sample.dynamic_target_source_cells << " "
            << sample.dynamic_reflection_source_cells << " " << sample.excess_move_source_cells
            << " " << sample.hydrostatic_source_events << " " << sample.dynamic_target_source_events
            << " " << sample.dynamic_reflection_source_events << " "
            << sample.excess_move_source_events << " " << sample.hydrostatic_source_amount << " "
            << sample.dynamic_target_source_amount << " " << sample.dynamic_reflection_source_amount
            << " " << sample.excess_move_source_amount << " ";

        if (sample.source_cells > 0) {
            out << sample.min_x << "," << sample.min_y << "-" << sample.max_x << ","
                << sample.max_y;
        }
        else {
            out << ".";
        }

        out << " " << sample.strongest_source_amount << " " << sample.strongest_source_x << " "
            << sample.strongest_source_y << " " << sample.strongest_source_pressure << " "
            << sample.strongest_source_gradient_y << " " << sample.strongest_source_force_y << "\n";

        if (sample.source_cells > 0) {
            out << "Top source cells at frame " << frame << "\n";
            out << dumpFramePressureSourceHotspots(replay);
        }

        if (frame < kDiagnosticFrames) {
            replay.advanceTime(kDt);
        }
    }

    return out.str();
}

std::string dumpInitialWorldPressureSourceTimeSeries(const World& world)
{
    return dumpWorldPressureSourceTimeSeriesWithInitialSettle(
        world, 0, "World pressure source time series from frame 0");
}

std::string dumpWorldPressureSourceTimeSeries(const World& world)
{
    return dumpWorldPressureSourceTimeSeriesWithInitialSettle(
        world, kSettleSteps, "World pressure source time series after settle");
}

std::string dumpWorldPressureSourceSettleScan(const World& world, std::optional<RegionCoord> region)
{
    World replay(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(replay);
    replay.getPhysicsSettings() = world.getPhysicsSettings();

    const std::vector<int> checkpoint_frames{ 0, 1, 2, 4, 8, 16, 32, 48, 64, 96, 128, 160, 180 };
    std::ostringstream out;
    out << "World pressure source settle scan\n";
    out << "frame src surf buried hydE dynTE dynRE exE hydAmt dynTAmt dynRAmt exAmt";
    if (region.has_value()) {
        out << " regionState regionWake regionMeanV regionMaxV regionMeanP regionMeanForceY";
    }
    out << "\n";

    for (int frame = 0; frame <= kSettleSteps; ++frame) {
        const WorldPressureSourceFrameSample source_sample =
            sampleWorldPressureSourceFrame(replay, frame);
        const bool is_checkpoint =
            std::find(checkpoint_frames.begin(), checkpoint_frames.end(), frame)
            != checkpoint_frames.end();
        const bool interesting = is_checkpoint || source_sample.source_cells > 0;

        if (interesting) {
            out << frame << " " << source_sample.source_cells << " "
                << source_sample.surface_adjacent_source_cells << " "
                << source_sample.buried_source_cells << " "
                << source_sample.hydrostatic_source_events << " "
                << source_sample.dynamic_target_source_events << " "
                << source_sample.dynamic_reflection_source_events << " "
                << source_sample.excess_move_source_events << " "
                << source_sample.hydrostatic_source_amount << " "
                << source_sample.dynamic_target_source_amount << " "
                << source_sample.dynamic_reflection_source_amount << " "
                << source_sample.excess_move_source_amount;

            if (region.has_value()) {
                const RegionFrameSample region_sample = sampleRegionFrame(replay, *region, frame);
                out << " " << regionStateToChar(region_sample.state) << " "
                    << wakeReasonToChar(region_sample.wake_reason) << " "
                    << region_sample.mean_velocity << " " << region_sample.max_velocity << " "
                    << region_sample.mean_pressure << " "
                    << region_sample.mean_abs_pressure_force_y;
            }
            out << "\n";

            if (source_sample.source_cells > 0) {
                out << "Top source cells at frame " << frame << "\n";
                out << dumpFramePressureSourceHotspots(replay);
            }
        }

        if (frame < kSettleSteps) {
            replay.advanceTime(kDt);
        }
    }

    return out.str();
}

std::string dumpSettlingDiagnostics(
    const World& world, const GridOfCells& grid, const std::vector<RegionCoord>& buriedRegions)
{
    std::ostringstream out;
    out << dumpRegionStates(world) << "\n";
    out << dumpWakeReasons(world) << "\n";
    out << dumpBuriedExposureMap(world, grid) << "\n";
    out << dumpBuriedRegionDetails(world, buriedRegions) << "\n";
    out << dumpBuriedRegionCellMaps(world, buriedRegions) << "\n";
    out << dumpBuriedRegionCellDetails(world, buriedRegions) << "\n";
    out << dumpOptionalPureBuriedRegionTimeSeries(world, buriedRegions);
    if (world.getPhysicsSettings().pressure_hydrostatic_enabled
        || world.getPhysicsSettings().pressure_dynamic_enabled) {
        out << "\n"
            << dumpWorldPressureSourceSettleScan(world, findPureBuriedRegion(world, buriedRegions));
        out << "\n" << dumpInitialWorldPressureSourceTimeSeries(world);
        out << "\n" << dumpWorldPressureSourceTimeSeries(world);
    }
    return out.str();
}

void buildBoundaryWalls(World& world)
{
    const WorldData& data = world.getData();
    for (int x = 0; x < data.width; ++x) {
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(x), 0 }, Material::EnumType::Wall);
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(data.height - 1) },
            Material::EnumType::Wall);
    }
    for (int y = 0; y < data.height; ++y) {
        world.replaceMaterialAtCell(
            Vector2s{ 0, static_cast<int16_t>(y) }, Material::EnumType::Wall);
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(data.width - 1), static_cast<int16_t>(y) },
            Material::EnumType::Wall);
    }
}

void initializeSleepAnalysisWorld(World& world)
{
    buildBoundaryWalls(world);
    buildSymmetricDirtPile(world);
}

void initializeSandboxQuadrantWorld(World& world)
{
    SandboxScenario scenario;
    const Config::Sandbox config{
        .quadrantEnabled = true,
        .waterColumnEnabled = false,
        .rightThrowEnabled = false,
        .rainRate = 0.0,
    };

    scenario.setConfig(ScenarioConfig{ config }, world);
    scenario.setup(world);
}

void initializeStillWaterPoolWorld(World& world)
{
    for (int y = 8; y <= 32; ++y) {
        world.replaceMaterialAtCell(
            Vector2s{ 15, static_cast<int16_t>(y) }, Material::EnumType::Wall);
        world.replaceMaterialAtCell(
            Vector2s{ 48, static_cast<int16_t>(y) }, Material::EnumType::Wall);
    }

    for (int x = 15; x <= 48; ++x) {
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(x), 32 }, Material::EnumType::Wall);
    }

    for (int y = 16; y <= 31; ++y) {
        fillHorizontalSpan(world, y, 16, 47, Material::EnumType::Water);
    }
}

void initializeDamBreakWorld(World& world, DamBreakScenario& scenario)
{
    const Config::DamBreak config{
        .damHeight = 10.0,
        .autoRelease = true,
        .releaseTime = kDt,
    };

    scenario.setConfig(ScenarioConfig{ config }, world);
    scenario.setup(world);
}

class WorldRegionSleepingBehaviorParamTest : public ::testing::TestWithParam<GravityCase> {};

} // namespace

TEST(WorldRegionSleepingBehaviorTest, DeepDryPileSceneIncludesBuriedCore)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);

    SCOPED_TRACE(dumpSettlingDiagnostics(world, grid, buried_regions));

    ASSERT_FALSE(buried_regions.empty()) << "Expected the test pile to contain at least one buried"
                                         << " 8x8 region.";
}

TEST(WorldRegionSleepingBehaviorTest, TransferQuietHeuristicFindsBuriedRegionUnderGravity)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);
    const TransferQuietHeuristicConfig config{};
    const TransferQuietExperimentResult experiment =
        runTransferQuietExperiment(world, buried_regions, config);

    ASSERT_FALSE(buried_regions.empty()) << "Expected the test pile to contain at least one buried"
                                         << " 8x8 region.";
    EXPECT_GT(countTransferQuietCandidateRegions(experiment), 0)
        << dumpTransferQuietExperiment(experiment, config);
}

TEST(WorldRegionSleepingBehaviorTest, PureBuriedRegionCanQualifyWhenFullyQuiet)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);
    const TransferQuietHeuristicConfig config{};
    const TransferQuietExperimentResult experiment =
        runTransferQuietExperiment(world, buried_regions, config);

    ASSERT_TRUE(experiment.pure_region.has_value())
        << "Expected at least one pure buried region in the test pile.";

    const TransferQuietRegionObservation* observation =
        findTransferQuietObservation(experiment, *experiment.pure_region);
    ASSERT_NE(observation, nullptr) << "Expected observation for the pure buried region.";

    EXPECT_TRUE(observation->ever_candidate) << dumpTransferQuietExperiment(experiment, config);
    EXPECT_EQ(observation->max_generated_moves, 0)
        << dumpTransferQuietExperiment(experiment, config);
    EXPECT_EQ(observation->max_successful_transfers, 0)
        << dumpTransferQuietExperiment(experiment, config);
}

TEST(WorldRegionSleepingBehaviorTest, ToeNotchMovesExposedDirtWithoutDraggingPureBuriedCore)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);

    settleWorld(world);
    GridOfCells settled_grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions =
        collectBuriedMaterialRegions(world, settled_grid);
    const std::optional<RegionCoord> pure_region = findPureBuriedRegion(world, buried_regions);

    ASSERT_TRUE(pure_region.has_value())
        << "Expected at least one pure buried region before the surface disturbance.";

    carveLeftToeNotch(world);

    constexpr int kObservationFrames = 24;
    int frames_with_surface_activity = 0;
    int total_surface_successful_transfers = 0;
    int max_pure_successful_transfers = 0;
    double max_pure_max_velocity = 0.0;
    double max_pure_mean_velocity = 0.0;

    for (int frame = 0; frame < kObservationFrames; ++frame) {
        world.advanceTime(kDt);

        GridOfCells frame_grid = makeGrid(world);
        const int surface_successful_transfers =
            countExposedGranularSuccessfulTransfers(world, frame_grid);
        total_surface_successful_transfers += surface_successful_transfers;
        if (surface_successful_transfers > 0) {
            frames_with_surface_activity++;
        }

        const RegionFrameSample pure_region_sample =
            sampleRegionFrame(world, *pure_region, frame + 1);
        max_pure_successful_transfers = std::max(
            max_pure_successful_transfers,
            pure_region_sample.successful_outgoing_transfers
                + pure_region_sample.successful_incoming_transfers);
        max_pure_mean_velocity = std::max(max_pure_mean_velocity, pure_region_sample.mean_velocity);
        max_pure_max_velocity = std::max(max_pure_max_velocity, pure_region_sample.max_velocity);
    }

    GridOfCells final_grid = makeGrid(world);
    const std::vector<RegionCoord> final_buried_regions =
        collectBuriedMaterialRegions(world, final_grid);
    std::ostringstream trace;
    trace << "surfaceActivityFrames=" << frames_with_surface_activity
          << " totalSurfaceSuccessfulTransfers=" << total_surface_successful_transfers
          << " maxPureSuccessfulTransfers=" << max_pure_successful_transfers
          << " maxPureMeanVelocity=" << max_pure_mean_velocity
          << " maxPureMaxVelocity=" << max_pure_max_velocity;

    SCOPED_TRACE(trace.str());
    SCOPED_TRACE(dumpSettlingDiagnostics(world, final_grid, final_buried_regions));

    EXPECT_GT(frames_with_surface_activity, 0)
        << "Expected the toe notch to cause real exposed granular transfers.";
    EXPECT_GT(total_surface_successful_transfers, 0)
        << "Expected the toe notch to produce at least one successful transfer.";
    EXPECT_EQ(max_pure_successful_transfers, 0)
        << "Expected the pure buried core to remain transfer-quiet during the early surface slide.";
    EXPECT_LT(max_pure_mean_velocity, 0.02) << "Expected the pure buried core mean velocity to "
                                               "stay low during the early surface slide.";
    EXPECT_LT(max_pure_max_velocity, 0.10)
        << "Expected the pure buried core max velocity to stay low during the early surface slide.";
}

TEST(WorldRegionSleepingBehaviorTest, LargeToeCutStaysSurfaceLocalizedDuringEarlyCollapse)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);

    settleWorld(world);
    GridOfCells settled_grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions =
        collectBuriedMaterialRegions(world, settled_grid);
    const std::optional<RegionCoord> pure_region = findPureBuriedRegion(world, buried_regions);

    ASSERT_TRUE(pure_region.has_value())
        << "Expected at least one pure buried region before the larger toe collapse.";

    carveLargeLeftToeCut(world);

    constexpr int kObservationFrames = 48;
    constexpr int kPureCoreGraceFrames = 16;
    int frames_with_surface_activity = 0;
    int total_surface_successful_transfers = 0;
    int max_pure_successful_transfers = 0;
    double max_pure_max_velocity = 0.0;
    double max_pure_mean_velocity = 0.0;

    for (int frame = 0; frame < kObservationFrames; ++frame) {
        world.advanceTime(kDt);

        GridOfCells frame_grid = makeGrid(world);
        const int surface_successful_transfers =
            countExposedGranularSuccessfulTransfers(world, frame_grid);
        total_surface_successful_transfers += surface_successful_transfers;
        if (surface_successful_transfers > 0) {
            frames_with_surface_activity++;
        }

        if (frame < kPureCoreGraceFrames) {
            const RegionFrameSample pure_region_sample =
                sampleRegionFrame(world, *pure_region, frame + 1);
            max_pure_successful_transfers = std::max(
                max_pure_successful_transfers,
                pure_region_sample.successful_outgoing_transfers
                    + pure_region_sample.successful_incoming_transfers);
            max_pure_mean_velocity =
                std::max(max_pure_mean_velocity, pure_region_sample.mean_velocity);
            max_pure_max_velocity =
                std::max(max_pure_max_velocity, pure_region_sample.max_velocity);
        }
    }

    GridOfCells final_grid = makeGrid(world);
    const std::vector<RegionCoord> final_buried_regions =
        collectBuriedMaterialRegions(world, final_grid);
    std::ostringstream trace;
    trace << "surfaceActivityFrames=" << frames_with_surface_activity
          << " totalSurfaceSuccessfulTransfers=" << total_surface_successful_transfers
          << " maxPureSuccessfulTransfers=" << max_pure_successful_transfers
          << " maxPureMeanVelocity=" << max_pure_mean_velocity
          << " maxPureMaxVelocity=" << max_pure_max_velocity;

    SCOPED_TRACE(trace.str());
    SCOPED_TRACE(dumpSettlingDiagnostics(world, final_grid, final_buried_regions));

    EXPECT_GE(frames_with_surface_activity, 4)
        << "Expected the larger toe cut to sustain exposed granular activity for multiple frames.";
    EXPECT_GE(total_surface_successful_transfers, 4)
        << "Expected the larger toe cut to produce multiple successful exposed transfers.";
    EXPECT_EQ(max_pure_successful_transfers, 0)
        << "Expected the pure buried core to remain transfer-quiet during the early collapse "
           "window.";
    EXPECT_LT(max_pure_mean_velocity, 0.02) << "Expected the pure buried core mean velocity to "
                                               "stay low before the failure front reaches"
                                            << " it.";
    EXPECT_LT(max_pure_max_velocity, 0.10)
        << "Expected the pure buried core max velocity to stay bounded during the early collapse"
        << " window.";
}

TEST(WorldRegionSleepingBehaviorTest, SupportLossReactivatesUnsupportedGranularWedge)
{
    World world(kWorldWidth, kWorldHeight);
    buildBoundaryWalls(world);
    buildSupportedGranularWedge(world);

    settleWorld(world, 120);

    {
        const WorldData& data = world.getData();
        const size_t center_idx = static_cast<size_t>(15) * data.width + 31;
        const CellDebug& center_debug = data.debug_info[center_idx];
        ASSERT_TRUE(center_debug.has_granular_support_path)
            << "Expected the wedge base to be supported before removing the shelf.";
        ASSERT_TRUE(center_debug.gravity_skipped_for_support)
            << "Expected the supported wedge base to skip gravity before support removal.";
    }

    removeGranularWedgeSupport(world);

    constexpr int kObservationFrames = 24;
    int frames_with_unsupported_motion = 0;
    int max_unsupported_moving_cells = 0;
    int total_exposed_successful_transfers = 0;

    for (int frame = 0; frame < kObservationFrames; ++frame) {
        world.advanceTime(kDt);

        GridOfCells frame_grid = makeGrid(world);
        total_exposed_successful_transfers +=
            countExposedGranularSuccessfulTransfers(world, frame_grid);

        const int unsupported_moving_cells =
            countUnsupportedMovingGranularCells(world, 22, 40, 12, 24, 0.05);
        max_unsupported_moving_cells =
            std::max(max_unsupported_moving_cells, unsupported_moving_cells);
        if (unsupported_moving_cells > 0) {
            frames_with_unsupported_motion++;
        }
    }

    const int dirt_cells_below_support =
        countMaterialCellsInBox(world, 24, 38, 17, 28, Material::EnumType::Dirt);

    std::ostringstream trace;
    trace << "framesWithUnsupportedMotion=" << frames_with_unsupported_motion
          << " maxUnsupportedMovingCells=" << max_unsupported_moving_cells
          << " totalExposedSuccessfulTransfers=" << total_exposed_successful_transfers
          << " dirtCellsBelowSupport=" << dirt_cells_below_support;

    SCOPED_TRACE(trace.str());

    EXPECT_GT(frames_with_unsupported_motion, 0)
        << "Expected the unsupported wedge to start moving after the support shelf is removed.";
    EXPECT_GT(max_unsupported_moving_cells, 0)
        << "Expected at least one unsupported granular cell to pick up meaningful velocity.";
    EXPECT_GT(total_exposed_successful_transfers, 0)
        << "Expected the unsupported wedge to produce successful transfers while it falls.";
    EXPECT_GT(dirt_cells_below_support, 0)
        << "Expected dirt from the former wedge to move below the removed support shelf.";
}

TEST(WorldRegionSleepingBehaviorTest, SandboxQuadrantWallOnlyMixesDoNotCountAsMixedRegions)
{
    World world(kSandboxWorldWidth, kSandboxWorldHeight);
    initializeSandboxQuadrantWorld(world);

    settleWorld(world);

    const GridOfCells grid = makeGrid(world);
    const SandboxRegionBuckets buckets = classifySandboxDirtRegions(world, grid);

    SCOPED_TRACE(dumpSandboxQuadrantDiagnostics(
        ForceIsolationProfile{
            .name = "full",
            .gravity = getDefaultPhysicsSettings().gravity,
        }));

    EXPECT_TRUE(buckets.mixed_dirt_regions.empty())
        << "Expected dirt regions that only share blocks with static walls to stop counting as"
        << " mixed-material regions.";
    EXPECT_GE(static_cast<int>(buckets.interior_dirt_regions.size()), 4)
        << "Expected wall-backed dirt regions to become interior candidates once wall-only mixes"
        << " stop forcing Awake.";
}

TEST_P(WorldRegionSleepingBehaviorParamTest, BuriedRegionQuietStateMatchesGravityMode)
{
    const GravityCase& gravity_case = GetParam();
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);
    world.getPhysicsSettings().gravity = gravity_case.gravity;

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);

    SCOPED_TRACE(dumpSettlingDiagnostics(world, grid, buried_regions));

    ASSERT_FALSE(buried_regions.empty()) << "Expected the test pile to contain at least one buried"
                                         << " 8x8 region.";

    const int quiet_buried_regions = countQuietBuriedRegions(world, buried_regions);

    if (gravity_case.expect_quiet_buried_region) {
        EXPECT_GT(quiet_buried_regions, 0)
            << "Expected at least one buried region to transition out of Awake after settling.";
    }
    else {
        EXPECT_EQ(quiet_buried_regions, 0)
            << "Expected all buried regions to remain Awake under the current heuristic.";
    }
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_DeepDryPileHasBuriedRegionThatQuietsUnderGravity)
{
    World world(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(world);

    settleWorld(world);
    GridOfCells grid = makeGrid(world);
    const std::vector<RegionCoord> buried_regions = collectBuriedMaterialRegions(world, grid);

    SCOPED_TRACE(dumpSettlingDiagnostics(world, grid, buried_regions));

    ASSERT_FALSE(buried_regions.empty()) << "Expected the test pile to contain at least one buried"
                                         << " 8x8 region.";

    EXPECT_GT(countQuietBuriedRegions(world, buried_regions), 0)
        << "Expected at least one buried region to transition out of Awake after settling.";
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_TransferQuietForceIsolationSweep)
{
    std::cout << dumpForceIsolationSweep();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_GravityContactOnlyDiagnostics)
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "gravity_contact_only",
        .gravity = defaults.gravity,
        .pressure = false,
        .cohesion = false,
        .adhesion = false,
        .friction = false,
        .viscosity = false,
        .air_resistance = false,
        .swap = false,
    };

    std::cout << dumpForceIsolationProfileDiagnostics(profile);
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_PressureOnlyDiagnostics)
{
    const ForceIsolationProfile profile{
        .name = "pressure_only",
        .gravity = 0.0,
        .cohesion = false,
        .adhesion = false,
        .friction = false,
        .viscosity = false,
        .air_resistance = false,
        .swap = false,
    };

    std::cout << dumpForceIsolationProfileDiagnostics(profile);
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_GravityPressureOnlyDiagnostics)
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "gravity_pressure_only",
        .gravity = defaults.gravity,
        .cohesion = false,
        .adhesion = false,
        .friction = false,
        .viscosity = false,
        .air_resistance = false,
        .swap = false,
    };

    std::cout << dumpForceIsolationProfileDiagnostics(profile);
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_ViscosityOnlyDiagnostics)
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "viscosity_only",
        .gravity = defaults.gravity,
        .pressure = false,
        .cohesion = false,
        .adhesion = false,
        .friction = false,
        .air_resistance = false,
        .swap = false,
    };

    std::cout << dumpForceIsolationProfileDiagnostics(profile);
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantDiagnostics)
{
    const PhysicsSettings defaults = getDefaultPhysicsSettings();
    const ForceIsolationProfile profile{
        .name = "full",
        .gravity = defaults.gravity,
    };

    std::cout << dumpSandboxQuadrantDiagnostics(profile);
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantForceIsolationSweep)
{
    std::cout << dumpSandboxQuadrantForceIsolationSweep();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantAsciiDiagrams)
{
    std::cout << dumpSandboxQuadrantAsciiDiagrams();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantGravityContactDiagnostics)
{
    std::cout << dumpSandboxGravityContactDiagnostics();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantCohesionAdhesionFrictionDiagnostics)
{
    std::cout << dumpSandboxCohesionAdhesionFrictionDiagnostics();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantWallBackedInteriorDiagnostics)
{
    std::cout << dumpSandboxWallBackedInteriorDiagnostics();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_SandboxQuadrantLongRunRegionTransitions)
{
    std::cout << dumpSandboxQuadrantLongRunRegionTransitions();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_WaterPoolDiagnostics)
{
    std::cout << dumpWaterPoolDiagnostics();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_WaterPoolForceIsolationSweep)
{
    std::cout << dumpWaterPoolForceIsolationSweep();
}

TEST(WorldRegionSleepingBehaviorTest, DISABLED_DamBreakDiagnostics)
{
    std::cout << dumpDamBreakDiagnostics();
}

INSTANTIATE_TEST_SUITE_P(
    GravityModes,
    WorldRegionSleepingBehaviorParamTest,
    ::testing::Values(
        GravityCase{
            .name = "gravity_off",
            .gravity = 0.0,
            .expect_quiet_buried_region = true,
        },
        GravityCase{
            .name = "gravity_on",
            .gravity = 10.0,
            .expect_quiet_buried_region = true,
        }),
    [](const ::testing::TestParamInfo<GravityCase>& info) { return info.param.name; });
