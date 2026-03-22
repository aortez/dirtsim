#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/tests/DuckTestUtils.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/clock_scenario/ClockEventTypes.h"
#include "core/scenarios/clock_scenario/DrainManager.h"
#include "core/scenarios/clock_scenario/MeltdownEvent.h"
#include "core/water/WaterSim.h"

#include <chrono>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

using namespace DirtSim;
using namespace DirtSim::Test;

namespace {

struct WaterAccounting {
    float organismVolume = 0.0f;
    float solidVolume = 0.0f;
    float totalVolume = 0.0f;
    float trackedOrganismVolume = 0.0f;
    float airVolume = 0.0f;
    float legacyWaterVolume = 0.0f;
    float renderInvisibleAirVolume = 0.0f;
    float renderVisibleAirVolume = 0.0f;
    int legacyWaterCellCount = 0;
    int renderInvisibleAirCellCount = 0;
    int renderVisibleAirCellCount = 0;
};

constexpr float kUiWaterVisibleVolumeThreshold = 0.5f / 255.0f;

GuidedWaterDrain makeBottomRowGuidedDrain(const World& world, int mouthStartX, int mouthEndX)
{
    const int drainY = world.getData().height - 1;
    return GuidedWaterDrain{
        .guideStartX = 1,
        .guideEndX = static_cast<int16_t>(world.getData().width - 2),
        .guideTopY = static_cast<int16_t>(drainY - 1),
        .guideBottomY = static_cast<int16_t>(drainY - 1),
        .mouthStartX = static_cast<int16_t>(mouthStartX),
        .mouthEndX = static_cast<int16_t>(mouthEndX),
        .mouthY = static_cast<int16_t>(drainY),
        .guideDownwardSpeed = 0.0f,
        .guideLateralSpeed = 8.0f,
        .mouthDownwardSpeed = 12.0f,
        .drainRatePerSecond = 6.0f,
    };
}

WaterAccounting measureWaterAccounting(
    const World& world, std::optional<OrganismId> trackedOrganism = std::nullopt)
{
    WaterAccounting accounting{};
    WaterVolumeView waterVolume{};
    if (!world.tryGetWaterVolumeView(waterVolume)) {
        return accounting;
    }

    const WorldData& data = world.getData();
    const auto& organismGrid = world.getOrganismManager().getGrid();
    if (waterVolume.width != data.width || waterVolume.height != data.height) {
        return accounting;
    }

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const float volume = waterVolume.volume[idx];
            if (volume <= 0.0f) {
                continue;
            }

            accounting.totalVolume += volume;

            const Cell& cell = data.at(x, y);
            if (cell.material_type == Material::EnumType::Water) {
                accounting.legacyWaterVolume += static_cast<float>(cell.fill_ratio);
                accounting.legacyWaterCellCount++;
            }
            const bool isVisibleAir =
                cell.isEmpty() || cell.material_type == Material::EnumType::Air;
            if (isVisibleAir) {
                accounting.airVolume += volume;
                if (volume >= kUiWaterVisibleVolumeThreshold) {
                    accounting.renderVisibleAirVolume += volume;
                    accounting.renderVisibleAirCellCount++;
                }
                else {
                    accounting.renderInvisibleAirVolume += volume;
                    accounting.renderInvisibleAirCellCount++;
                }
            }
            else {
                accounting.solidVolume += volume;
            }

            if (idx < organismGrid.size() && organismGrid[idx] != INVALID_ORGANISM_ID) {
                accounting.organismVolume += volume;
                if (trackedOrganism.has_value() && organismGrid[idx] == *trackedOrganism) {
                    accounting.trackedOrganismVolume += volume;
                }
            }
        }
    }

    return accounting;
}

WaterAccounting measureWaterRect(
    const World& world,
    int minX,
    int minY,
    int maxX,
    int maxY,
    std::optional<OrganismId> trackedOrganism = std::nullopt)
{
    WaterAccounting accounting{};
    WaterVolumeView waterVolume{};
    if (!world.tryGetWaterVolumeView(waterVolume)) {
        return accounting;
    }

    const WorldData& data = world.getData();
    const auto& organismGrid = world.getOrganismManager().getGrid();
    if (waterVolume.width != data.width || waterVolume.height != data.height) {
        return accounting;
    }

    const int clampedMinX = std::max(0, minX);
    const int clampedMinY = std::max(0, minY);
    const int clampedMaxX = std::min(data.width - 1, maxX);
    const int clampedMaxY = std::min(data.height - 1, maxY);

    for (int y = clampedMinY; y <= clampedMaxY; ++y) {
        for (int x = clampedMinX; x <= clampedMaxX; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const float volume = waterVolume.volume[idx];
            if (volume <= 0.0f) {
                continue;
            }

            accounting.totalVolume += volume;

            const Cell& cell = data.at(x, y);
            if (cell.material_type == Material::EnumType::Water) {
                accounting.legacyWaterVolume += static_cast<float>(cell.fill_ratio);
                accounting.legacyWaterCellCount++;
            }
            const bool isVisibleAir =
                cell.isEmpty() || cell.material_type == Material::EnumType::Air;
            if (isVisibleAir) {
                accounting.airVolume += volume;
                if (volume >= kUiWaterVisibleVolumeThreshold) {
                    accounting.renderVisibleAirVolume += volume;
                    accounting.renderVisibleAirCellCount++;
                }
                else {
                    accounting.renderInvisibleAirVolume += volume;
                    accounting.renderInvisibleAirCellCount++;
                }
            }
            else {
                accounting.solidVolume += volume;
            }

            if (idx < organismGrid.size() && organismGrid[idx] != INVALID_ORGANISM_ID) {
                accounting.organismVolume += volume;
                if (trackedOrganism.has_value() && organismGrid[idx] == *trackedOrganism) {
                    accounting.trackedOrganismVolume += volume;
                }
            }
        }
    }

    return accounting;
}

float getMaxBulkWaterAmountInRect(const World& world, int minX, int minY, int maxX, int maxY)
{
    const WorldData& data = world.getData();
    const int clampedMinX = std::max(0, minX);
    const int clampedMinY = std::max(0, minY);
    const int clampedMaxX = std::min(data.width - 1, maxX);
    const int clampedMaxY = std::min(data.height - 1, maxY);

    float maxAmount = 0.0f;
    for (int y = clampedMinY; y <= clampedMaxY; ++y) {
        for (int x = clampedMinX; x <= clampedMaxX; ++x) {
            maxAmount = std::max(maxAmount, world.getBulkWaterAmountAtCell(x, y));
        }
    }

    return maxAmount;
}

float getTotalBulkWaterAmountInRect(const World& world, int minX, int minY, int maxX, int maxY)
{
    const WorldData& data = world.getData();
    const int clampedMinX = std::max(0, minX);
    const int clampedMinY = std::max(0, minY);
    const int clampedMaxX = std::min(data.width - 1, maxX);
    const int clampedMaxY = std::min(data.height - 1, maxY);

    float total = 0.0f;
    for (int y = clampedMinY; y <= clampedMaxY; ++y) {
        for (int x = clampedMinX; x <= clampedMaxX; ++x) {
            total += world.getBulkWaterAmountAtCell(x, y);
        }
    }

    return total;
}

char getWaterCellTag(
    const WorldData& data,
    const std::vector<OrganismId>& organismGrid,
    int x,
    int y,
    std::optional<OrganismId> trackedOrganism = std::nullopt)
{
    const size_t idx = static_cast<size_t>(y) * data.width + x;
    if (idx < organismGrid.size()) {
        if (trackedOrganism.has_value() && organismGrid[idx] == *trackedOrganism) {
            return 'D';
        }
        if (organismGrid[idx] != INVALID_ORGANISM_ID) {
            return 'O';
        }
    }

    const Cell& cell = data.at(x, y);
    if (cell.material_type == Material::EnumType::Water) {
        return 'W';
    }
    if (cell.isEmpty() || cell.material_type == Material::EnumType::Air) {
        return '.';
    }

    return '#';
}

std::string formatWaterWindow(
    const World& world,
    int minX,
    int minY,
    int maxX,
    int maxY,
    std::optional<OrganismId> trackedOrganism = std::nullopt)
{
    WaterVolumeView waterVolume{};
    if (!world.tryGetWaterVolumeView(waterVolume)) {
        return "<no water volume>\n";
    }

    const WorldData& data = world.getData();
    const auto& organismGrid = world.getOrganismManager().getGrid();
    if (waterVolume.width != data.width || waterVolume.height != data.height) {
        return "<water volume size mismatch>\n";
    }

    const int clampedMinX = std::max(0, minX);
    const int clampedMinY = std::max(0, minY);
    const int clampedMaxX = std::min(data.width - 1, maxX);
    const int clampedMaxY = std::min(data.height - 1, maxY);

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "x";
    for (int x = clampedMinX; x <= clampedMaxX; ++x) {
        out << " " << x;
    }
    out << "\n";

    for (int y = clampedMinY; y <= clampedMaxY; ++y) {
        out << "y=" << y;
        for (int x = clampedMinX; x <= clampedMaxX; ++x) {
            const size_t idx = static_cast<size_t>(y) * data.width + x;
            const float volume = waterVolume.volume[idx];
            out << " " << getWaterCellTag(data, organismGrid, x, y, trackedOrganism) << volume;
            if (volume >= kUiWaterVisibleVolumeThreshold) {
                out << "*";
            }
        }
        out << "\n";
    }

    return out.str();
}

void openDrainMouth(World& world, int mouthStartX, int mouthEndX)
{
    const int drainY = world.getData().height - 1;
    for (int x = mouthStartX; x <= mouthEndX; ++x) {
        world.clearCellAtPosition(
            Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(drainY) });
    }
}

} // namespace

TEST(ClockWaterTransitionTest, DrainOpensForBulkWaterOnBottomPlayableRow)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    DrainManager drain;
    const int bottomRow = world.getData().height - 2;
    const int centerX = world.getData().width / 2;
    world.addBulkWaterAtCell(centerX, bottomRow, 1.0f);

    drain.update(world, 0.1, 0.0, std::nullopt);

    EXPECT_TRUE(drain.isOpen());
    EXPECT_EQ(drain.getStartX(), centerX);
    EXPECT_EQ(drain.getEndX(), centerX);
}

TEST(ClockWaterTransitionTest, DrainDissipatesBulkWaterAtOpenDrainCells)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    DrainManager drain;
    drain.update(world, 0.1, 1.0, std::nullopt);
    ASSERT_TRUE(drain.isOpen());

    const int drainX = drain.getStartX();
    const int drainY = world.getData().height - 1;
    world.setBulkWaterAmountAtCell(drainX, drainY, 1.0f);

    drain.update(world, 0.05, 1.0, std::nullopt);
    world.advanceTime(0.05);

    EXPECT_GT(world.getBulkWaterAmountAtCell(drainX, drainY), 0.0f);
    EXPECT_LT(world.getBulkWaterAmountAtCell(drainX, drainY), 1.0f);
}

TEST(ClockWaterTransitionTest, DrainStaysOpenWhileGuideAreaStillContainsBulkWater)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    DrainManager drain;
    drain.update(world, 0.1, 1.0, std::nullopt);
    ASSERT_TRUE(drain.isOpen());

    const int guideTopY = world.getData().height - 5;
    world.addBulkWaterAtCell(2, guideTopY, 1.0f);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    drain.update(world, 0.1, 0.0, std::nullopt);

    EXPECT_TRUE(drain.isOpen());
}

TEST(ClockWaterTransitionTest, GuidedWaterDrainPullsBottomRowWaterTowardOpenMouth)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    DrainManager drain;
    drain.update(world, 0.1, 1.0, std::nullopt);
    ASSERT_TRUE(drain.isOpen());

    const int bottomRow = world.getData().height - 2;
    const int mouthX = drain.getStartX();
    const int sourceX = mouthX - 2;
    ASSERT_GE(sourceX, 1);

    world.addBulkWaterAtCell(sourceX, bottomRow, 1.0f);

    world.queueGuidedWaterDrain(
        GuidedWaterDrain{
            .guideStartX = 1,
            .guideEndX = static_cast<int16_t>(world.getData().width - 2),
            .guideTopY = static_cast<int16_t>(bottomRow),
            .guideBottomY = static_cast<int16_t>(bottomRow),
            .mouthStartX = drain.getStartX(),
            .mouthEndX = drain.getEndX(),
            .mouthY = static_cast<int16_t>(world.getData().height - 1),
            .guideDownwardSpeed = 0.0f,
            .guideLateralSpeed = 8.0f,
            .mouthDownwardSpeed = 12.0f,
            .drainRatePerSecond = 6.0f,
        });
    world.advanceTime(0.05);

    EXPECT_LT(world.getBulkWaterAmountAtCell(sourceX, bottomRow), 1.0f);
    EXPECT_GT(world.getBulkWaterAmountAtCell(sourceX + 1, bottomRow), 0.0f);
}

TEST(ClockWaterTransitionTest, GuidedWaterDrainPullsUpperRowWaterDownTowardBottomGuideRow)
{
    World world(11, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    const int drainY = world.getData().height - 1;
    const int bottomGuideRow = drainY - 1;
    const int upperGuideRow = bottomGuideRow - 2;
    const int x = 3;

    world.addBulkWaterAtCell(x, upperGuideRow, 1.0f);

    world.queueGuidedWaterDrain(
        GuidedWaterDrain{
            .guideStartX = 1,
            .guideEndX = static_cast<int16_t>(world.getData().width - 2),
            .guideTopY = static_cast<int16_t>(upperGuideRow),
            .guideBottomY = static_cast<int16_t>(bottomGuideRow),
            .mouthStartX = static_cast<int16_t>(world.getData().width / 2),
            .mouthEndX = static_cast<int16_t>(world.getData().width / 2),
            .mouthY = static_cast<int16_t>(drainY),
            .guideDownwardSpeed = 6.0f,
            .guideLateralSpeed = 0.0f,
            .mouthDownwardSpeed = 0.0f,
            .drainRatePerSecond = 0.0f,
        });
    world.advanceTime(0.05);

    EXPECT_LT(world.getBulkWaterAmountAtCell(x, upperGuideRow), 1.0f);
    EXPECT_GT(world.getBulkWaterAmountAtCell(x, upperGuideRow + 1), 0.0f);
}

TEST(ClockWaterTransitionTest, GuidedDrainPullsLeftWaterWithoutCrossingToRightOfMouth)
{
    auto world = createFlatWorld(24, 8);
    world->getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    const int bottomRow = world->getData().height - 2;
    const int duckX = world->getData().width - 4;

    auto brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brainPtr = brain.get();
    const OrganismId duckId =
        world->getOrganismManager().createDuck(*world, duckX, bottomRow, std::move(brain));
    ASSERT_NE(duckId, INVALID_ORGANISM_ID);
    brainPtr->setAction(DuckAction::WAIT);

    for (int i = 0; i < 12; ++i) {
        world->advanceTime(0.016);
    }

    for (int x = 2; x <= 5; ++x) {
        world->setBulkWaterAmountAtCell(x, bottomRow, 1.0f);
    }

    DrainManager drain;
    constexpr double kDt = 0.016;

    float maxTowardDrainVolume = 0.0f;
    float maxRightSideWater = 0.0f;

    for (int frame = 0; frame < 40; ++frame) {
        drain.update(*world, kDt, 0.0, std::nullopt);
        if (frame == 0) {
            EXPECT_TRUE(drain.isOpen());
            EXPECT_EQ(drain.getStartX(), world->getData().width / 2);
            EXPECT_EQ(drain.getEndX(), world->getData().width / 2);
        }

        world->advanceTime(kDt);

        const float towardDrainVolume = getTotalBulkWaterAmountInRect(
            *world, 6, bottomRow, drain.getStartX(), world->getData().height - 1);
        maxTowardDrainVolume = std::max(maxTowardDrainVolume, towardDrainVolume);

        const float rightSideWater = getMaxBulkWaterAmountInRect(
            *world,
            drain.getEndX() + 1,
            1,
            world->getData().width - 2,
            world->getData().height - 1);
        maxRightSideWater = std::max(maxRightSideWater, rightSideWater);

        EXPECT_LT(rightSideWater, 0.0002f) << "frame=" << frame;
    }

    EXPECT_LT(world->getBulkWaterAmountAtCell(2, bottomRow), 0.95f);
    EXPECT_GT(maxTowardDrainVolume, 0.05f);
    EXPECT_LT(maxRightSideWater, 0.0002f);
}

TEST(ClockWaterTransitionTest, DuckCellCanHoldHiddenMacWaterUntilNextStep)
{
    auto world = createFlatWorld(16, 8);
    world->getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    const int duckX = 4;
    const int duckY = world->getData().height - 2;
    const int mouthX = world->getData().width / 2;

    openDrainMouth(*world, mouthX, mouthX);
    world->setBulkWaterAmountAtCell(duckX, duckY, 1.0f);

    auto brain = std::make_unique<TestDuckBrain>();
    brain->setAction(DuckAction::WAIT);
    const OrganismId duckId =
        world->getOrganismManager().createDuck(*world, duckX, duckY, std::move(brain));

    const WaterAccounting before = measureWaterAccounting(*world, duckId);
    EXPECT_NEAR(before.totalVolume, 1.0f, 0.001f);
    EXPECT_NEAR(before.organismVolume, 1.0f, 0.001f);
    EXPECT_NEAR(before.trackedOrganismVolume, 1.0f, 0.001f);
    EXPECT_NEAR(before.airVolume, 0.0f, 0.001f);

    world->queueGuidedWaterDrain(makeBottomRowGuidedDrain(*world, mouthX, mouthX));
    world->advanceTime(0.016);

    const WaterAccounting after = measureWaterAccounting(*world, duckId);
    EXPECT_LT(after.organismVolume, 0.001f);
    EXPECT_LT(after.trackedOrganismVolume, 0.001f);
    EXPECT_GT(after.renderVisibleAirVolume, 0.25f);
}

TEST(ClockWaterTransitionTest, DISABLED_GuidedDrainDuckWaterAccountingDiagnostics)
{
    auto world = createFlatWorld(20, 8);
    world->getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    const int duckY = world->getData().height - 2;
    const int mouthX = world->getData().width / 2;
    openDrainMouth(*world, mouthX, mouthX);

    for (int x = 2; x <= 6; ++x) {
        world->setBulkWaterAmountAtCell(x, duckY, 1.0f);
    }

    auto brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brainPtr = brain.get();
    const OrganismId duckId =
        world->getOrganismManager().createDuck(*world, 4, duckY, std::move(brain));
    Duck* duck = world->getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);

    brainPtr->setAction(DuckAction::RUN_RIGHT);

    constexpr double kDt = 0.016;
    for (int frame = 0; frame < 24; ++frame) {
        const WaterAccounting before = measureWaterAccounting(*world, duckId);
        const Vector2i duckPos = duck->getAnchorCell();
        const WaterAccounting duckWindow = measureWaterRect(
            *world, duckPos.x - 2, duckPos.y - 1, duckPos.x + 2, duckPos.y + 1, duckId);
        const WaterAccounting bottomRows = measureWaterRect(
            *world,
            1,
            world->getData().height - 3,
            world->getData().width - 2,
            world->getData().height - 2,
            duckId);

        std::cout << "frame=" << frame << " duck=(" << duckPos.x << "," << duckPos.y
                  << ") total=" << before.totalVolume << " air=" << before.airVolume
                  << " visible_air=" << before.renderVisibleAirVolume
                  << " invisible_air=" << before.renderInvisibleAirVolume
                  << " hidden_in_duck=" << before.trackedOrganismVolume
                  << " legacy=" << before.legacyWaterVolume
                  << " legacy_cells=" << before.legacyWaterCellCount
                  << " | duck_window visible=" << duckWindow.renderVisibleAirVolume
                  << " invisible=" << duckWindow.renderInvisibleAirVolume
                  << " hidden_in_duck=" << duckWindow.trackedOrganismVolume
                  << " | bottom_rows visible=" << bottomRows.renderVisibleAirVolume
                  << " invisible=" << bottomRows.renderInvisibleAirVolume
                  << " invisible_cells=" << bottomRows.renderInvisibleAirCellCount
                  << " legacy=" << bottomRows.legacyWaterVolume
                  << " legacy_cells=" << bottomRows.legacyWaterCellCount << "\n";

        world->queueGuidedWaterDrain(makeBottomRowGuidedDrain(*world, mouthX, mouthX));
        world->advanceTime(kDt);
    }
}

TEST(ClockWaterTransitionTest, DISABLED_ClockScenarioGuidedDrainDuckDiagnostics)
{
    ClockScenario scenario{};
    const ScenarioMetadata& metadata = scenario.getMetadata();

    World world(metadata.requiredWidth, metadata.requiredHeight);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setScenario(&scenario);
    scenario.setup(world);

    const int bottomRow = world.getData().height - 2;
    const int drainX = world.getData().width / 2;

    for (int x = 2; x <= 8; ++x) {
        world.setBulkWaterAmountAtCell(x, bottomRow, 1.0f);
    }

    auto brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brainPtr = brain.get();
    const OrganismId duckId =
        world.getOrganismManager().createDuck(world, drainX - 3, bottomRow, std::move(brain));
    Duck* duck = world.getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);
    brainPtr->setAction(DuckAction::RUN_RIGHT);

    constexpr double kDt = 0.016;
    for (int frame = 0; frame < 120; ++frame) {
        const Vector2i duckPos = duck->getAnchorCell();
        const WaterAccounting full = measureWaterAccounting(world, duckId);
        const WaterAccounting duckWindow = measureWaterRect(
            world, duckPos.x - 3, duckPos.y - 1, duckPos.x + 3, duckPos.y + 1, duckId);
        const WaterAccounting bottomRows = measureWaterRect(
            world,
            1,
            world.getData().height - 3,
            world.getData().width - 2,
            world.getData().height - 2,
            duckId);

        std::cout << "frame=" << frame << " duck=(" << duckPos.x << "," << duckPos.y
                  << ") total=" << full.totalVolume
                  << " visible_air=" << full.renderVisibleAirVolume
                  << " invisible_air=" << full.renderInvisibleAirVolume
                  << " hidden_in_duck=" << full.trackedOrganismVolume
                  << " legacy=" << full.legacyWaterVolume
                  << " legacy_cells=" << full.legacyWaterCellCount
                  << " | duck_window visible=" << duckWindow.renderVisibleAirVolume
                  << " invisible=" << duckWindow.renderInvisibleAirVolume
                  << " hidden_in_duck=" << duckWindow.trackedOrganismVolume
                  << " | bottom_rows visible=" << bottomRows.renderVisibleAirVolume
                  << " invisible=" << bottomRows.renderInvisibleAirVolume
                  << " invisible_cells=" << bottomRows.renderInvisibleAirCellCount
                  << " legacy=" << bottomRows.legacyWaterVolume
                  << " legacy_cells=" << bottomRows.legacyWaterCellCount << "\n";

        world.advanceTime(kDt);
    }
}

TEST(ClockWaterTransitionTest, DISABLED_ClockScenarioGuidedDrainDuckBurstWindowDiagnostics)
{
    ClockScenario scenario{};
    const ScenarioMetadata& metadata = scenario.getMetadata();

    World world(metadata.requiredWidth, metadata.requiredHeight);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setScenario(&scenario);
    scenario.setup(world);

    const int bottomRow = world.getData().height - 2;
    const int drainX = world.getData().width / 2;

    for (int x = 2; x <= 8; ++x) {
        world.setBulkWaterAmountAtCell(x, bottomRow, 1.0f);
    }

    auto brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brainPtr = brain.get();
    const OrganismId duckId =
        world.getOrganismManager().createDuck(world, drainX - 3, bottomRow, std::move(brain));
    Duck* duck = world.getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);
    brainPtr->setAction(DuckAction::RUN_RIGHT);

    constexpr double kDt = 0.016;
    int dryStreak = 0;
    std::string previousWindowDump;
    WaterAccounting previousDuckWindow{};
    bool hasPreviousState = false;

    for (int frame = 0; frame < 180; ++frame) {
        const Vector2i duckPos = duck->getAnchorCell();
        const WaterAccounting duckWindow = measureWaterRect(
            world, duckPos.x - 4, duckPos.y - 1, duckPos.x + 4, duckPos.y + 1, duckId);
        const std::string currentWindowDump = formatWaterWindow(
            world, duckPos.x - 4, duckPos.y - 1, duckPos.x + 4, duckPos.y + 1, duckId);

        if (duckWindow.renderVisibleAirCellCount == 0) {
            dryStreak++;
        }
        else {
            if (hasPreviousState && dryStreak >= 3) {
                std::cout << "visible burst frame=" << frame << " dry_streak=" << dryStreak
                          << " duck=(" << duckPos.x << "," << duckPos.y << ")\n";
                std::cout << "previous duck window visible="
                          << previousDuckWindow.renderVisibleAirVolume
                          << " invisible=" << previousDuckWindow.renderInvisibleAirVolume
                          << " hidden_in_duck=" << previousDuckWindow.trackedOrganismVolume
                          << " legacy=" << previousDuckWindow.legacyWaterVolume
                          << " legacy_cells=" << previousDuckWindow.legacyWaterCellCount << "\n";
                std::cout << previousWindowDump;
                std::cout << "current duck window visible=" << duckWindow.renderVisibleAirVolume
                          << " invisible=" << duckWindow.renderInvisibleAirVolume
                          << " hidden_in_duck=" << duckWindow.trackedOrganismVolume
                          << " legacy=" << duckWindow.legacyWaterVolume
                          << " legacy_cells=" << duckWindow.legacyWaterCellCount << "\n";
                std::cout << currentWindowDump;
                return;
            }
            dryStreak = 0;
        }

        previousDuckWindow = duckWindow;
        previousWindowDump = currentWindowDump;
        hasPreviousState = true;
        world.advanceTime(kDt);
    }

    std::cout << "No visible burst after a dry streak was detected in this clock seed.\n";
}

TEST(ClockWaterTransitionTest, DISABLED_GuidedDrainInvisibleFilmDuckBurstDiagnostics)
{
    auto world = createFlatWorld(24, 8);
    world->getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    const int bottomRow = world->getData().height - 2;
    const int mouthX = world->getData().width / 2;
    openDrainMouth(*world, mouthX, mouthX);

    const float invisibleFilmVolume = kUiWaterVisibleVolumeThreshold * 0.95f;
    for (int x = 2; x <= world->getData().width - 3; ++x) {
        world->setBulkWaterAmountAtCell(x, bottomRow, invisibleFilmVolume);
    }

    auto brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brainPtr = brain.get();
    const OrganismId duckId =
        world->getOrganismManager().createDuck(*world, 4, bottomRow, std::move(brain));
    Duck* duck = world->getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);
    brainPtr->setAction(DuckAction::RUN_RIGHT);

    constexpr double kDt = 0.016;
    int dryStreak = 0;
    std::string previousWindowDump;
    WaterAccounting previousDuckWindow{};
    bool hasPreviousState = false;

    for (int frame = 0; frame < 120; ++frame) {
        const Vector2i duckPos = duck->getAnchorCell();
        const WaterAccounting full = measureWaterAccounting(*world, duckId);
        const WaterAccounting duckWindow = measureWaterRect(
            *world, duckPos.x - 4, duckPos.y - 1, duckPos.x + 4, duckPos.y + 1, duckId);
        const std::string currentWindowDump = formatWaterWindow(
            *world, duckPos.x - 4, duckPos.y - 1, duckPos.x + 4, duckPos.y + 1, duckId);

        if (duckWindow.renderVisibleAirCellCount == 0) {
            dryStreak++;
        }
        else {
            if (hasPreviousState && dryStreak >= 3) {
                std::cout << "invisible-film burst frame=" << frame << " dry_streak=" << dryStreak
                          << " duck=(" << duckPos.x << "," << duckPos.y
                          << ") total=" << full.totalVolume
                          << " visible_air=" << full.renderVisibleAirVolume
                          << " invisible_air=" << full.renderInvisibleAirVolume
                          << " legacy=" << full.legacyWaterVolume
                          << " legacy_cells=" << full.legacyWaterCellCount << "\n";
                std::cout << "previous duck window visible="
                          << previousDuckWindow.renderVisibleAirVolume
                          << " invisible=" << previousDuckWindow.renderInvisibleAirVolume
                          << " hidden_in_duck=" << previousDuckWindow.trackedOrganismVolume
                          << " legacy=" << previousDuckWindow.legacyWaterVolume
                          << " legacy_cells=" << previousDuckWindow.legacyWaterCellCount << "\n";
                std::cout << previousWindowDump;
                std::cout << "current duck window visible=" << duckWindow.renderVisibleAirVolume
                          << " invisible=" << duckWindow.renderInvisibleAirVolume
                          << " hidden_in_duck=" << duckWindow.trackedOrganismVolume
                          << " legacy=" << duckWindow.legacyWaterVolume
                          << " legacy_cells=" << duckWindow.legacyWaterCellCount << "\n";
                std::cout << currentWindowDump;
                return;
            }
            dryStreak = 0;
        }

        previousDuckWindow = duckWindow;
        previousWindowDump = currentWindowDump;
        hasPreviousState = true;
        world->queueGuidedWaterDrain(makeBottomRowGuidedDrain(*world, mouthX, mouthX));
        world->advanceTime(kDt);
    }

    std::cout << "No visible burst after a dry streak was detected in the invisible-film repro.\n";
}

TEST(ClockWaterTransitionTest, DISABLED_ClockScenarioGuidedDrainDelayedDuckDiagnostics)
{
    ClockScenario scenario{};
    const ScenarioMetadata& metadata = scenario.getMetadata();

    World world(metadata.requiredWidth, metadata.requiredHeight);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;
    world.setScenario(&scenario);
    scenario.setup(world);

    const int bottomRow = world.getData().height - 2;
    const int drainX = world.getData().width / 2;

    for (int x = 2; x <= 8; ++x) {
        world.setBulkWaterAmountAtCell(x, bottomRow, 1.0f);
    }

    constexpr double kDt = 0.016;
    bool foundMostlyDryInvisibleState = false;
    for (int frame = 0; frame < 1600; ++frame) {
        const WaterAccounting bottomRows = measureWaterRect(
            world,
            1,
            world.getData().height - 3,
            world.getData().width - 2,
            world.getData().height - 2);

        if (bottomRows.renderVisibleAirVolume < 0.05f
            && bottomRows.renderInvisibleAirVolume > 0.002f) {
            std::cout << "delayed-duck prestate frame=" << frame
                      << " visible=" << bottomRows.renderVisibleAirVolume
                      << " invisible=" << bottomRows.renderInvisibleAirVolume
                      << " invisible_cells=" << bottomRows.renderInvisibleAirCellCount << "\n";
            foundMostlyDryInvisibleState = true;
            break;
        }

        world.advanceTime(kDt);
    }

    if (!foundMostlyDryInvisibleState) {
        std::cout << "No mostly-dry invisible-water prestate was detected in this clock seed.\n";
        return;
    }

    auto brain = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brainPtr = brain.get();
    const OrganismId duckId =
        world.getOrganismManager().createDuck(world, drainX - 3, bottomRow, std::move(brain));
    Duck* duck = world.getOrganismManager().getDuck(duckId);
    ASSERT_NE(duck, nullptr);
    brainPtr->setAction(DuckAction::RUN_RIGHT);

    int dryStreak = 0;
    std::string previousWindowDump;
    WaterAccounting previousDuckWindow{};
    bool hasPreviousState = false;

    for (int frame = 0; frame < 240; ++frame) {
        const Vector2i duckPos = duck->getAnchorCell();
        const WaterAccounting full = measureWaterAccounting(world, duckId);
        const WaterAccounting duckWindow = measureWaterRect(
            world, duckPos.x - 4, duckPos.y - 1, duckPos.x + 4, duckPos.y + 1, duckId);
        const std::string currentWindowDump = formatWaterWindow(
            world, duckPos.x - 4, duckPos.y - 1, duckPos.x + 4, duckPos.y + 1, duckId);

        if (duckWindow.renderVisibleAirCellCount == 0) {
            dryStreak++;
        }
        else {
            if (hasPreviousState && dryStreak >= 3) {
                std::cout << "delayed-duck burst frame=" << frame << " dry_streak=" << dryStreak
                          << " duck=(" << duckPos.x << "," << duckPos.y
                          << ") total=" << full.totalVolume
                          << " visible_air=" << full.renderVisibleAirVolume
                          << " invisible_air=" << full.renderInvisibleAirVolume
                          << " legacy=" << full.legacyWaterVolume
                          << " legacy_cells=" << full.legacyWaterCellCount << "\n";
                std::cout << "previous duck window visible="
                          << previousDuckWindow.renderVisibleAirVolume
                          << " invisible=" << previousDuckWindow.renderInvisibleAirVolume
                          << " hidden_in_duck=" << previousDuckWindow.trackedOrganismVolume
                          << " legacy=" << previousDuckWindow.legacyWaterVolume
                          << " legacy_cells=" << previousDuckWindow.legacyWaterCellCount << "\n";
                std::cout << previousWindowDump;
                std::cout << "current duck window visible=" << duckWindow.renderVisibleAirVolume
                          << " invisible=" << duckWindow.renderInvisibleAirVolume
                          << " hidden_in_duck=" << duckWindow.trackedOrganismVolume
                          << " legacy=" << duckWindow.legacyWaterVolume
                          << " legacy_cells=" << duckWindow.legacyWaterCellCount << "\n";
                std::cout << currentWindowDump;
                return;
            }
            dryStreak = 0;
        }

        previousDuckWindow = duckWindow;
        previousWindowDump = currentWindowDump;
        hasPreviousState = true;
        world.advanceTime(kDt);
    }

    std::cout
        << "No visible burst after a dry streak was detected in the delayed-duck clock repro.\n";
}

TEST(ClockWaterTransitionTest, MeltdownConvertsFallenDigitsToBulkWaterInMacMode)
{
    World world(9, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    MeltdownEventState state{};
    state.digit_material = Material::EnumType::Metal;

    const int aboveBottomY = world.getData().height - 2;
    const int bottomWallY = world.getData().height - 1;
    world.getData().at(3, aboveBottomY).replaceMaterial(Material::EnumType::Metal, 0.75f);
    world.getData().at(4, bottomWallY).replaceMaterial(Material::EnumType::Metal, 0.5f);

    double remainingTime = 10.0;
    ClockEvents::updateMeltdown(state, world, remainingTime, 20.0, true, 4, 4);

    EXPECT_TRUE(world.getData().at(3, aboveBottomY).isEmpty());
    EXPECT_TRUE(world.getData().at(4, bottomWallY).isEmpty());
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(3, aboveBottomY), 0.75f, 0.001f);
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(4, bottomWallY), 0.5f, 0.001f);
}

TEST(ClockWaterTransitionTest, MeltdownEndConvertsRemainingDigitsToBulkWaterInMacMode)
{
    World world(9, 8);
    world.getPhysicsSettings().water_sim_mode = WaterSimMode::MacProjection;

    world.getData().at(2, 2).replaceMaterial(Material::EnumType::Metal, 0.4f);
    world.getData().at(5, 4).replaceMaterial(Material::EnumType::Metal, 0.9f);

    ClockEvents::endMeltdown(world, Material::EnumType::Metal);

    EXPECT_TRUE(world.getData().at(2, 2).isEmpty());
    EXPECT_TRUE(world.getData().at(5, 4).isEmpty());
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(2, 2), 0.4f, 0.001f);
    EXPECT_NEAR(world.getBulkWaterAmountAtCell(5, 4), 0.9f, 0.001f);
}
