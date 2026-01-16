#include "core/organisms/evolution/FitnessResult.h"
#include <gtest/gtest.h>

namespace DirtSim {

TEST(FitnessResultTest, DefaultFitnessIgnoresEnergy)
{
    FitnessResult base{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 0.0 };
    FitnessResult boosted{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 100.0 };

    const double baseFitness = base.computeFitness(20.0, 10, 10, 100.0, false);
    const double boostedFitness = boosted.computeFitness(20.0, 10, 10, 100.0, false);

    EXPECT_DOUBLE_EQ(baseFitness, boostedFitness);
}

TEST(FitnessResultTest, TreeFitnessIncludesEnergy)
{
    FitnessResult lowEnergy{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 0.0 };
    FitnessResult highEnergy{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 100.0 };

    const double lowFitness = lowEnergy.computeFitness(20.0, 10, 10, 100.0, true);
    const double highFitness = highEnergy.computeFitness(20.0, 10, 10, 100.0, true);

    EXPECT_GT(highFitness, lowFitness);
}

TEST(FitnessResultTest, DistanceIncreasesFitness)
{
    FitnessResult base{ .lifespan = 10.0, .distanceTraveled = 0.0, .maxEnergy = 0.0 };
    FitnessResult moved{ .lifespan = 10.0, .distanceTraveled = 10.0, .maxEnergy = 0.0 };

    const double baseFitness = base.computeFitness(20.0, 10, 10, 100.0, false);
    const double movedFitness = moved.computeFitness(20.0, 10, 10, 100.0, false);

    EXPECT_GT(movedFitness, baseFitness);
}

} // namespace DirtSim
