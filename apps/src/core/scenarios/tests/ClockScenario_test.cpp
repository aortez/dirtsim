#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include <gtest/gtest.h>
#include <map>

using namespace DirtSim;

class ClockScenarioTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Create scenario with default event configs but random triggering disabled.
        scenario_ = std::make_unique<ClockScenario>(ClockEventConfigs{
            .color_cycle = { .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 } },
            .color_showcase = { .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 } },
            .digit_slide = { .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 } },
            .duck = {
                .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 },
                .floor_obstacles_enabled = false,
            },
            .marquee = { .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 } },
            .meltdown = { .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 } },
            .rain = { .timing = { .duration = 5.0, .chance = 0.0, .cooldown = 1.0 } },
        });

        // Get required dimensions from scenario metadata.
        const auto& metadata = scenario_->getMetadata();
        world_ = std::make_unique<World>(metadata.requiredWidth, metadata.requiredHeight);

        // Apply scenario setup.
        scenario_->setup(*world_);

        // Run one tick to set first_tick_done_ = true.
        // This is required for manual event toggling via setConfig() to work.
        scenario_->tick(*world_, 0.016);
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

    // Note: Top border intentionally has no walls to allow sunlight to illuminate the world.
    // Only the corner cells (x=0 and x=width-1) have walls from the left/right borders.

    // Verify bottom border is all walls.
    for (int x = 0; x < data.width; ++x) {
        EXPECT_EQ(data.at(x, data.height - 1).material_type, Material::EnumType::Wall)
            << "Bottom border missing WALL at x=" << x;
    }

    // Verify left border is all walls.
    for (int y = 0; y < data.height; ++y) {
        EXPECT_EQ(data.at(0, y).material_type, Material::EnumType::Wall)
            << "Left border missing WALL at y=" << y;
    }

    // Verify right border is all walls.
    for (int y = 0; y < data.height; ++y) {
        EXPECT_EQ(data.at(data.width - 1, y).material_type, Material::EnumType::Wall)
            << "Right border missing WALL at y=" << y;
    }
}

TEST_F(ClockScenarioTest, Setup_HasMinimumDigitBlocks)
{
    const WorldData& data = world_->getData();

    // Count cells that are WALL with render_as set to METAL (digit cells).
    int digit_cell_count = 0;
    for (int y = 1; y < data.height - 1; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                digit_cell_count++;
            }
        }
    }

    std::cout << "Found " << digit_cell_count << " digit cells\n";

    // Should have at least some digit cells (HH:MM = 4 digits minimum).
    // Each 7-segment digit has at least 10 cells, so expect at least 40.
    EXPECT_GE(digit_cell_count, 40) << "Expected at least 40 digit cells for HH:MM display";
}

TEST_F(ClockScenarioTest, Setup_NoActiveEvents)
{
    // After setup, no events should be active.
    EXPECT_FALSE(scenario_->isEventActive(ClockEventType::COLOR_CYCLE));
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
    config.eventFrequency = 0.0; // Disable random triggering.
    scenario_->setConfig(config, *world_);

    // Duck event should now be active.
    EXPECT_TRUE(scenario_->isEventActive(ClockEventType::DUCK));
    EXPECT_EQ(scenario_->getActiveEventCount(), 1u);
}

TEST_F(ClockScenarioTest, DuckEvent_CompletesAfterDuration)
{
    // Create scenario with short duck duration for faster test.
    // Duration of 0.5s will timeout before the door even finishes opening.
    // Note: chance = 0.0 prevents random triggering; we don't set
    // eventFrequency = 0.0 because that would skip updateEvents() entirely.
    auto short_scenario = std::make_unique<ClockScenario>(ClockEventConfigs{
        .color_cycle = {},
        .color_showcase = {},
        .digit_slide = {},
        .duck = {
            .timing = { .duration = 0.5, .chance = 0.0, .cooldown = 0.0 },
            .floor_obstacles_enabled = false,
        },
        .marquee = {},
        .meltdown = {},
        .rain = {},
    });
    const auto& metadata = short_scenario->getMetadata();
    auto short_world = std::make_unique<World>(metadata.requiredWidth, metadata.requiredHeight);
    short_scenario->setup(*short_world);
    short_scenario->tick(*short_world, 0.016); // Enable manual event toggling.

    // Start duck event manually.
    auto config = std::get<Config::Clock>(short_scenario->getConfig());
    config.duckEnabled = true;
    short_scenario->setConfig(config, *short_world);

    ASSERT_TRUE(short_scenario->isEventActive(ClockEventType::DUCK));

    // Get the configured duration.
    double duration = short_scenario->getEventTiming(ClockEventType::DUCK).duration;
    std::cout << "Duck event duration: " << duration << "s\n";

    // Advance time past the event duration.
    double elapsed = 0.0;
    const double dt = 0.05; // Small timesteps for accuracy.
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
    // With eventFrequency = 0 and chance = 0, no events should trigger.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.eventFrequency = 0.0;
    scenario_->setConfig(config, *world_);

    // Tick many times.
    for (int i = 0; i < 1000; ++i) {
        scenario_->tick(*world_, 0.016);
    }

    // No events should have started.
    EXPECT_EQ(scenario_->getActiveEventCount(), 0u) << "No events should trigger with frequency=0";
}

// =============================================================================
// Event Config Tests
// =============================================================================

TEST_F(ClockScenarioTest, DuckEvent_DoorsOpenAndCloseAtCorrectPositions)
{
    // Create scenario with duration long enough to observe full door cycle.
    // Exit door opens at remaining_time <= 7.0, so need duration > 7.0.
    // Door open delay is 2.0s, door close delay is 1.0s.
    auto test_scenario = std::make_unique<ClockScenario>(ClockEventConfigs{
        .color_cycle = {},
        .color_showcase = {},
        .digit_slide = {},
        .duck = {
            .timing = { .duration = 15.0, .chance = 0.0, .cooldown = 0.0 },
            .floor_obstacles_enabled = false,
        },
        .marquee = {},
        .meltdown = {},
        .rain = {},
    });

    const auto& metadata = test_scenario->getMetadata();
    auto test_world = std::make_unique<World>(metadata.requiredWidth, metadata.requiredHeight);
    test_scenario->setup(*test_world);
    test_scenario->tick(*test_world, 0.016); // Enable manual event toggling.

    const WorldData& data = test_world->getData();
    const uint32_t world_width = data.width;
    const uint32_t world_height = data.height;

    // Expected door Y position: one above the floor.
    const int expected_door_y = static_cast<int>(world_height - 2);

    std::cout << "World size: " << world_width << "x" << world_height << "\n";
    std::cout << "Expected door Y: " << expected_door_y << " (one above floor at "
              << world_height - 1 << ")\n";

    // Track door events across all frames.
    struct DoorEvent {
        double time;
        std::string description;
        int door_x;
        int door_y;
    };
    std::vector<DoorEvent> door_events;

    // Track door states.
    bool entrance_door_opened = false;
    bool entrance_door_closed = false;
    bool exit_door_opened = false;
    bool exit_door_closed = false;
    int entrance_door_x = -1;
    int exit_door_x = -1;

    // Start duck event.
    auto config = std::get<Config::Clock>(test_scenario->getConfig());
    config.duckEnabled = true;
    test_scenario->setConfig(config, *test_world);

    ASSERT_TRUE(test_scenario->isEventActive(ClockEventType::DUCK));

    // Helper to check if a position is an open door (AIR at wall position).
    auto is_door_open = [&](int x, int y) -> bool {
        if (x < 0 || x >= static_cast<int>(world_width) || y < 0
            || y >= static_cast<int>(world_height)) {
            return false;
        }
        const Cell& cell = data.at(x, y);
        // Door is open if the wall cell has been cleared to AIR.
        return cell.material_type == Material::EnumType::Air;
    };

    // Run simulation and track all frames.
    double elapsed = 0.0;
    const double dt = 0.05;       // Small timesteps for accuracy.
    const double max_time = 25.0; // Safety limit.

    while (test_scenario->isEventActive(ClockEventType::DUCK) && elapsed < max_time) {
        // Check left door (x=0).
        bool left_door_open = is_door_open(0, expected_door_y);

        // Check right door (x=width-1).
        bool right_door_open = is_door_open(world_width - 1, expected_door_y);

        // Track entrance door opening.
        if (!entrance_door_opened && (left_door_open || right_door_open)) {
            entrance_door_opened = true;
            entrance_door_x = left_door_open ? 0 : static_cast<int>(world_width - 1);
            door_events.push_back(
                { elapsed, "ENTRANCE_DOOR_OPENED", entrance_door_x, expected_door_y });
            std::cout << "t=" << elapsed << "s: Entrance door opened at (" << entrance_door_x
                      << ", " << expected_door_y << ")\n";
        }

        // Track entrance door closing.
        if (entrance_door_opened && !entrance_door_closed) {
            bool entrance_still_open = (entrance_door_x == 0) ? left_door_open : right_door_open;
            if (!entrance_still_open) {
                entrance_door_closed = true;
                door_events.push_back(
                    { elapsed, "ENTRANCE_DOOR_CLOSED", entrance_door_x, expected_door_y });
                std::cout << "t=" << elapsed << "s: Entrance door closed at (" << entrance_door_x
                          << ", " << expected_door_y << ")\n";
            }
        }

        // Track exit door opening (opposite side from entrance).
        if (entrance_door_opened && !exit_door_opened) {
            int potential_exit_x = (entrance_door_x == 0) ? static_cast<int>(world_width - 1) : 0;
            bool exit_open = (potential_exit_x == 0) ? left_door_open : right_door_open;
            if (exit_open) {
                exit_door_opened = true;
                exit_door_x = potential_exit_x;
                door_events.push_back(
                    { elapsed, "EXIT_DOOR_OPENED", exit_door_x, expected_door_y });
                std::cout << "t=" << elapsed << "s: Exit door opened at (" << exit_door_x << ", "
                          << expected_door_y << ")\n";
            }
        }

        // Advance simulation.
        test_scenario->tick(*test_world, dt);
        test_world->advanceTime(dt);
        elapsed += dt;
    }

    // After event ends, check if exit door closed.
    if (exit_door_opened && !exit_door_closed) {
        bool exit_still_open = (exit_door_x == 0) ? is_door_open(0, expected_door_y)
                                                  : is_door_open(world_width - 1, expected_door_y);
        if (!exit_still_open) {
            exit_door_closed = true;
            door_events.push_back({ elapsed, "EXIT_DOOR_CLOSED", exit_door_x, expected_door_y });
            std::cout << "t=" << elapsed << "s: Exit door closed at (" << exit_door_x << ", "
                      << expected_door_y << ")\n";
        }
    }

    // Print summary.
    std::cout << "\n=== Door Event Summary ===\n";
    for (const auto& event : door_events) {
        std::cout << "  t=" << event.time << "s: " << event.description << " at (" << event.door_x
                  << ", " << event.door_y << ")\n";
    }
    std::cout << "Event ended after " << elapsed << "s\n";

    // Verify all door events occurred.
    EXPECT_TRUE(entrance_door_opened) << "Entrance door should have opened";
    EXPECT_TRUE(entrance_door_closed) << "Entrance door should have closed";
    EXPECT_TRUE(exit_door_opened) << "Exit door should have opened";
    EXPECT_TRUE(exit_door_closed) << "Exit door should have closed";

    // Verify door positions are at the edges.
    if (entrance_door_opened) {
        EXPECT_TRUE(entrance_door_x == 0 || entrance_door_x == static_cast<int>(world_width - 1))
            << "Entrance door should be at world edge, got x=" << entrance_door_x;
    }
    if (exit_door_opened) {
        EXPECT_TRUE(exit_door_x == 0 || exit_door_x == static_cast<int>(world_width - 1))
            << "Exit door should be at world edge, got x=" << exit_door_x;
    }

    // Verify entrance and exit are on opposite sides.
    if (entrance_door_opened && exit_door_opened) {
        EXPECT_NE(entrance_door_x, exit_door_x)
            << "Entrance and exit doors should be on opposite sides";
    }

    // Verify all doors were at the correct Y position (one above floor).
    for (const auto& event : door_events) {
        EXPECT_EQ(event.door_y, expected_door_y)
            << "Door at event '" << event.description << "' should be at y=" << expected_door_y
            << " (one above floor)";
    }

    // Verify event completed.
    EXPECT_FALSE(test_scenario->isEventActive(ClockEventType::DUCK));
}

// =============================================================================
// Color Cycle Event Tests
// =============================================================================

TEST_F(ClockScenarioTest, ColorCycleEvent_CyclesThroughMaterials)
{
    // Track material counts for digit cells across all frames.
    std::map<Material::EnumType, int> material_counts;

    // Start color cycle event via config toggle.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.colorCycleEnabled = true;
    scenario_->setConfig(config, *world_);

    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::COLOR_CYCLE));

    // Color cycle uses colorsPerSecond from config.
    const double event_duration = scenario_->getEventTiming(ClockEventType::COLOR_CYCLE).duration;
    const double time_per_color = 1.0 / config.colorsPerSecond;

    std::cout << "Duration: " << event_duration << "s, rate: " << config.colorsPerSecond
              << " colors/sec, time per color: " << time_per_color << "s\n";

    // Run through the event, sampling materials at regular intervals.
    double elapsed = 0.0;
    const double sample_dt = 0.1;

    while (scenario_->isEventActive(ClockEventType::COLOR_CYCLE)
           && elapsed < event_duration + 1.0) {
        // Sample current material from digit cells.
        const WorldData& data = world_->getData();
        for (int y = 1; y < data.height - 1; ++y) {
            for (int x = 1; x < data.width - 1; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                    Material::EnumType render_material =
                        static_cast<Material::EnumType>(cell.render_as);
                    material_counts[render_material]++;
                    goto sampled; // Sample one cell per tick.
                }
            }
        }
    sampled:

        scenario_->tick(*world_, sample_dt);
        world_->advanceTime(sample_dt);
        elapsed += sample_dt;
    }

    std::cout << "Event ran for " << elapsed << "s\n";

    // Print material counts.
    std::cout << "\n=== Material Counts ===\n";
    int total_samples = 0;
    for (const auto& [mat, count] : material_counts) {
        std::cout << "  " << toString(mat) << ": " << count << " samples\n";
        total_samples += count;
    }
    std::cout << "Total samples: " << total_samples << "\n";

    // Verify we saw multiple different materials (at least 3 of the 7).
    EXPECT_GE(material_counts.size(), 3u)
        << "Should have seen at least 3 different materials during color cycling";

    // Verify no single material dominates (none should have > 60% of samples).
    for (const auto& [mat, count] : material_counts) {
        EXPECT_GE(count, 1) << toString(mat) << " should have at least 1 sample";
        EXPECT_LE(count, total_samples * 0.6)
            << toString(mat) << " should not dominate (have more than 60% of samples)";
    }

    // Verify event ended.
    EXPECT_FALSE(scenario_->isEventActive(ClockEventType::COLOR_CYCLE))
        << "Color cycle event should have ended after duration";
}

// =============================================================================
// Digit Slide Event Tests
// =============================================================================

TEST_F(ClockScenarioTest, DigitSlideEvent_AnimatesWhenTimeChanges)
{
    // Helper to find digit Y positions.
    auto getDigitYPositions = [](const WorldData& data) {
        std::vector<int> y_positions;
        for (int y = 1; y < data.height - 1; ++y) {
            for (int x = 1; x < data.width - 1; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                    y_positions.push_back(static_cast<int>(y));
                }
            }
        }
        return y_positions;
    };

    // Set initial time and enable digit slide.
    scenario_->setTimeOverride("1 2 : 3 4");

    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.digitSlideEnabled = true;
    config.eventFrequency = 1.0; // Enable events.
    scenario_->setConfig(config, *world_);

    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::DIGIT_SLIDE));

    // Run a few ticks to establish the initial state.
    for (int i = 0; i < 5; ++i) {
        scenario_->tick(*world_, 0.016);
    }

    // Record initial Y positions.
    auto initial_positions = getDigitYPositions(world_->getData());
    ASSERT_FALSE(initial_positions.empty()) << "Should have digit cells";

    // Find the min/max Y to understand the digit bounds.
    int min_y = *std::min_element(initial_positions.begin(), initial_positions.end());
    int max_y = *std::max_element(initial_positions.begin(), initial_positions.end());
    std::cout << "Initial digit Y range: " << min_y << " to " << max_y << "\n";

    // Change the time (last digit changes from 4 to 5).
    scenario_->setTimeOverride("1 2 : 3 5");

    // Tick once to trigger the animation.
    scenario_->tick(*world_, 0.016);

    // Now tick partway through the animation (animation takes 0.5s at speed 2.0).
    scenario_->tick(*world_, 0.2);

    // Get positions mid-animation.
    auto mid_positions = getDigitYPositions(world_->getData());

    // During animation, we should see digits at different Y positions than before.
    // The old digit slides down (Y increases) and new digit slides in from above.
    int mid_min_y = mid_positions.empty()
        ? min_y
        : *std::min_element(mid_positions.begin(), mid_positions.end());
    int mid_max_y = mid_positions.empty()
        ? max_y
        : *std::max_element(mid_positions.begin(), mid_positions.end());

    std::cout << "Mid-animation digit Y range: " << mid_min_y << " to " << mid_max_y << "\n";

    // The animation should have expanded the Y range (new digit coming from above,
    // old digit sliding down).
    bool animation_visible = (mid_min_y < min_y) || (mid_max_y > max_y);
    EXPECT_TRUE(animation_visible)
        << "Animation should show digits at different Y positions than static display";
}

// =============================================================================
// Marquee Event Tests
// =============================================================================

TEST_F(ClockScenarioTest, MarqueeEvent_EndsWithDigitsAtDefaultPosition)
{
    // Helper to find digit cell positions (WALL cells with render_as set).
    auto getDigitPositions = [](const WorldData& data) {
        std::vector<std::pair<uint32_t, uint32_t>> positions;
        for (int y = 1; y < data.height - 1; ++y) {
            for (int x = 1; x < data.width - 1; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                    positions.emplace_back(x, y);
                }
            }
        }
        return positions;
    };

    // Record default digit positions before starting marquee.
    auto initial_positions = getDigitPositions(world_->getData());
    ASSERT_FALSE(initial_positions.empty()) << "Should have digit cells before marquee";

    std::cout << "Initial digit cells: " << initial_positions.size() << "\n";

    // Start marquee event.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.marqueeEnabled = true;
    scenario_->setConfig(config, *world_);

    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::MARQUEE));

    // Run until the event finishes.
    double elapsed = 0.0;
    const double dt = 0.02;       // Small timesteps.
    const double max_time = 30.0; // Safety limit.

    while (scenario_->isEventActive(ClockEventType::MARQUEE) && elapsed < max_time) {
        scenario_->tick(*world_, dt);
        world_->advanceTime(dt);
        elapsed += dt;
    }

    std::cout << "Marquee event ended after " << elapsed << "s\n";

    // Verify event ended (not just timed out).
    ASSERT_FALSE(scenario_->isEventActive(ClockEventType::MARQUEE))
        << "Marquee event should have ended";
    ASSERT_LT(elapsed, max_time) << "Event should finish before safety timeout";

    // Verify digits are back at default positions.
    auto final_positions = getDigitPositions(world_->getData());

    std::cout << "Final digit cells: " << final_positions.size() << "\n";

    // Digit count should match (same time string).
    EXPECT_EQ(initial_positions.size(), final_positions.size())
        << "Should have same number of digit cells after marquee ends";

    // Digit positions should match the initial positions (viewport_x = 0).
    // Sort both vectors to compare.
    auto sorted_initial = initial_positions;
    auto sorted_final = final_positions;
    std::sort(sorted_initial.begin(), sorted_initial.end());
    std::sort(sorted_final.begin(), sorted_final.end());

    EXPECT_EQ(sorted_initial, sorted_final)
        << "Digits should be at their default positions when marquee ends";
}

// =============================================================================
// Combined Event Tests
// =============================================================================

TEST_F(ClockScenarioTest, ShowcaseWithSlide_MaintainsConsistentMaterial)
{
    // This test verifies that when both COLOR_SHOWCASE and DIGIT_SLIDE events are
    // active, all digit cells maintain a consistent material during animation.
    // Bug: Without proper time tracking, showcase would cycle colors every frame
    // during slide animation, causing visible flashing.

    // Helper to get all digit cell materials.
    auto getDigitMaterials = [](const WorldData& data) {
        std::vector<Material::EnumType> materials;
        for (int y = 1; y < data.height - 1; ++y) {
            for (int x = 1; x < data.width - 1; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                    materials.push_back(static_cast<Material::EnumType>(cell.render_as));
                }
            }
        }
        return materials;
    };

    // Set initial time.
    scenario_->setTimeOverride("1 2 : 3 4");

    // Enable both showcase and slide events.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.colorShowcaseEnabled = true;
    config.digitSlideEnabled = true;
    config.eventFrequency = 1.0; // Enable events.
    scenario_->setConfig(config, *world_);

    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::COLOR_SHOWCASE));
    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::DIGIT_SLIDE));

    // Run a few ticks to establish initial state.
    for (int i = 0; i < 5; ++i) {
        scenario_->tick(*world_, 0.016);
    }

    // Record the initial showcase material.
    auto initial_materials = getDigitMaterials(world_->getData());
    ASSERT_FALSE(initial_materials.empty()) << "Should have digit cells";

    Material::EnumType initial_showcase = initial_materials[0];
    std::cout << "Initial showcase material: " << toString(initial_showcase) << "\n";

    // Change the time to trigger slide animation.
    // Showcase will change color once (expected), then stay consistent.
    scenario_->setTimeOverride("1 2 : 3 5");

    // Tick once to detect the time change and start animation.
    scenario_->tick(*world_, 0.016);

    // Sample materials during the slide animation (should take ~0.5s at speed 2.0).
    // We sample multiple times during the animation to catch any flashing.
    std::map<Material::EnumType, int> material_counts;
    int num_samples = 0;

    for (int frame = 0; frame < 30; ++frame) { // ~0.5s of animation at 60fps.
        scenario_->tick(*world_, 0.016);

        auto frame_materials = getDigitMaterials(world_->getData());
        for (const auto& mat : frame_materials) {
            material_counts[mat]++;
            num_samples++;
        }
    }

    // Print material distribution.
    std::cout << "\n=== Material Distribution During Animation ===\n";
    for (const auto& [mat, count] : material_counts) {
        double pct = 100.0 * count / num_samples;
        std::cout << "  " << toString(mat) << ": " << count << " samples (" << pct << "%)\n";
    }

    // METAL should NOT appear during showcase+slide (that indicates showcase reset bug).
    auto metal_it = material_counts.find(Material::EnumType::Metal);
    EXPECT_TRUE(metal_it == material_counts.end())
        << "Found METAL material during animation. METAL appeared "
        << (metal_it != material_counts.end() ? metal_it->second : 0)
        << " times. This indicates the showcase event incorrectly reset to METAL.";

    // All frames should use the same material (no rapid cycling through all colors).
    // Showcase changes once when time changes, then stays consistent.
    EXPECT_EQ(material_counts.size(), 1u)
        << "Should see exactly 1 material during animation (no rapid cycling). " << "Found "
        << material_counts.size() << " different materials.";
}

TEST_F(ClockScenarioTest, ShowcaseWithMarquee_MaintainsConsistentMaterial)
{
    // Similar test for showcase + marquee combination.

    auto getDigitMaterials = [](const WorldData& data) {
        std::vector<Material::EnumType> materials;
        for (int y = 1; y < data.height - 1; ++y) {
            for (int x = 1; x < data.width - 1; ++x) {
                const Cell& cell = data.at(x, y);
                if (cell.material_type == Material::EnumType::Wall && cell.render_as >= 0) {
                    materials.push_back(static_cast<Material::EnumType>(cell.render_as));
                }
            }
        }
        return materials;
    };

    // Enable both showcase and marquee events.
    auto config = std::get<Config::Clock>(scenario_->getConfig());
    config.colorShowcaseEnabled = true;
    config.marqueeEnabled = true;
    config.eventFrequency = 1.0;
    scenario_->setConfig(config, *world_);

    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::COLOR_SHOWCASE));
    ASSERT_TRUE(scenario_->isEventActive(ClockEventType::MARQUEE));

    // Run a few ticks and get the showcase material.
    for (int i = 0; i < 5; ++i) {
        scenario_->tick(*world_, 0.016);
    }

    auto initial_materials = getDigitMaterials(world_->getData());
    ASSERT_FALSE(initial_materials.empty()) << "Should have digit cells";

    Material::EnumType showcase_material = initial_materials[0];
    std::cout << "Showcase material: " << toString(showcase_material) << "\n";

    // Sample materials during marquee animation.
    std::map<Material::EnumType, int> material_counts;
    int num_samples = 0;

    for (int frame = 0; frame < 60; ++frame) { // ~1s of animation.
        scenario_->tick(*world_, 0.016);
        world_->advanceTime(0.016);

        auto frame_materials = getDigitMaterials(world_->getData());
        for (const auto& mat : frame_materials) {
            material_counts[mat]++;
            num_samples++;
        }
    }

    // Print material distribution.
    std::cout << "\n=== Material Distribution During Marquee ===\n";
    for (const auto& [mat, count] : material_counts) {
        double pct = 100.0 * count / num_samples;
        std::cout << "  " << toString(mat) << ": " << count << " samples (" << pct << "%)\n";
    }

    // METAL should NOT appear during showcase+marquee.
    auto metal_it = material_counts.find(Material::EnumType::Metal);
    if (metal_it != material_counts.end() && showcase_material != Material::EnumType::Metal) {
        FAIL() << "Found METAL material during marquee when showcase material was "
               << toString(showcase_material) << ". METAL appeared " << metal_it->second
               << " times out of " << num_samples << " samples.";
    }

    // Should see at most 2 materials (no rapid cycling).
    EXPECT_LE(material_counts.size(), 2u)
        << "Should see at most 2 different materials during marquee (no rapid cycling)";
}
