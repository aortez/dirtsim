#include "core/organisms/tests/DuckTestUtils.h"

#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Test;

namespace {

class ConstantInputDuckBrain : public DuckBrain {
public:
    void setInput(DuckInput input) { input_ = input; }

    void think(Duck& duck, const DuckSensoryData& /*sensory*/, double /*deltaTime*/) override
    {
        duck.setInput(input_);
    }

private:
    DuckInput input_{};
};

struct JumpMetrics {
    double energyConsumedTotal = 0.0;
    double minPositionY = 0.0;
};

JumpMetrics simulateHeldJump(float moveY)
{
    constexpr int kWidth = 12;
    constexpr int kHeight = 80;
    constexpr int kSpawnX = 6;
    constexpr int kSpawnY = kHeight - 2;
    constexpr double kDt = 0.016;

    auto world = createFlatWorld(kWidth, kHeight);
    OrganismManager& manager = world->getOrganismManager();

    auto brain = std::make_unique<ConstantInputDuckBrain>();
    auto* brainPtr = brain.get();
    const OrganismId duckId = manager.createDuck(*world, kSpawnX, kSpawnY, std::move(brain));
    Duck* duck = manager.getDuck(duckId);
    EXPECT_NE(duck, nullptr);

    // Settle on ground.
    brainPtr->setInput({ .move = {}, .jump = false, .run = false });
    for (int i = 0; i < 120; ++i) {
        world->advanceTime(kDt);
        if (duck->isOnGround()) {
            break;
        }
    }
    EXPECT_TRUE(duck->isOnGround());

    // Duck has a one-frame landing cooldown. Ensure we're past it before starting the jump.
    for (int i = 0; i < 2; ++i) {
        world->advanceTime(kDt);
    }

    // Hold jump for the entire sim, with different move.y values to test wings.
    brainPtr->setInput({ .move = { 0.0f, moveY }, .jump = true, .run = false });

    JumpMetrics metrics;
    metrics.minPositionY = duck->position.y;
    for (int i = 0; i < 180; ++i) {
        world->advanceTime(kDt);
        metrics.minPositionY = std::min(metrics.minPositionY, duck->position.y);
    }

    metrics.energyConsumedTotal = duck->getEnergyConsumedTotal();
    return metrics;
}

} // namespace

TEST(DuckWingsTest, LiftCancelsGravityWhileAirborne)
{
    auto liftSetup = DuckTestSetup::create(10, 30, 5, 5, 0);
    auto baselineSetup = DuckTestSetup::create(10, 30, 5, 5, 0);

    liftSetup.brain->setDirectInput(Vector2f{ 0.0f, 1.0f }, false);
    baselineSetup.brain->setDirectInput(Vector2f{ 0.0f, 0.0f }, false);

    liftSetup.advanceFrames(30);
    baselineSetup.advanceFrames(30);

    const double liftVy = liftSetup.getVelocity().y;
    const double baselineVy = baselineSetup.getVelocity().y;

    EXPECT_FALSE(liftSetup.duck->isOnGround());
    EXPECT_FALSE(baselineSetup.duck->isOnGround());

    // With lift held, the duck should accumulate substantially less downward velocity.
    EXPECT_LT(liftVy, baselineVy - 1.0);
}

TEST(DuckWingsTest, LiftHoversWhenStartingStationaryAirborne)
{
    auto liftSetup = DuckTestSetup::create(10, 60, 5, 5, 0);
    auto baselineSetup = DuckTestSetup::create(10, 60, 5, 5, 0);

    liftSetup.brain->setDirectInput(Vector2f{ 0.0f, 1.0f }, false);
    baselineSetup.brain->setDirectInput(Vector2f{ 0.0f, 0.0f }, false);

    const double liftStartY = liftSetup.duck->position.y;
    const double baselineStartY = baselineSetup.duck->position.y;

    liftSetup.advanceFrames(60);
    baselineSetup.advanceFrames(60);

    const double liftDeltaY = liftSetup.duck->position.y - liftStartY;
    const double baselineDeltaY = baselineSetup.duck->position.y - baselineStartY;

    // Gravity pulls +Y. With full lift held and near-zero starting velocity,
    // the duck should hover (minimal change in position.y).
    EXPECT_LT(std::abs(liftDeltaY), 0.10);

    // Baseline should fall noticeably in the same time window.
    EXPECT_GT(baselineDeltaY, 0.50);

    // Hovering with wings should consume energy; baseline should not.
    EXPECT_GT(liftSetup.duck->getEnergyConsumedTotal(), 0.15);
    EXPECT_NEAR(baselineSetup.duck->getEnergyConsumedTotal(), 0.0, 1e-9);
}

TEST(DuckWingsTest, FullLiftIsEnergyLimitedAfterAboutTenSeconds)
{
    // With default config:
    // startingEnergy=1.0, regen=0.15/s, wingLiftCost=0.25/s (at |move.y|=1),
    // net drain is 0.10/s, so we should lose the ability to hold full lift after ~10s.
    auto setup = DuckTestSetup::create(10, 200, 5, 5, 0);
    setup.brain->setDirectInput(Vector2f{ 0.0f, 1.0f }, false);

    constexpr double kDt = 0.016;
    double limitedAtSeconds = -1.0;

    for (int i = 0; i < 1000; ++i) {
        setup.advance(kDt);
        if (setup.duck->getEnergyLimitedSeconds() > 0.0) {
            limitedAtSeconds = static_cast<double>(i + 1) * kDt;
            break;
        }
    }

    ASSERT_GT(limitedAtSeconds, 0.0);
    EXPECT_GT(limitedAtSeconds, 9.0);
    EXPECT_LT(limitedAtSeconds, 11.0);

    // By the time we're energy limited, energy should be near empty.
    EXPECT_LT(setup.duck->getEnergy(), 0.02);
}

TEST(DuckWingsTest, HeldJumpWithLiftIsHigherAndMoreExpensiveThanNeutralAndDive)
{
    const JumpMetrics neutral = simulateHeldJump(0.0f);
    const JumpMetrics lift = simulateHeldJump(1.0f);
    const JumpMetrics dive = simulateHeldJump(-1.0f);

    // Smaller position.y means higher in the world (gravity pulls +Y).
    EXPECT_LT(lift.minPositionY, neutral.minPositionY - 0.5);
    EXPECT_GT(dive.minPositionY, neutral.minPositionY + 0.25);

    // Wings should consume extra energy (lift a lot, dive a little).
    EXPECT_GT(lift.energyConsumedTotal, neutral.energyConsumedTotal + 0.1);
    EXPECT_GT(dive.energyConsumedTotal, neutral.energyConsumedTotal);
    EXPECT_GT(lift.energyConsumedTotal, dive.energyConsumedTotal + 0.05);
}
