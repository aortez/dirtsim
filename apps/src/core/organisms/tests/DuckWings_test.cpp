#include "core/organisms/tests/DuckTestUtils.h"

#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Test;

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
