#include "core/organisms/TreeResourceTotals.h"
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
    config.waterReference = 100.0;
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
    const TreeResourceTotals resources{};

    const FitnessContext with_context{
        .result = with_distance,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };
    const FitnessContext without_context{
        .result = no_distance,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };

    const double fitness_with = computeFitnessForOrganism(with_context);
    const double fitness_without = computeFitnessForOrganism(without_context);

    EXPECT_DOUBLE_EQ(fitness_with, fitness_without);
}

TEST(FitnessCalculatorTest, TreeFitnessIncludesEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .distanceTraveled = 50.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0,
                                     .distanceTraveled = 0.0,
                                     .maxEnergy = 100.0 };
    const TreeResourceTotals resources{};

    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, TreeFitnessRewardsResourceCollection)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .distanceTraveled = 0.0, .maxEnergy = 0.0 };
    const TreeResourceTotals low_resources{ .waterAbsorbed = 0.0, .energyProduced = 0.0 };
    const TreeResourceTotals high_resources{ .waterAbsorbed = 100.0, .energyProduced = 100.0 };

    const FitnessContext low_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &low_resources,
    };
    const FitnessContext high_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &high_resources,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, DuckFitnessIgnoresEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0,
                                     .distanceTraveled = 5.0,
                                     .maxEnergy = 100.0 };
    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_DOUBLE_EQ(fitness_low, fitness_high);
}

TEST(FitnessCalculatorTest, GooseFitnessRewardsDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult static_move{ .lifespan = 10.0, .distanceTraveled = 0.0, .maxEnergy = 0.0 };
    const FitnessResult moved{ .lifespan = 10.0, .distanceTraveled = 10.0, .maxEnergy = 0.0 };
    const FitnessContext static_context{
        .result = static_move,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const FitnessContext moved_context{
        .result = moved,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };

    const double fitness_static = computeFitnessForOrganism(static_context);
    const double fitness_moved = computeFitnessForOrganism(moved_context);

    EXPECT_GT(fitness_moved, fitness_static);
}

} // namespace DirtSim
