#include "core/GridOfCells.h"
#include "core/PhysicsSettings.h"
#include "core/RegionDebugInfo.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldRegionActivityTracker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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
constexpr int kSettleSteps = 180;
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
    int received_move_cells = 0;
    int generated_moves = 0;
    int received_moves = 0;
    double max_velocity = 0.0;
    double mean_velocity = 0.0;
    double max_abs_velocity_y = 0.0;
    double mean_abs_velocity_y = 0.0;
    double max_abs_pressure_gradient_y = 0.0;
    double mean_abs_pressure_gradient_y = 0.0;
    double mean_pressure = 0.0;
};

struct CellTrack {
    int x = 0;
    int y = 0;
};

std::string dumpPureBuriedRegionTimeSeries(const World& world, RegionCoord region);
void initializeSleepAnalysisWorld(World& world);

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

void settleWorld(World& world, int steps = kSettleSteps, double dt = kDt)
{
    for (int step = 0; step < steps; ++step) {
        world.advanceTime(dt);
    }
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
                const bool interesting = velocity > 0.05 || debug.generated_move_count > 0
                    || debug.received_move_count > 0 || debug.gravity_skipped_for_support
                    || debug.has_granular_support_path || debug.carries_transmitted_granular_load;

                if (!interesting) {
                    continue;
                }

                found_interesting_cell = true;
                const double self_weight =
                    isLoadBearingGranularCell(cell) ? cell.getMass() * std::abs(gravity) : 0.0;
                out << "  (" << x << "," << y << ") mat=" << toString(cell.material_type)
                    << " vel=" << velocity << " com=(" << cell.com.x << "," << cell.com.y << ")"
                    << " load=" << cell.static_load << " self=" << self_weight
                    << " grav=" << gravityStateToChar(cell, debug)
                    << " moves=" << moveStateToChar(debug) << " gen=" << debug.generated_move_count
                    << " recv=" << debug.received_move_count << "\n";
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
    double abs_pressure_gradient_y_sum = 0.0;
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
            const double abs_pressure_gradient_y = std::abs(cell.pressure_gradient.y);

            sample.material_cells++;
            velocity_sum += velocity;
            abs_velocity_y_sum += abs_velocity_y;
            abs_pressure_gradient_y_sum += abs_pressure_gradient_y;
            pressure_sum += cell.pressure;
            sample.max_velocity = std::max(sample.max_velocity, velocity);
            sample.max_abs_velocity_y = std::max(sample.max_abs_velocity_y, abs_velocity_y);
            sample.max_abs_pressure_gradient_y =
                std::max(sample.max_abs_pressure_gradient_y, abs_pressure_gradient_y);

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
            if (debug.received_move_count > 0) {
                sample.received_move_cells++;
            }

            sample.generated_moves += debug.generated_move_count;
            sample.received_moves += debug.received_move_count;
        }
    }

    if (sample.material_cells > 0) {
        const double material_cells = static_cast<double>(sample.material_cells);
        sample.mean_velocity = velocity_sum / material_cells;
        sample.mean_abs_velocity_y = abs_velocity_y_sum / material_cells;
        sample.mean_abs_pressure_gradient_y = abs_pressure_gradient_y_sum / material_cells;
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
                + static_cast<double>(debug.generated_move_count + debug.received_move_count) * 10.0
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

std::string dumpPureBuriedRegionTimeSeries(const World& world, RegionCoord region)
{
    World replay(kWorldWidth, kWorldHeight);
    initializeSleepAnalysisWorld(replay);
    replay.getPhysicsSettings().gravity = world.getPhysicsSettings().gravity;

    settleWorld(replay);
    const std::vector<CellTrack> tracks = selectTrackedCells(replay, region);
    std::ostringstream out;

    out << "Pure buried region time series for (" << region.x << "," << region.y << ")\n";
    out << "frame state wake cells move cBottom cTop skip support carry genC recvC gen recv "
           "maxV meanV maxVy meanVy maxGradY meanGradY meanP\n";

    for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
        const RegionFrameSample sample = sampleRegionFrame(replay, region, frame);
        out << sample.frame << " " << regionStateToChar(sample.state) << " "
            << wakeReasonToChar(sample.wake_reason) << " " << sample.material_cells << " "
            << sample.moving_cells << " " << sample.near_bottom_cells << " "
            << sample.near_top_cells << " " << sample.gravity_skipped_cells << " "
            << sample.support_path_cells << " " << sample.carrying_load_cells << " "
            << sample.generated_move_cells << " " << sample.received_move_cells << " "
            << sample.generated_moves << " " << sample.received_moves << " " << sample.max_velocity
            << " " << sample.mean_velocity << " " << sample.max_abs_velocity_y << " "
            << sample.mean_abs_velocity_y << " " << sample.max_abs_pressure_gradient_y << " "
            << sample.mean_abs_pressure_gradient_y << " " << sample.mean_pressure << "\n";

        if (frame < kDiagnosticFrames) {
            replay.advanceTime(kDt);
        }
    }

    out << "Tracked cells\n";
    for (const CellTrack& track : tracks) {
        out << "(" << track.x << "," << track.y << ")\n";
        out << "  frame vel vy comY pressure gradY grav moves gen recv\n";

        World trackedReplay(kWorldWidth, kWorldHeight);
        initializeSleepAnalysisWorld(trackedReplay);
        trackedReplay.getPhysicsSettings().gravity = world.getPhysicsSettings().gravity;
        settleWorld(trackedReplay);

        for (int frame = 0; frame <= kDiagnosticFrames; ++frame) {
            const WorldData& replayData = trackedReplay.getData();
            const size_t idx = static_cast<size_t>(track.y) * replayData.width + track.x;
            const Cell& cell = replayData.at(track.x, track.y);
            const CellDebug& debug = replayData.debug_info[idx];

            out << "  " << frame << " " << cell.velocity.magnitude() << " " << cell.velocity.y
                << " " << cell.com.y << " " << cell.pressure << " " << cell.pressure_gradient.y
                << " " << gravityStateToChar(cell, debug) << " " << moveStateToChar(debug) << " "
                << debug.generated_move_count << " " << debug.received_move_count << "\n";

            if (frame < kDiagnosticFrames) {
                trackedReplay.advanceTime(kDt);
            }
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
            .expect_quiet_buried_region = false,
        }),
    [](const ::testing::TestParamInfo<GravityCase>& info) { return info.param.name; });
