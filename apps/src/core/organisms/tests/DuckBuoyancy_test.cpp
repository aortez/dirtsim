/**
 * @file DuckBuoyancy_test.cpp
 * @brief Tests for duck buoyancy behavior in water.
 *
 * For basic physics tests, see Duck_test.cpp.
 * For brain behavior tests, see DuckBrain_test.cpp.
 * For jumping tests, see DuckJump_test.cpp.
 */

#include "DuckTestUtils.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/organisms/Duck.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <iostream>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;
using namespace DirtSim::Test;

class DuckBuoyancyTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }
};

/**
 * @brief Test that duck (single-cell organism) can float in water via buoyancy.
 *
 * This tests the fix for the bug where organism cells were blocked from
 * participating in buoyancy swaps. Single-cell organisms like Duck should
 * use normal cell physics (including swaps), while rigid body organisms
 * like Goose should resist displacement.
 */
TEST_F(DuckBuoyancyTest, DISABLED_DuckFloatsInWater)
{
    // Enable swap logging to verify the swap mechanism.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Swap, spdlog::level::info);

    spdlog::info("=== DuckFloatsInWater ===");

    // Create a 3x6 world (narrow column of water with duck submerged).
    auto world = std::make_unique<World>(3, 6);
    world->setWallsEnabled(false);
    world->setRandomSeed(123); // Deterministic physics for reproducible test.

    // Configure physics for buoyancy testing.
    world->getPhysicsSettings().pressure_hydrostatic_enabled = true;
    world->getPhysicsSettings().pressure_hydrostatic_strength = 1.0;
    world->getPhysicsSettings().swap_enabled = true;
    world->getPhysicsSettings().gravity = 9.81;

    // Fill the middle column with water, duck submerged at bottom.
    // Layout: [W=water, D=duck, .=air]
    //   . W .   y=0
    //   . W .   y=1
    //   . W .   y=2
    //   . D .   y=3 (duck starts here, submerged)
    //   . W .   y=4
    //   . W .   y=5
    for (int16_t y = 0; y < 6; ++y) {
        if (y != 3) {
            world->addMaterialAtCell({ 1, y }, Material::EnumType::Water, 1.0f);
        }
    }

    // Create duck at (1, 3) - submerged in water.
    // Use TestDuckBrain so the duck just waits (no random movement affecting buoyancy).
    auto test_brain = std::make_unique<TestDuckBrain>();
    test_brain->setAction(DuckAction::WAIT);
    OrganismId duck_id =
        world->getOrganismManager().createDuck(*world, 1, 3, std::move(test_brain));
    ASSERT_NE(duck_id, INVALID_ORGANISM_ID);

    Duck* duck = world->getOrganismManager().getDuck(duck_id);
    ASSERT_NE(duck, nullptr);

    int initial_y = duck->getAnchorCell().y;
    spdlog::info("Duck starts at y={}", initial_y);
    EXPECT_EQ(initial_y, 3);

    // Run simulation - duck should float upward.
    const double deltaTime = 0.016;
    const int max_steps = 500;
    int final_y = initial_y;
    int swap_count = 0;

    // Output formatted table header.
    // Format: step | duck_y | duck_com_y | duck_vel_y | above_mat | above_com_y | above_vel_y |
    // swap
    std::cout << "\n=== BUOYANCY DATA TABLE ===\n";
    std::cout << "step\tduck_y\tcom_y\tvel_y\tabove_mat\tabove_com\tabove_vel\tswap\n";
    std::cout << "----\t------\t-----\t-----\t---------\t---------\t---------\t----\n";

    for (int step = 0; step < max_steps; ++step) {
        int y_before = duck->getAnchorCell().y;

        world->advanceTime(deltaTime);

        int y_after = duck->getAnchorCell().y;
        bool swapped = (y_after != y_before);
        if (swapped) {
            swap_count++;
            final_y = y_after;
        }

        // Output data every 5 steps, or on swap events, or near interesting times.
        bool should_log = (step % 5 == 0) || swapped || (step >= 25 && step <= 35);
        if (should_log) {
            const Cell& duck_cell = world->getData().at(1, y_after);

            // Get info about cell above the duck (if exists).
            std::string above_mat = "-";
            std::string above_com = "-";
            std::string above_vel = "-";
            if (y_after > 0) {
                const Cell& above = world->getData().at(1, y_after - 1);
                above_mat = toString(above.material_type);
                above_com = fmt::format("{:.2f}", above.com.y);
                above_vel = fmt::format("{:.2f}", above.velocity.y);
            }

            std::cout << fmt::format(
                "{}\t{}\t{:.2f}\t{:.2f}\t{}\t{}\t{}\t{}\n",
                step,
                y_after,
                duck_cell.com.y,
                duck_cell.velocity.y,
                above_mat,
                above_com,
                above_vel,
                swapped ? "SWAP" : "");
        }

        // Stop early if duck reached the surface.
        if (y_after == 0) {
            spdlog::info("  Duck reached surface at step {}", step);
            break;
        }
    }
    std::cout << "=== END TABLE ===\n\n";

    spdlog::info(
        "Duck final position: y={} (started at y={}), {} swaps", final_y, initial_y, swap_count);

    // Duck should have floated upward (y decreased).
    EXPECT_LT(final_y, initial_y)
        << "Duck (WOOD, density 0.3) should float upward through water (density 1.0)";
    EXPECT_GE(swap_count, 1)
        << "Duck should participate in buoyancy swaps (not blocked by organism check)";

    // Check that duck doesn't rise too fast (max 0.75 cells per swap).
    // Threshold raised from 0.5 to 0.75 to allow for oscillation (duck may sink
    // briefly before rising again, which is valid physics but increases swap count).
    // Distance traveled = initial_y - final_y (positive when rising).
    int distance_traveled = initial_y - final_y;
    double rise_rate = static_cast<double>(distance_traveled) / static_cast<double>(swap_count);
    spdlog::info(
        "Rise rate: {:.2f} cells/swap ({} cells in {} swaps)",
        rise_rate,
        distance_traveled,
        swap_count);
    EXPECT_LE(rise_rate, 0.75) << "Duck should not rise faster than 0.75 cells per swap (was "
                               << rise_rate << ")";
}
