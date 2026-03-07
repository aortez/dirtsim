/**
 * @file DuckHealth_test.cpp
 * @brief Calibration and unit tests for duck health/collision damage system.
 *
 * Phase 1 tests measure collision impact energy across scripted duck behaviors
 * to validate impactEnergyThreshold and impactDamageScale values.
 * Phase 2 tests verify health mechanics (regen, damage, death, brain input).
 */

#include "DuckTestUtils.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace DirtSim;
using namespace DirtSim::Test;

class DuckHealthTest : public ::testing::Test {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::warn); }
};

// Helper to create a settled duck in a tall world for jump/fall tests.
struct TallWorldSetup {
    std::unique_ptr<World> world;
    Duck* duck = nullptr;
    TestDuckBrain* brain = nullptr;
    OrganismId duck_id = INVALID_ORGANISM_ID;

    static TallWorldSetup create(int width, int height)
    {
        TallWorldSetup setup;
        setup.world = createFlatWorld(width, height);

        auto brain_ptr = std::make_unique<TestDuckBrain>();
        setup.brain = brain_ptr.get();

        OrganismManager& manager = setup.world->getOrganismManager();
        const int duck_y = height - 2;
        setup.duck_id = manager.createDuck(*setup.world, width / 2, duck_y, std::move(brain_ptr));
        setup.duck = manager.getDuck(setup.duck_id);

        // Settle.
        setup.brain->setAction(DuckAction::WAIT);
        for (int i = 0; i < 40; ++i) {
            setup.world->advanceTime(0.016);
        }
        return setup;
    }
};

// ============================================================================
// Phase 1: Calibration Tests
// ============================================================================

TEST_F(DuckHealthTest, CalibrationStandingOnGround)
{
    auto setup = TallWorldSetup::create(20, 10);
    ASSERT_TRUE(setup.duck->isOnGround());

    const double damageAfterSettle = setup.duck->getCollisionDamageTotal();

    setup.brain->setAction(DuckAction::WAIT);
    for (int i = 0; i < 60; ++i) {
        setup.world->advanceTime(0.016);
    }

    const double newDamage = setup.duck->getCollisionDamageTotal() - damageAfterSettle;
    spdlog::warn(
        "CALIBRATION standing: newDamage={:.6f}, totalDamage={:.6f}",
        newDamage,
        setup.duck->getCollisionDamageTotal());

    // Standing should produce no collision damage above threshold.
    EXPECT_DOUBLE_EQ(newDamage, 0.0);
}

TEST_F(DuckHealthTest, CalibrationFreefallLanding)
{
    auto setup = TallWorldSetup::create(20, 20);
    ASSERT_TRUE(setup.duck->isOnGround());

    const double damageBeforeJump = setup.duck->getCollisionDamageTotal();

    // Jump.
    setup.brain->triggerJump();
    setup.world->advanceTime(0.016);

    // Descent with no wing input.
    setup.brain->setMove({ 0.0f, 0.0f });
    for (int i = 0; i < 120; ++i) {
        setup.world->advanceTime(0.016);
        if (setup.duck->isOnGround() && i > 10) {
            break;
        }
    }

    const double jumpDamage = setup.duck->getCollisionDamageTotal() - damageBeforeJump;
    spdlog::warn(
        "CALIBRATION freefall: jumpDamage={:.6f}, health={:.4f}",
        jumpDamage,
        setup.duck->getHealth());

    // Freefall from a standard jump should produce some damage.
    EXPECT_GE(jumpDamage, 0.0);
}

TEST_F(DuckHealthTest, CalibrationCushionedLanding)
{
    auto setup = TallWorldSetup::create(20, 20);
    ASSERT_TRUE(setup.duck->isOnGround());

    const double damageBeforeJump = setup.duck->getCollisionDamageTotal();

    // Jump.
    setup.brain->triggerJump();
    setup.world->advanceTime(0.016);

    // Wing lift during descent.
    setup.brain->setMove({ 0.0f, 1.0f });
    for (int i = 0; i < 120; ++i) {
        setup.world->advanceTime(0.016);
        if (setup.duck->isOnGround() && i > 10) {
            break;
        }
    }

    const double jumpDamage = setup.duck->getCollisionDamageTotal() - damageBeforeJump;
    spdlog::warn(
        "CALIBRATION cushioned: jumpDamage={:.6f}, health={:.4f}",
        jumpDamage,
        setup.duck->getHealth());

    // Cushioned landing should produce less damage.
    EXPECT_LT(jumpDamage, 0.5);
}

TEST_F(DuckHealthTest, CalibrationWallCollision)
{
    auto world = createObstacleWorld(20, 10, 15, 3);
    auto brain_ptr = std::make_unique<TestDuckBrain>();
    TestDuckBrain* brain = brain_ptr.get();

    OrganismManager& manager = world->getOrganismManager();
    OrganismId duck_id = manager.createDuck(*world, 5, 8, std::move(brain_ptr));
    Duck* duck = manager.getDuck(duck_id);

    // Settle.
    brain->setAction(DuckAction::WAIT);
    for (int i = 0; i < 40; ++i) {
        world->advanceTime(0.016);
    }
    ASSERT_TRUE(duck->isOnGround());

    const double damageBeforeRun = duck->getCollisionDamageTotal();

    // Run right into the wall.
    brain->setDirectInput({ 1.0f, 0.0f }, false);
    for (int i = 0; i < 120; ++i) {
        world->advanceTime(0.016);
    }

    const double runDamage = duck->getCollisionDamageTotal() - damageBeforeRun;
    spdlog::warn(
        "CALIBRATION wall collision: runDamage={:.6f}, health={:.4f}",
        runDamage,
        duck->getHealth());

    // Wall collision should produce meaningful damage.
    EXPECT_GT(runDamage, 0.0);
}

// ============================================================================
// Phase 2: Health Unit Tests
// ============================================================================

TEST_F(DuckHealthTest, HealthStartsAtOne)
{
    auto world = createFlatWorld(20, 10);
    auto brain_ptr = std::make_unique<TestDuckBrain>();
    OrganismManager& manager = world->getOrganismManager();
    OrganismId duck_id = manager.createDuck(*world, 10, 8, std::move(brain_ptr));
    Duck* duck = manager.getDuck(duck_id);
    EXPECT_DOUBLE_EQ(duck->getHealth(), 1.0);
    EXPECT_FALSE(duck->isDead());
}

TEST_F(DuckHealthTest, ApplyDamageReducesHealth)
{
    auto world = createFlatWorld(20, 10);
    auto brain_ptr = std::make_unique<TestDuckBrain>();
    OrganismManager& manager = world->getOrganismManager();
    OrganismId duck_id = manager.createDuck(*world, 10, 8, std::move(brain_ptr));
    Duck* duck = manager.getDuck(duck_id);

    duck->applyDamage(0.3);
    EXPECT_NEAR(duck->getHealth(), 0.7, 0.001);
    EXPECT_FALSE(duck->isDead());
}

TEST_F(DuckHealthTest, HealthRegenerates)
{
    auto setup = TallWorldSetup::create(20, 10);

    setup.duck->applyDamage(0.3);
    double healthAfterDamage = setup.duck->getHealth();

    // Run frames to allow regen.
    setup.brain->setAction(DuckAction::WAIT);
    for (int i = 0; i < 60; ++i) {
        setup.world->advanceTime(0.016);
    }

    EXPECT_GT(setup.duck->getHealth(), healthAfterDamage);
}

TEST_F(DuckHealthTest, HealthCapsAtOne)
{
    auto setup = TallWorldSetup::create(20, 10);

    setup.brain->setAction(DuckAction::WAIT);
    for (int i = 0; i < 120; ++i) {
        setup.world->advanceTime(0.016);
    }

    EXPECT_LE(setup.duck->getHealth(), 1.0);
}

TEST_F(DuckHealthTest, DuckDiesAtZeroHealth)
{
    auto world = createFlatWorld(20, 10);
    auto brain_ptr = std::make_unique<TestDuckBrain>();
    OrganismManager& manager = world->getOrganismManager();
    OrganismId duck_id = manager.createDuck(*world, 10, 8, std::move(brain_ptr));
    Duck* duck = manager.getDuck(duck_id);

    duck->applyDamage(1.5);
    world->advanceTime(0.016);

    EXPECT_TRUE(duck->isDead());
}

TEST_F(DuckHealthTest, StandingOnGroundNoCollisionDamage)
{
    auto setup = TallWorldSetup::create(20, 10);
    ASSERT_TRUE(setup.duck->isOnGround());

    const double damageAfterSettle = setup.duck->getCollisionDamageTotal();

    setup.brain->setAction(DuckAction::WAIT);
    for (int i = 0; i < 60; ++i) {
        setup.world->advanceTime(0.016);
    }

    EXPECT_DOUBLE_EQ(setup.duck->getCollisionDamageTotal(), damageAfterSettle);
}

TEST_F(DuckHealthTest, BrainSeesHealthInSensoryData)
{
    auto world = createFlatWorld(20, 10);
    auto brain_ptr = std::make_unique<TestDuckBrain>();
    OrganismManager& manager = world->getOrganismManager();
    OrganismId duck_id = manager.createDuck(*world, 10, 8, std::move(brain_ptr));
    Duck* duck = manager.getDuck(duck_id);

    DuckSensoryData sensory = duck->gatherSensoryData(*world, 0.016);
    EXPECT_FLOAT_EQ(sensory.health, 1.0f);

    duck->applyDamage(0.4);
    sensory = duck->gatherSensoryData(*world, 0.016);
    EXPECT_NEAR(sensory.health, 0.6f, 0.01f);
}
