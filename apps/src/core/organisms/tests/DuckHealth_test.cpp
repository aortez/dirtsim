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

    // Create with duck on the ground (settled).
    static TallWorldSetup create(int width, int height)
    {
        return createAtY(width, height, height - 2, true);
    }

    // Create with duck at a specific Y position. Optionally settle.
    static TallWorldSetup createAtY(int width, int height, int duckY, bool settle)
    {
        TallWorldSetup setup;
        setup.world = createFlatWorld(width, height);

        auto brain_ptr = std::make_unique<TestDuckBrain>();
        setup.brain = brain_ptr.get();

        OrganismManager& manager = setup.world->getOrganismManager();
        setup.duck_id = manager.createDuck(*setup.world, width / 2, duckY, std::move(brain_ptr));
        setup.duck = manager.getDuck(setup.duck_id);

        if (settle) {
            setup.brain->setAction(DuckAction::WAIT);
            for (int i = 0; i < 40; ++i) {
                setup.world->advanceTime(0.016);
            }
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
    double maxKE = 0.0;
    double maxVPre = 0.0;
    double maxVPost = 0.0;
    double maxDeltaV = 0.0;
    for (int i = 0; i < 60; ++i) {
        Vector2d vPre = setup.duck->getPreCollisionVelocity();
        setup.world->advanceTime(0.016);
        Vector2i pos = setup.duck->getAnchorCell();
        const auto& cell = setup.world->getData().at(pos.x, pos.y);
        Vector2d vPost = cell.velocity;
        const double mass = static_cast<double>(cell.getMass());
        const double vPreMag = std::sqrt(vPre.x * vPre.x + vPre.y * vPre.y);
        const double vPostMag = std::sqrt(vPost.x * vPost.x + vPost.y * vPost.y);
        const double ke = std::max(0.0, 0.5 * mass * (vPreMag * vPreMag - vPostMag * vPostMag));
        const double dv = std::sqrt(
            (vPre.x - vPost.x) * (vPre.x - vPost.x) + (vPre.y - vPost.y) * (vPre.y - vPost.y));
        if (ke > maxKE) {
            maxKE = ke;
            maxVPre = vPreMag;
            maxVPost = vPostMag;
            maxDeltaV = dv;
        }
    }

    const double newDamage = setup.duck->getCollisionDamageTotal() - damageAfterSettle;
    spdlog::warn(
        "CALIBRATION standing: maxKE={:.4f}, maxVPre={:.4f}, maxVPost={:.4f}, "
        "maxDeltaV={:.4f}, newDamage={:.6f}",
        maxKE,
        maxVPre,
        maxVPost,
        maxDeltaV,
        newDamage);

    // Standing should produce no collision damage above threshold.
    EXPECT_DOUBLE_EQ(newDamage, 0.0);
}

TEST_F(DuckHealthTest, CalibrationFreefallLanding)
{
    auto setup = TallWorldSetup::create(20, 40);
    ASSERT_TRUE(setup.duck->isOnGround());

    const int startY = setup.duck->getAnchorCell().y;
    const double damageBeforeJump = setup.duck->getCollisionDamageTotal();

    // Hold jump across frames for the full boost window.
    for (int i = 0; i < 10; ++i) {
        setup.brain->setDirectInput({ 0.0f, 0.0f }, true);
        setup.world->advanceTime(0.016);
    }

    // Release jump, no wing input — pure freefall descent.
    setup.brain->setMove({ 0.0f, 0.0f });

    int minY = setup.duck->getAnchorCell().y;
    double maxTestKE = 0.0;
    double impactVPre = 0.0;
    double impactVPost = 0.0;
    double impactDeltaV = 0.0;
    int landFrame = -1;

    for (int i = 0; i < 200; ++i) {
        // What the test measures: preCollisionVelocity before advanceTime vs cell velocity after.
        Vector2d testVPre = setup.duck->getPreCollisionVelocity();

        // What Duck::update() sees: preCollisionVelocity_ vs cell.velocity at start of frame.
        // This is BEFORE advanceTime runs — same data Duck::update() reads at step 1.
        Vector2i posBefore = setup.duck->getAnchorCell();
        const auto& cellBefore = setup.world->getData().at(posBefore.x, posBefore.y);
        Vector2d duckVPost = cellBefore.velocity;
        const double massBefore = static_cast<double>(cellBefore.getMass());
        const double duckVPreSq = testVPre.x * testVPre.x + testVPre.y * testVPre.y;
        const double duckVPostSq = duckVPost.x * duckVPost.x + duckVPost.y * duckVPost.y;
        const double duckKE = std::max(0.0, 0.5 * massBefore * (duckVPreSq - duckVPostSq));

        const double prevDamage = setup.duck->getCollisionDamageTotal();
        setup.world->advanceTime(0.016);
        const double newDamage = setup.duck->getCollisionDamageTotal() - prevDamage;

        int curY = setup.duck->getAnchorCell().y;
        if (curY < minY) {
            minY = curY;
        }

        // Test's cross-boundary KE measurement.
        Vector2i pos = setup.duck->getAnchorCell();
        const auto& cell = setup.world->getData().at(pos.x, pos.y);
        Vector2d testVPost = cell.velocity;
        const double mass = static_cast<double>(cell.getMass());
        const double testVPreMag = std::sqrt(testVPre.x * testVPre.x + testVPre.y * testVPre.y);
        const double testVPostMag =
            std::sqrt(testVPost.x * testVPost.x + testVPost.y * testVPost.y);
        const double testKE =
            std::max(0.0, 0.5 * mass * (testVPreMag * testVPreMag - testVPostMag * testVPostMag));

        // Log frames near landing (high velocity or damage).
        const double cellVelY = cellBefore.velocity.y;
        if (cellVelY > 3.0 || duckKE > 1.0 || testKE > 1.0 || newDamage > 0.0) {
            spdlog::warn(
                "  frame {:3d}: y={}, cellVel=({:.2f},{:.2f}), preSnap=({:.2f},{:.2f}), "
                "duckKE={:.2f}, testKE={:.2f}, dmgDelta={:.6f}, onGround={}",
                i,
                curY,
                duckVPost.x,
                duckVPost.y,
                testVPre.x,
                testVPre.y,
                duckKE,
                testKE,
                newDamage,
                setup.duck->isOnGround());
        }

        if (testKE > maxTestKE) {
            maxTestKE = testKE;
            impactVPre = testVPreMag;
            impactVPost = testVPostMag;
            impactDeltaV = std::sqrt(
                (testVPre.x - testVPost.x) * (testVPre.x - testVPost.x)
                + (testVPre.y - testVPost.y) * (testVPre.y - testVPost.y));
        }

        if (setup.duck->isOnGround() && i > 20 && landFrame < 0) {
            landFrame = i;
        }

        // Run a few frames past landing so Duck::update() processes the collision.
        if (landFrame >= 0 && i >= landFrame + 5) {
            break;
        }
    }

    const int jumpHeight = startY - minY;
    const double jumpDamage = setup.duck->getCollisionDamageTotal() - damageBeforeJump;
    spdlog::warn(
        "CALIBRATION freefall: jumpHeight={} cells, apex_y={}, startY={}, landFrame={}, "
        "maxTestKE={:.4f}, impactVPre={:.4f}, impactVPost={:.4f}, impactDeltaV={:.4f}, "
        "damage={:.6f}, health={:.4f}",
        jumpHeight,
        minY,
        startY,
        landFrame,
        maxTestKE,
        impactVPre,
        impactVPost,
        impactDeltaV,
        jumpDamage,
        setup.duck->getHealth());

    EXPECT_GE(jumpDamage, 0.0);
}

TEST_F(DuckHealthTest, CalibrationCushionedLanding)
{
    auto setup = TallWorldSetup::create(20, 40);
    ASSERT_TRUE(setup.duck->isOnGround());

    const int startY = setup.duck->getAnchorCell().y;
    const double damageBeforeJump = setup.duck->getCollisionDamageTotal();

    // Hold jump across frames for the full boost window (same as freefall).
    for (int i = 0; i < 10; ++i) {
        setup.brain->setDirectInput({ 0.0f, 0.0f }, true);
        setup.world->advanceTime(0.016);
    }

    // Release jump, no wing yet — wait for apex.
    setup.brain->setMove({ 0.0f, 0.0f });

    int minY = setup.duck->getAnchorCell().y;
    bool pastApex = false;
    double maxKE = 0.0;
    double impactVPre = 0.0;
    double impactVPost = 0.0;
    double impactDeltaV = 0.0;
    int landFrame = -1;

    for (int i = 0; i < 200; ++i) {
        Vector2i pos = setup.duck->getAnchorCell();
        const auto& cellBefore = setup.world->getData().at(pos.x, pos.y);

        // Detect apex: velocity.y flips from negative (rising) to positive (falling).
        if (!pastApex && cellBefore.velocity.y > 0.0) {
            pastApex = true;
            setup.brain->setMove({ 0.0f, 1.0f });
        }

        Vector2d vPre = setup.duck->getPreCollisionVelocity();
        setup.world->advanceTime(0.016);

        int curY = setup.duck->getAnchorCell().y;
        if (curY < minY) {
            minY = curY;
        }

        pos = setup.duck->getAnchorCell();
        const auto& cell = setup.world->getData().at(pos.x, pos.y);
        Vector2d vPost = cell.velocity;
        const double mass = static_cast<double>(cell.getMass());
        const double vPreMag = std::sqrt(vPre.x * vPre.x + vPre.y * vPre.y);
        const double vPostMag = std::sqrt(vPost.x * vPost.x + vPost.y * vPost.y);
        const double ke = std::max(0.0, 0.5 * mass * (vPreMag * vPreMag - vPostMag * vPostMag));
        if (ke > maxKE) {
            maxKE = ke;
            impactVPre = vPreMag;
            impactVPost = vPostMag;
            impactDeltaV = std::sqrt(
                (vPre.x - vPost.x) * (vPre.x - vPost.x) + (vPre.y - vPost.y) * (vPre.y - vPost.y));
        }

        if (setup.duck->isOnGround() && i > 20 && landFrame < 0) {
            landFrame = i;
        }

        // Run a few frames past landing so Duck::update() processes the collision.
        if (landFrame >= 0 && i >= landFrame + 5) {
            break;
        }
    }

    const int jumpHeight = startY - minY;
    const double jumpDamage = setup.duck->getCollisionDamageTotal() - damageBeforeJump;
    spdlog::warn(
        "CALIBRATION cushioned: jumpHeight={} cells, apex_y={}, startY={}, landFrame={}, "
        "maxKE={:.4f}, impactVPre={:.4f}, impactVPost={:.4f}, impactDeltaV={:.4f}, "
        "damage={:.6f}, health={:.4f}",
        jumpHeight,
        minY,
        startY,
        landFrame,
        maxKE,
        impactVPre,
        impactVPost,
        impactDeltaV,
        jumpDamage,
        setup.duck->getHealth());

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

    // Run right into the wall, track per-frame damage.
    brain->setDirectInput({ 1.0f, 0.0f }, false);
    double maxKE = 0.0;
    double maxVPre = 0.0;
    double maxVPost = 0.0;
    double maxDeltaV = 0.0;
    int maxKEFrame = -1;
    double maxSpeed = 0.0;
    int damageHitCount = 0;
    for (int i = 0; i < 120; ++i) {
        Vector2d vPre = duck->getPreCollisionVelocity();
        const double prevDamage = duck->getCollisionDamageTotal();
        world->advanceTime(0.016);
        const double frameDamage = duck->getCollisionDamageTotal() - prevDamage;

        Vector2i pos = duck->getAnchorCell();
        const auto& cell = world->getData().at(pos.x, pos.y);
        Vector2d vPost = cell.velocity;
        const double mass = static_cast<double>(cell.getMass());
        const double vPreMag = std::sqrt(vPre.x * vPre.x + vPre.y * vPre.y);
        const double vPostMag = std::sqrt(vPost.x * vPost.x + vPost.y * vPost.y);
        const double speed = std::sqrt(vPost.x * vPost.x + vPost.y * vPost.y);
        if (speed > maxSpeed) {
            maxSpeed = speed;
        }
        const double ke = std::max(0.0, 0.5 * mass * (vPreMag * vPreMag - vPostMag * vPostMag));
        if (ke > maxKE) {
            maxKE = ke;
            maxVPre = vPreMag;
            maxVPost = vPostMag;
            maxDeltaV = std::sqrt(
                (vPre.x - vPost.x) * (vPre.x - vPost.x) + (vPre.y - vPost.y) * (vPre.y - vPost.y));
            maxKEFrame = i;
        }
        if (frameDamage > 0.0) {
            ++damageHitCount;
            spdlog::warn(
                "  wall hit frame {:3d}: dmg={:.6f}, totalDmg={:.6f}, health={:.4f}, "
                "vPre=({:.2f},{:.2f}), vPost=({:.2f},{:.2f}), KE={:.2f}",
                i,
                frameDamage,
                duck->getCollisionDamageTotal(),
                duck->getHealth(),
                vPre.x,
                vPre.y,
                vPost.x,
                vPost.y,
                ke);
        }
    }

    const double runDamage = duck->getCollisionDamageTotal() - damageBeforeRun;
    spdlog::warn(
        "CALIBRATION wall collision: maxSpeed={:.4f}, maxKE={:.4f} (frame {}), "
        "impactVPre={:.4f}, impactVPost={:.4f}, impactDeltaV={:.4f}, "
        "totalDamage={:.6f}, health={:.4f}, damageHits={}",
        maxSpeed,
        maxKE,
        maxKEFrame,
        maxVPre,
        maxVPost,
        maxDeltaV,
        runDamage,
        duck->getHealth(),
        damageHitCount);

    // Wall collision should produce meaningful damage.
    EXPECT_GT(runDamage, 0.0);
}

struct DropTestParams {
    int dropCells;
    bool flapping;

    // For readable test names.
    friend std::ostream& operator<<(std::ostream& os, const DropTestParams& p)
    {
        os << p.dropCells << "cells_" << (p.flapping ? "flapping" : "freefall");
        return os;
    }
};

class DuckDropTest : public ::testing::TestWithParam<DropTestParams> {
protected:
    void SetUp() override { spdlog::set_level(spdlog::level::warn); }
};

TEST_P(DuckDropTest, CalibrationDrop)
{
    const auto& params = GetParam();

    // World needs enough height for the drop plus floor. Floor is at height-1.
    // Duck lands at height-2. Drop distance = (height-2) - duckY.
    const int worldHeight = params.dropCells + 4;
    const int duckY = 2;
    auto setup = TallWorldSetup::createAtY(20, worldHeight, duckY, false);
    ASSERT_FALSE(setup.duck->isOnGround());

    const int startY = setup.duck->getAnchorCell().y;
    const double damageBeforeDrop = setup.duck->getCollisionDamageTotal();

    if (params.flapping) {
        setup.brain->setMove({ 0.0f, 1.0f });
    }
    else {
        setup.brain->setAction(DuckAction::WAIT);
    }

    double maxTestKE = 0.0;
    double impactVPre = 0.0;
    double impactVPost = 0.0;
    double impactDeltaV = 0.0;
    int landFrame = -1;

    for (int i = 0; i < 2000; ++i) {
        Vector2d testVPre = setup.duck->getPreCollisionVelocity();

        setup.world->advanceTime(0.016);

        // Test's cross-boundary KE measurement.
        Vector2i pos = setup.duck->getAnchorCell();
        const auto& cell = setup.world->getData().at(pos.x, pos.y);
        Vector2d testVPost = cell.velocity;
        const double mass = static_cast<double>(cell.getMass());
        const double testVPreMag = std::sqrt(testVPre.x * testVPre.x + testVPre.y * testVPre.y);
        const double testVPostMag =
            std::sqrt(testVPost.x * testVPost.x + testVPost.y * testVPost.y);
        const double testKE =
            std::max(0.0, 0.5 * mass * (testVPreMag * testVPreMag - testVPostMag * testVPostMag));

        if (testKE > maxTestKE) {
            maxTestKE = testKE;
            impactVPre = testVPreMag;
            impactVPost = testVPostMag;
            impactDeltaV = std::sqrt(
                (testVPre.x - testVPost.x) * (testVPre.x - testVPost.x)
                + (testVPre.y - testVPost.y) * (testVPre.y - testVPost.y));
        }

        if (setup.duck->isOnGround() && i > 5 && landFrame < 0) {
            landFrame = i;
        }

        // Run a few frames past landing so Duck::update() processes the collision.
        if (landFrame >= 0 && i >= landFrame + 5) {
            break;
        }

        // Bail if duck died mid-fall.
        if (setup.duck->isDead()) {
            break;
        }
    }

    const int fallDistance = setup.duck->getAnchorCell().y - startY;
    const double dropDamage = setup.duck->getCollisionDamageTotal() - damageBeforeDrop;
    spdlog::warn(
        "CALIBRATION drop: height={} cells, {}, fallDistance={}, landFrame={}, "
        "maxTestKE={:.4f}, impactVPre={:.4f}, impactVPost={:.4f}, impactDeltaV={:.4f}, "
        "damage={:.6f}, health={:.4f}, dead={}",
        params.dropCells,
        params.flapping ? "FLAPPING" : "freefall",
        fallDistance,
        landFrame,
        maxTestKE,
        impactVPre,
        impactVPost,
        impactDeltaV,
        dropDamage,
        setup.duck->getHealth(),
        setup.duck->isDead());

    EXPECT_GE(dropDamage, 0.0);
}

// clang-format off
INSTANTIATE_TEST_SUITE_P(
    CalibrationDrops,
    DuckDropTest,
    ::testing::Values(
        DropTestParams{  4, false },
        DropTestParams{  8, false },
        DropTestParams{ 12, false },
        DropTestParams{ 16, false },
        DropTestParams{ 24, false },
        DropTestParams{ 32, false },
        DropTestParams{ 48, false },
        DropTestParams{ 64, false },
        DropTestParams{  4, true  },
        DropTestParams{  8, true  },
        DropTestParams{ 12, true  },
        DropTestParams{ 16, true  },
        DropTestParams{ 24, true  },
        DropTestParams{ 32, true  },
        DropTestParams{ 48, true  },
        DropTestParams{ 64, true  }
    ),
    [](const ::testing::TestParamInfo<DropTestParams>& info) {
        return std::to_string(info.param.dropCells) + "cells_"
            + (info.param.flapping ? "flapping" : "freefall");
    }
);
// clang-format on

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
