#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "core/organisms/evolution/FitnessResult.h"
#include <gtest/gtest.h>

namespace DirtSim {

namespace {
EvolutionConfig makeConfig()
{
    EvolutionConfig config;
    config.maxSimulationTime = 20.0;
    config.energyReference = 100.0;
    return config;
}
} // namespace

TEST(FitnessCalculatorTest, TreeFitnessIgnoresDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult with_distance{ .lifespan = 10.0,
                                       .distanceTraveled = 50.0,
                                       .maxEnergy = 0.0 };
    const FitnessResult no_distance{ .lifespan = 10.0, .distanceTraveled = 0.0, .maxEnergy = 0.0 };

    const double fitness_with =
        computeFitnessForOrganism(with_distance, OrganismType::TREE, 10, 10, config);
    const double fitness_without =
        computeFitnessForOrganism(no_distance, OrganismType::TREE, 10, 10, config);

    EXPECT_DOUBLE_EQ(fitness_with, fitness_without);
}

TEST(FitnessCalculatorTest, TreeFitnessIncludesEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .distanceTraveled = 50.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0,
                                     .distanceTraveled = 0.0,
                                     .maxEnergy = 100.0 };

    const double fitness_low =
        computeFitnessForOrganism(low_energy, OrganismType::TREE, 10, 10, config);
    const double fitness_high =
        computeFitnessForOrganism(high_energy, OrganismType::TREE, 10, 10, config);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, DuckFitnessIgnoresEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0,
                                     .distanceTraveled = 5.0,
                                     .maxEnergy = 100.0 };

    const double fitness_low =
        computeFitnessForOrganism(low_energy, OrganismType::DUCK, 10, 10, config);
    const double fitness_high =
        computeFitnessForOrganism(high_energy, OrganismType::DUCK, 10, 10, config);

    EXPECT_DOUBLE_EQ(fitness_low, fitness_high);
}

TEST(FitnessCalculatorTest, GooseFitnessRewardsDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult static_move{ .lifespan = 10.0, .distanceTraveled = 0.0, .maxEnergy = 0.0 };
    const FitnessResult moved{ .lifespan = 10.0, .distanceTraveled = 10.0, .maxEnergy = 0.0 };

    const double fitness_static =
        computeFitnessForOrganism(static_move, OrganismType::GOOSE, 10, 10, config);
    const double fitness_moved =
        computeFitnessForOrganism(moved, OrganismType::GOOSE, 10, 10, config);

    EXPECT_GT(fitness_moved, fitness_static);
}

} // namespace DirtSim
