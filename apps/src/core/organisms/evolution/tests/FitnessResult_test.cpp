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

TEST(FitnessResultTest, DefaultFitnessIgnoresEnergy)
{
    FitnessResult base{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 0.0 };
    FitnessResult boosted{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 100.0 };
    const EvolutionConfig config = makeConfig();

    const FitnessContext baseContext{
        .result = base,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const FitnessContext boostedContext{
        .result = boosted,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const double baseFitness = computeFitnessForOrganism(baseContext);
    const double boostedFitness = computeFitnessForOrganism(boostedContext);

    EXPECT_DOUBLE_EQ(baseFitness, boostedFitness);
}

TEST(FitnessResultTest, TreeFitnessIncludesEnergy)
{
    FitnessResult lowEnergy{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 0.0 };
    FitnessResult highEnergy{ .lifespan = 10.0, .distanceTraveled = 5.0, .maxEnergy = 100.0 };
    const EvolutionConfig config = makeConfig();
    const TreeResourceTotals resources{};

    const FitnessContext lowContext{
        .result = lowEnergy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };
    const FitnessContext highContext{
        .result = highEnergy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };
    const double lowFitness = computeFitnessForOrganism(lowContext);
    const double highFitness = computeFitnessForOrganism(highContext);

    EXPECT_GT(highFitness, lowFitness);
}

TEST(FitnessResultTest, DistanceIncreasesFitness)
{
    FitnessResult base{ .lifespan = 10.0, .distanceTraveled = 0.0, .maxEnergy = 0.0 };
    FitnessResult moved{ .lifespan = 10.0, .distanceTraveled = 10.0, .maxEnergy = 0.0 };
    const EvolutionConfig config = makeConfig();

    const FitnessContext baseContext{
        .result = base,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const FitnessContext movedContext{
        .result = moved,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const double baseFitness = computeFitnessForOrganism(baseContext);
    const double movedFitness = computeFitnessForOrganism(movedContext);

    EXPECT_GT(movedFitness, baseFitness);
}

} // namespace DirtSim
