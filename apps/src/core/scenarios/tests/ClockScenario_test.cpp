#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/scenarios/ClockScenario.h"
#include "server/scenarios/ScenarioRegistry.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class ClockScenarioTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Create scenario with default event configs but random triggering disabled.
        scenario_ = std::make_unique<ClockScenario>(ClockEventConfigs{
            .duck = { .duration = 5.0, .chance_per_second = 0.0, .cooldown = 1.0 },
            .meltdown = { .duration = 5.0, .chance_per_second = 0.0, .cooldown = 1.0 },
            .rain = { .duration = 5.0, .chance_per_second = 0.0, .cooldown = 1.0 },
        });

        // Get required dimensions from scenario metadata.
        const auto& metadata = scenario_->getMetadata();
        world_ = std::make_unique<World>(metadata.requiredWidth, metadata.requiredHeight);

        // Apply scenario setup.
        scenario_->setup(*world_);
    }

    std::unique_ptr<ClockScenario> scenario_;
    std::unique_ptr<World> world_;
};

// =============================================================================
// Setup Tests
// =============================================================================

TEST_F(ClockScenarioTest, Setup_HasWallBorders)
{
    const WorldData& data = world_->getData();

    std::cout << "World size: " << data.width << "x" << data.height << "\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world_) << "\n";

    // Verify top border is all walls.
    for (uint32_t x = 0; x < data.width; ++x) {
        EXPECT_EQ(data.at(x, 0).material_type, MaterialType::WALL)
            << "Top border missing WALL at x=" << x;
    }

    // Verify bottom border is all walls.
    for (uint32_t x = 0; x < data.width; ++x) {
        EXPECT_EQ(data.at(x, data.height - 1).material_type, MaterialType::WALL)
            << "Bottom border missing WALL at x=" << x;
    }

    // Verify left border is all walls.
    for (uint32_t y = 0; y < data.height; ++y) {
        EXPECT_EQ(data.at(0, y).material_type, MaterialType::WALL)
            << "Left border missing WALL at y=" << y;
    }

    // Verify right border is all walls.
    for (uint32_t y = 0; y < data.height; ++y) {
        EXPECT_EQ(data.at(data.width - 1, y).material_type, MaterialType::WALL)
            << "Right border missing WALL at y=" << y;
    }
}

TEST_F(ClockScenarioTest, Setup_HasMinimumDigitBlocks)
{
    const WorldData& data = world_->getData();

    // Count cells that are WALL with render_as set to METAL (digit cells).
    int digit_cell_count = 0;
    for (uint32_t y = 1; y < data.height - 1; ++y) {
        for (uint32_t x = 1; x < data.width - 1; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.material_type == MaterialType::WALL && cell.render_as >= 0) {
                digit_cell_count++;
            }
        }
    }

    std::cout << "Found " << digit_cell_count << " digit cells\n";

    // Should have at least some digit cells (HH:MM = 4 digits minimum).
    // Each 7-segment digit has at least 10 cells, so expect at least 40.
    EXPECT_GE(digit_cell_count, 40)
        << "Expected at least 40 digit cells for HH:MM display";
}

TEST_F(ClockScenarioTest, Setup_NoActiveEvents)
{
    // After setup, no events should be active.
    EXPECT_FALSE(scenario_->isEventActive(ClockEventType::DUCK));
    EXPECT_FALSE(scenario_->isEventActive(ClockEventType::MELTDOWN));
    EXPECT_FALSE(scenario_->isEventActive(ClockEventType::RAIN));
    EXPECT_EQ(scenario_->getActiveEventCount(), 0u);
}

// =============================================================================
// Duck Event Tests
// =============================================================================

TEST_F(ClockScenarioTest, DuckEvent_CanStartManually)
{
    // Get the current config and enable duck.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.duckEnabled = true;
    config.eventFrequency = 0.0;  // Disable random triggering.
    scenario_->setConfig(config, *world_);

    // Duck event should now be active.
    EXPECT_TRUE(scenario_->isEventActive(ClockEventType::DUCK));
    EXPECT_EQ(scenario_->getActiveEventCount(), 1u);
}

TEST_F(ClockScenarioTest, DuckEvent_CompletesAfterDuration)
{
    // Create scenario with short duck duration for faster test.
    // Duration of 0.5s will timeout before the door even finishes opening.
    // Note: chance_per_second = 0.0 prevents random triggering; we don't set
    // eventFrequency = 0.0 because that would skip updateEvents() entirely.
    auto short_scenario = std::make_unique<ClockScenario>(ClockEventConfigs{
        .duck = { .duration = 0.5, .chance_per_second = 0.0, .cooldown = 0.0 },
    });
    const auto& metadata = short_scenario->getMetadata();
    auto short_world = std::make_unique<World>(metadata.requiredWidth, metadata.requiredHeight);
    short_scenario->setup(*short_world);

    // Start duck event manually.
    auto config = std::get<Config::Clock>(short_scenario->getConfig());
    config.duckEnabled = true;
    short_scenario->setConfig(config, *short_world);

    ASSERT_TRUE(short_scenario->isEventActive(ClockEventType::DUCK));

    // Get the configured duration.
    double duration = short_scenario->getEventConfig(ClockEventType::DUCK).duration;
    std::cout << "Duck event duration: " << duration << "s\n";

    // Advance time past the event duration.
    double elapsed = 0.0;
    const double dt = 0.05;  // Small timesteps for accuracy.
    int ticks = 0;

    // Run for duration + buffer time.
    while (short_scenario->isEventActive(ClockEventType::DUCK) && elapsed < duration + 2.0) {
        short_scenario->tick(*short_world, dt);
        short_world->advanceTime(dt);
        elapsed += dt;
        ticks++;
    }

    std::cout << "Event ended after " << elapsed << "s (" << ticks << " ticks)\n";

    // Event should have ended via timeout.
    EXPECT_FALSE(short_scenario->isEventActive(ClockEventType::DUCK))
        << "Duck event should have ended after duration";
    EXPECT_LE(elapsed, duration + 0.5) << "Event should end close to configured duration";
}

TEST_F(ClockScenarioTest, DuckEvent_DoesNotTriggerRandomlyWhenDisabled)
{
    // With eventFrequency = 0 and chance_per_second = 0, no events should trigger.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.eventFrequency = 0.0;
    scenario_->setConfig(config, *world_);

    // Tick many times.
    for (int i = 0; i < 1000; ++i) {
        scenario_->tick(*world_, 0.016);
    }

    // No events should have started.
    EXPECT_EQ(scenario_->getActiveEventCount(), 0u)
        << "No events should trigger with frequency=0";
}

// =============================================================================
// Event Config Tests
// =============================================================================

TEST_F(ClockScenarioTest, EventConfig_CustomDurationIsRespected)
{
    // Create scenario with very short duck duration.
    auto short_scenario = std::make_unique<ClockScenario>(ClockEventConfigs{
        .duck = { .duration = 1.0, .chance_per_second = 0.0, .cooldown = 0.0 },
    });

    const auto& metadata = short_scenario->getMetadata();
    auto short_world = std::make_unique<World>(metadata.requiredWidth, metadata.requiredHeight);
    short_scenario->setup(*short_world);

    // Verify config is correct.
    EXPECT_DOUBLE_EQ(short_scenario->getEventConfig(ClockEventType::DUCK).duration, 1.0);

    // Start duck event.
    auto config = std::get<Config::Clock>(short_scenario->getConfig());
    config.duckEnabled = true;
    short_scenario->setConfig(config, *short_world);

    ASSERT_TRUE(short_scenario->isEventActive(ClockEventType::DUCK));

    // Advance time past 1 second.
    double elapsed = 0.0;
    while (short_scenario->isEventActive(ClockEventType::DUCK) && elapsed < 3.0) {
        short_scenario->tick(*short_world, 0.1);
        short_world->advanceTime(0.1);
        elapsed += 0.1;
    }

    std::cout << "Short event ended after " << elapsed << "s\n";

    // Should have ended around 1 second (plus door closing delay).
    EXPECT_FALSE(short_scenario->isEventActive(ClockEventType::DUCK));
    EXPECT_LT(elapsed, 3.0) << "Event should end well before 3 seconds";
}
