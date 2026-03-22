#include "core/GridOfCells.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldRegionActivityTracker.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

GridOfCells makeGrid(World& world)
{
    WorldData& data = world.getData();
    return GridOfCells(data.cells, data.debug_info, data.width, data.height);
}

void fillWorld(World& world, Material::EnumType material)
{
    WorldData& data = world.getData();
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            world.replaceMaterialAtCell(
                Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(y) }, material);
        }
    }
}

} // namespace

TEST(WorldRegionActivityTrackerTest, QuietHomogeneousRegionFallsAsleep)
{
    World world(8, 8);
    fillWorld(world, Material::EnumType::Dirt);

    GridOfCells grid = makeGrid(world);
    WorldRegionActivityTracker tracker;
    tracker.resize(
        world.getData().width, world.getData().height, grid.getBlocksX(), grid.getBlocksY());
    tracker.setConfig(
        WorldRegionActivityTracker::Config{
            .quiet_frames_to_sleep = 2,
            .live_pressure_delta_epsilon = 0.02f,
            .static_load_delta_epsilon = 0.02f,
            .velocity_epsilon = 0.01f,
        });

    tracker.beginFrame(world, grid, 0);
    tracker.summarizeFrame(world, grid, 0);
    EXPECT_EQ(tracker.getRegionState(0, 0), RegionState::LoadedQuiet);

    tracker.beginFrame(world, grid, 1);
    tracker.summarizeFrame(world, grid, 1);
    EXPECT_EQ(tracker.getRegionState(0, 0), RegionState::Sleeping);
}

TEST(WorldRegionActivityTrackerTest, WakeRequestActivatesRegionHalo)
{
    World world(40, 40);
    fillWorld(world, Material::EnumType::Dirt);

    GridOfCells grid = makeGrid(world);
    WorldRegionActivityTracker tracker;
    tracker.resize(
        world.getData().width, world.getData().height, grid.getBlocksX(), grid.getBlocksY());
    tracker.setConfig(
        WorldRegionActivityTracker::Config{
            .quiet_frames_to_sleep = 1,
            .live_pressure_delta_epsilon = 0.02f,
            .static_load_delta_epsilon = 0.02f,
            .velocity_epsilon = 0.01f,
        });

    tracker.beginFrame(world, grid, 0);
    tracker.summarizeFrame(world, grid, 0);
    EXPECT_EQ(tracker.getRegionState(2, 2), RegionState::Sleeping);

    tracker.noteWakeAtCell(20, 20, WakeReason::ExternalMutation);
    tracker.beginFrame(world, grid, 1);

    EXPECT_TRUE(tracker.isRegionActive(2, 2));
    EXPECT_TRUE(tracker.isRegionActive(1, 1));
    EXPECT_TRUE(tracker.isRegionActive(3, 3));
    EXPECT_FALSE(tracker.isRegionActive(0, 4));
    EXPECT_EQ(tracker.getLastWakeReason(2, 2), WakeReason::ExternalMutation);
    EXPECT_TRUE(tracker.isCellActive(20, 20));
    EXPECT_FALSE(tracker.isCellActive(1, 39));
}

TEST(WorldRegionActivityTrackerTest, WaterAdjacencyPreventsSleeping)
{
    World world(8, 8);
    fillWorld(world, Material::EnumType::Dirt);
    world.clearCellAtPosition(Vector2s{ 4, 4 });
    world.setBulkWaterAmountAtCell(Vector2s{ 4, 4 }, 1.0f);

    GridOfCells grid = makeGrid(world);
    WorldRegionActivityTracker tracker;
    tracker.resize(
        world.getData().width, world.getData().height, grid.getBlocksX(), grid.getBlocksY());
    tracker.setConfig(
        WorldRegionActivityTracker::Config{
            .quiet_frames_to_sleep = 1,
            .live_pressure_delta_epsilon = 0.02f,
            .static_load_delta_epsilon = 0.02f,
            .velocity_epsilon = 0.01f,
            .keep_empty_adjacent_awake = false,
        });

    tracker.beginFrame(world, grid, 0);
    tracker.summarizeFrame(world, grid, 0);

    EXPECT_TRUE(tracker.getRegionSummary(0, 0).has_water_adjacency);
    EXPECT_EQ(tracker.getRegionState(0, 0), RegionState::Awake);
}

TEST(WorldRegionActivityTrackerTest, WallOnlyMixedRegionFallsAsleep)
{
    World world(8, 8);
    fillWorld(world, Material::EnumType::Dirt);
    for (int x = 0; x < 8; ++x) {
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(x), 7 }, Material::EnumType::Wall);
    }

    GridOfCells grid = makeGrid(world);
    WorldRegionActivityTracker tracker;
    tracker.resize(
        world.getData().width, world.getData().height, grid.getBlocksX(), grid.getBlocksY());
    tracker.setConfig(
        WorldRegionActivityTracker::Config{
            .quiet_frames_to_sleep = 2,
            .live_pressure_delta_epsilon = 0.02f,
            .static_load_delta_epsilon = 0.02f,
            .velocity_epsilon = 0.01f,
        });

    tracker.beginFrame(world, grid, 0);
    tracker.summarizeFrame(world, grid, 0);
    EXPECT_FALSE(tracker.getRegionSummary(0, 0).has_mixed_material);
    EXPECT_EQ(tracker.getRegionState(0, 0), RegionState::LoadedQuiet);

    tracker.beginFrame(world, grid, 1);
    tracker.summarizeFrame(world, grid, 1);
    EXPECT_EQ(tracker.getRegionState(0, 0), RegionState::Sleeping);
}

TEST(WorldRegionActivityTrackerTest, MetalMixedRegionStaysAwake)
{
    World world(8, 8);
    fillWorld(world, Material::EnumType::Dirt);
    for (int x = 0; x < 8; ++x) {
        world.replaceMaterialAtCell(
            Vector2s{ static_cast<int16_t>(x), 7 }, Material::EnumType::Metal);
    }

    GridOfCells grid = makeGrid(world);
    WorldRegionActivityTracker tracker;
    tracker.resize(
        world.getData().width, world.getData().height, grid.getBlocksX(), grid.getBlocksY());
    tracker.setConfig(
        WorldRegionActivityTracker::Config{
            .quiet_frames_to_sleep = 1,
            .live_pressure_delta_epsilon = 0.02f,
            .static_load_delta_epsilon = 0.02f,
            .velocity_epsilon = 0.01f,
        });

    tracker.beginFrame(world, grid, 0);
    tracker.summarizeFrame(world, grid, 0);

    EXPECT_TRUE(tracker.getRegionSummary(0, 0).has_mixed_material);
    EXPECT_EQ(tracker.getRegionState(0, 0), RegionState::Awake);
}
