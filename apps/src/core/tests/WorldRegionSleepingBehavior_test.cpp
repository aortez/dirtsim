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
#include <sstream>
#include <string>
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

struct GravityCase {
    const char* name = "";
    double gravity = 0.0;
    bool expect_quiet_buried_region = false;
};

struct RegionCoord {
    int x = 0;
    int y = 0;
};

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

bool regionHasEmptyAdjacency(World& world, GridOfCells& grid, int block_x, int block_y)
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

std::vector<RegionCoord> collectBuriedMaterialRegions(World& world, GridOfCells& grid)
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

std::string dumpBuriedExposureMap(World& world, GridOfCells& grid)
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

    SCOPED_TRACE(dumpRegionStates(world));
    SCOPED_TRACE(dumpWakeReasons(world));
    SCOPED_TRACE(dumpBuriedExposureMap(world, grid));
    SCOPED_TRACE(dumpBuriedRegionDetails(world, buried_regions));

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

    SCOPED_TRACE(dumpRegionStates(world));
    SCOPED_TRACE(dumpWakeReasons(world));
    SCOPED_TRACE(dumpBuriedExposureMap(world, grid));
    SCOPED_TRACE(dumpBuriedRegionDetails(world, buried_regions));

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

    SCOPED_TRACE(dumpRegionStates(world));
    SCOPED_TRACE(dumpWakeReasons(world));
    SCOPED_TRACE(dumpBuriedExposureMap(world, grid));
    SCOPED_TRACE(dumpBuriedRegionDetails(world, buried_regions));

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
