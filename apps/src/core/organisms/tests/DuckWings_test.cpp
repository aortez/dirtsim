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
