#include "WorldRegionActivityTracker.h"

#include "Cell.h"
#include "GridOfCells.h"
#include "MaterialType.h"
#include "PhysicsSettings.h"
#include "World.h"
#include "WorldData.h"
#include "organisms/OrganismManager.h"
#include "organisms/OrganismType.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

using DirtSim::Cell;
using DirtSim::GridOfCells;
using DirtSim::RegionMeta;
using DirtSim::RegionState;
using DirtSim::RegionSummary;
using DirtSim::WakeReason;
using DirtSim::World;
using DirtSim::WorldData;
using DirtSim::WorldRegionActivityTracker;
using DirtSim::Material::EnumType;

bool isActiveState(RegionState state)
{
    switch (state) {
        case RegionState::Awake:
        case RegionState::LoadedQuiet:
            return true;
        case RegionState::Sleeping:
            return false;
    }

    return true;
}

bool isWaterCell(const Cell& cell)
{
    return !cell.isEmpty() && cell.material_type == EnumType::Water;
}

bool shouldForceAwake(
    const RegionSummary& summary, const WorldRegionActivityTracker::Config& config)
{
    if (summary.touched_this_frame) {
        return true;
    }
    if (summary.max_velocity > config.velocity_epsilon) {
        return true;
    }
    if (summary.max_live_pressure_delta > config.live_pressure_delta_epsilon) {
        return true;
    }
    if (summary.max_static_load_delta > config.static_load_delta_epsilon) {
        return true;
    }
    if (config.keep_empty_adjacent_awake && summary.has_empty_adjacency) {
        return true;
    }
    if (config.keep_mixed_material_awake && summary.has_mixed_material) {
        return true;
    }
    if (config.keep_organism_regions_awake && summary.has_organism) {
        return true;
    }
    if (config.keep_water_adjacent_awake && summary.has_water_adjacency) {
        return true;
    }

    return false;
}

uint16_t saturatingIncrement(uint16_t value)
{
    if (value == std::numeric_limits<uint16_t>::max()) {
        return value;
    }

    return static_cast<uint16_t>(value + 1);
}

} // namespace

namespace DirtSim {

void WorldRegionActivityTracker::resize(
    int world_width, int world_height, int blocks_x, int blocks_y)
{
    world_width_ = world_width;
    world_height_ = world_height;
    blocks_x_ = blocks_x;
    blocks_y_ = blocks_y;
    reset();
}

void WorldRegionActivityTracker::reset()
{
    const size_t cell_count = static_cast<size_t>(std::max(world_width_, 0))
        * static_cast<size_t>(std::max(world_height_, 0));
    const size_t region_count =
        static_cast<size_t>(std::max(blocks_x_, 0)) * static_cast<size_t>(std::max(blocks_y_, 0));

    previous_live_pressure_.assign(cell_count, 0.0f);
    previous_static_load_.assign(cell_count, 0.0f);

    region_meta_.assign(region_count, RegionMeta{});
    region_summary_.assign(region_count, RegionSummary{});

    cell_active_.assign(cell_count, 1);
    region_active_.assign(region_count, 1);
    region_touched_this_frame_.assign(region_count, 0);
    region_wake_requested_.assign(region_count, 0);
    region_pending_wake_reason_.assign(region_count, WakeReason::None);

    previous_gravity_ = 0.0;
    previous_gravity_initialized_ = false;
}

void WorldRegionActivityTracker::setConfig(const Config& config)
{
    config_ = config;
}

void WorldRegionActivityTracker::noteWakeAtCell(int x, int y, WakeReason reason)
{
    if (x < 0 || y < 0 || x >= world_width_ || y >= world_height_) {
        return;
    }

    noteWakeAtRegion(x / REGION_SIZE, y / REGION_SIZE, reason);
}

void WorldRegionActivityTracker::noteWakeAtRegion(int block_x, int block_y, WakeReason reason)
{
    if (block_x < 0 || block_y < 0 || block_x >= blocks_x_ || block_y >= blocks_y_) {
        return;
    }

    const int idx = regionIndex(block_x, block_y);
    region_touched_this_frame_[idx] = 1;
    region_wake_requested_[idx] = 1;
    if (region_pending_wake_reason_[idx] == WakeReason::None) {
        region_pending_wake_reason_[idx] = reason;
    }
}

void WorldRegionActivityTracker::noteBlockedTransfer(int x, int y)
{
    noteWakeAtCell(x, y, WakeReason::BlockedTransfer);
}

void WorldRegionActivityTracker::noteMaterialMove(int from_x, int from_y, int to_x, int to_y)
{
    noteWakeAtCell(from_x, from_y, WakeReason::Move);
    noteWakeAtCell(to_x, to_y, WakeReason::Move);
}

void WorldRegionActivityTracker::beginFrame(
    const World& world, const GridOfCells& grid, uint32_t timestep)
{
    if (world_width_ != grid.getWidth() || world_height_ != grid.getHeight()
        || blocks_x_ != grid.getBlocksX() || blocks_y_ != grid.getBlocksY()) {
        resize(grid.getWidth(), grid.getHeight(), grid.getBlocksX(), grid.getBlocksY());
    }

    const double gravity = world.getPhysicsSettings().gravity;
    if (!previous_gravity_initialized_) {
        previous_gravity_ = gravity;
        previous_gravity_initialized_ = true;
    }
    else if (std::abs(previous_gravity_ - gravity) > MIN_GRAVITY_DELTA) {
        for (int block_y = 0; block_y < blocks_y_; ++block_y) {
            for (int block_x = 0; block_x < blocks_x_; ++block_x) {
                noteWakeAtRegion(block_x, block_y, WakeReason::GravityChanged);
            }
        }
        previous_gravity_ = gravity;
    }

    applyWakeRequests(timestep);
    buildActiveMasks();
}

void WorldRegionActivityTracker::summarizeFrame(
    const World& world, const GridOfCells& grid, uint32_t /*timestep*/)
{
    if (world_width_ != grid.getWidth() || world_height_ != grid.getHeight()
        || blocks_x_ != grid.getBlocksX() || blocks_y_ != grid.getBlocksY()) {
        resize(grid.getWidth(), grid.getHeight(), grid.getBlocksX(), grid.getBlocksY());
    }

    const WorldData& data = world.getData();
    const auto& organism_grid = world.getOrganismManager().getGrid();

    region_summary_.assign(region_summary_.size(), RegionSummary{});
    std::vector<int> region_primary_material(region_meta_.size(), -1);

    for (size_t region_idx = 0; region_idx < region_summary_.size(); ++region_idx) {
        region_summary_[region_idx].touched_this_frame =
            region_touched_this_frame_[region_idx] != 0;
    }

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t cell_idx = static_cast<size_t>(y) * data.width + x;
            const int region_idx = cellToRegionIndex(x, y);

            const Cell& cell = data.at(x, y);
            RegionSummary& summary = region_summary_[region_idx];

            summary.max_velocity =
                std::max(summary.max_velocity, static_cast<float>(cell.velocity.mag()));
            summary.max_live_pressure_delta = std::max(
                summary.max_live_pressure_delta,
                std::abs(cell.pressure - previous_live_pressure_[cell_idx]));
            summary.max_static_load_delta = std::max(
                summary.max_static_load_delta,
                std::abs(cell.static_load - previous_static_load_[cell_idx]));

            if (cell_idx < organism_grid.size() && organism_grid[cell_idx] != INVALID_ORGANISM_ID) {
                summary.has_organism = true;
            }

            if (cell.isEmpty()) {
                continue;
            }

            const EmptyNeighborhood empty_neighborhood = grid.getEmptyNeighborhood(x, y);
            if (empty_neighborhood.countEmptyNeighbors() > 0) {
                summary.has_empty_adjacency = true;
            }

            const MaterialNeighborhood material_neighborhood = grid.getMaterialNeighborhood(x, y);
            if (isWaterCell(cell) || material_neighborhood.countMaterial(EnumType::Water) > 0) {
                summary.has_water_adjacency = true;
            }

            const int material_value = static_cast<int>(cell.material_type);
            if (region_primary_material[region_idx] < 0) {
                region_primary_material[region_idx] = material_value;
            }
            else if (region_primary_material[region_idx] != material_value) {
                summary.has_mixed_material = true;
            }
        }
    }

    for (size_t region_idx = 0; region_idx < region_meta_.size(); ++region_idx) {
        RegionMeta& meta = region_meta_[region_idx];
        const RegionSummary& summary = region_summary_[region_idx];

        if (shouldForceAwake(summary, config_)) {
            meta.quiet_frames = 0;
            meta.state = RegionState::Awake;
            continue;
        }

        meta.quiet_frames = saturatingIncrement(meta.quiet_frames);
        if (meta.quiet_frames >= config_.quiet_frames_to_sleep) {
            meta.state = RegionState::Sleeping;
        }
        else {
            meta.state = RegionState::LoadedQuiet;
        }
    }

    snapshotPreviousFields(world);
    std::fill(region_touched_this_frame_.begin(), region_touched_this_frame_.end(), 0);
}

bool WorldRegionActivityTracker::isCellActive(int x, int y) const
{
    if (x < 0 || y < 0 || x >= world_width_ || y >= world_height_) {
        return false;
    }
    if (cell_active_.empty()) {
        return true;
    }

    const size_t idx = static_cast<size_t>(y) * world_width_ + x;
    return idx < cell_active_.size() && cell_active_[idx] != 0;
}

bool WorldRegionActivityTracker::isRegionActive(int block_x, int block_y) const
{
    if (block_x < 0 || block_y < 0 || block_x >= blocks_x_ || block_y >= blocks_y_) {
        return false;
    }
    if (region_active_.empty()) {
        return true;
    }

    const int idx = regionIndex(block_x, block_y);
    return region_active_[idx] != 0;
}

RegionState WorldRegionActivityTracker::getRegionState(int block_x, int block_y) const
{
    return getRegionMeta(block_x, block_y).state;
}

WakeReason WorldRegionActivityTracker::getLastWakeReason(int block_x, int block_y) const
{
    return getRegionMeta(block_x, block_y).last_wake_reason;
}

const RegionMeta& WorldRegionActivityTracker::getRegionMeta(int block_x, int block_y) const
{
    static const RegionMeta default_meta{};

    if (block_x < 0 || block_y < 0 || block_x >= blocks_x_ || block_y >= blocks_y_) {
        return default_meta;
    }

    return region_meta_[regionIndex(block_x, block_y)];
}

const RegionSummary& WorldRegionActivityTracker::getRegionSummary(int block_x, int block_y) const
{
    static const RegionSummary default_summary{};

    if (block_x < 0 || block_y < 0 || block_x >= blocks_x_ || block_y >= blocks_y_) {
        return default_summary;
    }

    return region_summary_[regionIndex(block_x, block_y)];
}

void WorldRegionActivityTracker::populateDebugInfo(std::vector<RegionDebugInfo>& out) const
{
    out.resize(region_meta_.size());
    for (size_t idx = 0; idx < region_meta_.size(); ++idx) {
        out[idx].state = static_cast<uint8_t>(region_meta_[idx].state);
        out[idx].wake_reason = static_cast<uint8_t>(region_meta_[idx].last_wake_reason);
    }
}

int WorldRegionActivityTracker::cellToRegionIndex(int x, int y) const
{
    return regionIndex(x / REGION_SIZE, y / REGION_SIZE);
}

int WorldRegionActivityTracker::regionIndex(int block_x, int block_y) const
{
    return block_y * blocks_x_ + block_x;
}

void WorldRegionActivityTracker::applyWakeRequests(uint32_t timestep)
{
    for (size_t region_idx = 0; region_idx < region_meta_.size(); ++region_idx) {
        if (region_wake_requested_[region_idx] == 0) {
            continue;
        }

        RegionMeta& meta = region_meta_[region_idx];
        meta.last_wake_reason = region_pending_wake_reason_[region_idx];
        meta.last_wake_step = timestep;
        meta.quiet_frames = 0;
        meta.state = RegionState::Awake;

        region_wake_requested_[region_idx] = 0;
        region_pending_wake_reason_[region_idx] = WakeReason::None;
    }
}

void WorldRegionActivityTracker::buildActiveMasks()
{
    std::fill(region_active_.begin(), region_active_.end(), 0);
    std::fill(cell_active_.begin(), cell_active_.end(), 0);

    for (int block_y = 0; block_y < blocks_y_; ++block_y) {
        for (int block_x = 0; block_x < blocks_x_; ++block_x) {
            if (!isActiveState(region_meta_[regionIndex(block_x, block_y)].state)) {
                continue;
            }

            for (int halo_y = std::max(0, block_y - 1);
                 halo_y <= std::min(blocks_y_ - 1, block_y + 1);
                 ++halo_y) {
                for (int halo_x = std::max(0, block_x - 1);
                     halo_x <= std::min(blocks_x_ - 1, block_x + 1);
                     ++halo_x) {
                    region_active_[regionIndex(halo_x, halo_y)] = 1;
                }
            }
        }
    }

    for (int y = 0; y < world_height_; ++y) {
        for (int x = 0; x < world_width_; ++x) {
            const int region_idx = cellToRegionIndex(x, y);
            const size_t cell_idx = static_cast<size_t>(y) * world_width_ + x;
            cell_active_[cell_idx] = region_active_[region_idx];
        }
    }
}

void WorldRegionActivityTracker::snapshotPreviousFields(const World& world)
{
    const WorldData& data = world.getData();
    const size_t cell_count = data.cells.size();

    if (previous_live_pressure_.size() != cell_count) {
        previous_live_pressure_.resize(cell_count, 0.0f);
    }
    if (previous_static_load_.size() != cell_count) {
        previous_static_load_.resize(cell_count, 0.0f);
    }

    for (size_t idx = 0; idx < cell_count; ++idx) {
        previous_live_pressure_[idx] = data.cells[idx].pressure;
        previous_static_load_[idx] = data.cells[idx].static_load;
    }
}

} // namespace DirtSim
