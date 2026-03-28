#include "core/MaterialType.h"
#include "core/organisms/Duck.h"
#include "core/organisms/Tree.h"
#include "core/organisms/TreeCommandProcessor.h"
#include "core/organisms/TreeResourceTotals.h"
#include "core/organisms/brains/RuleBasedBrain.h"
#include "core/organisms/evolution/DuckEvaluator.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/OrganismTracker.h"
#include "core/organisms/evolution/TreeEvaluator.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>

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

std::unique_ptr<Tree> makeTree()
{
    return std::make_unique<Tree>(
        OrganismId{ 1 },
        std::make_unique<RuleBasedBrain>(),
        std::make_unique<TreeCommandProcessor>());
}

OrganismTrackingHistory makeHistory(std::initializer_list<Vector2d> positions)
{
    OrganismTrackingHistory history;
    double simTime = 0.0;
    for (const Vector2d& position : positions) {
        history.samples.push_back({ .simTime = simTime, .position = position });
        simTime += 0.016;
    }
    return history;
}

DuckEvaluationArtifacts makeClockArtifacts(const DuckClockEvaluationArtifacts& clock)
{
    return DuckEvaluationArtifacts{
        .clock = clock,
    };
}

double obstacleCompetenceExpected(double clears, double opportunities)
{
    if (opportunities <= 0.0) {
        return 0.0;
    }
    const double creditedClears = std::min(clears, opportunities);
    const double successRate = creditedClears / opportunities;
    const double confidence = 1.0 - std::exp(-opportunities / 3.0);
    return successRate * confidence;
}
} // namespace

TEST(FitnessCalculatorTest, TreeFitnessIgnoresDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory history = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 50.0, 0.0 },
        });
    const TreeResourceTotals resources{};

    const FitnessContext with_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .organismTrackingHistory = &history,
        .treeResources = &resources,
    };
    const FitnessContext without_context{
        .result = result,
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
    const FitnessResult low_energy{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0, .maxEnergy = 100.0 };
    auto tree = makeTree();
    tree->setEnergy(100.0);
    tree->addCellToLocalShape({ 0, -1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Root, 1.0);
    tree->addCellToLocalShape({ 1, -1 }, Material::EnumType::Leaf, 1.0);

    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, TreeFitnessRewardsResourceCollection)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const TreeResourceTotals low_resources{
        .waterAbsorbed = 0.0, .energyProduced = 0.0, .seedsProduced = 0, .landedSeeds = {}
    };
    const TreeResourceTotals high_resources{
        .waterAbsorbed = 100.0, .energyProduced = 100.0, .seedsProduced = 0, .landedSeeds = {}
    };
    auto tree = makeTree();
    tree->addCellToLocalShape({ 0, -1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Root, 1.0);
    tree->addCellToLocalShape({ 1, -1 }, Material::EnumType::Leaf, 1.0);

    const FitnessContext low_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &low_resources,
    };
    const FitnessContext high_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &high_resources,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, TreeResourceScoreRequiresMinimalStructure)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const TreeResourceTotals high_resources{
        .waterAbsorbed = 100.0, .energyProduced = 100.0, .seedsProduced = 0, .landedSeeds = {}
    };
    auto tree = makeTree();

    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &high_resources,
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown breakdown = evaluator.evaluateWithBreakdown(context);

    EXPECT_DOUBLE_EQ(breakdown.resourceScore, 0.0);
}

TEST(FitnessCalculatorTest, TreeHeldEnergyScoreRequiresMinimalStructure)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 100.0 };
    auto tree = makeTree();
    tree->setEnergy(100.0);

    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown breakdown = evaluator.evaluateWithBreakdown(context);

    EXPECT_DOUBLE_EQ(breakdown.energyScore, 0.0);
}

TEST(FitnessCalculatorTest, TreeHeldEnergyScoreScalesAfterMinimalStructure)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 100.0 };
    auto tree = makeTree();
    tree->setEnergy(100.0);
    tree->addCellToLocalShape({ 0, -1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Root, 1.0);
    tree->addCellToLocalShape({ 1, -1 }, Material::EnumType::Leaf, 1.0);

    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown breakdown = evaluator.evaluateWithBreakdown(context);

    EXPECT_DOUBLE_EQ(breakdown.energyScore, 1.0);
}

TEST(FitnessCalculatorTest, TreeCommandScoreIsDisabled)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult oneAccepted{
        .lifespan = config.maxSimulationTime,
        .maxEnergy = 0.0,
        .commandsAccepted = 1,
    };
    const FitnessResult manyAccepted{
        .lifespan = config.maxSimulationTime,
        .maxEnergy = 0.0,
        .commandsAccepted = 42,
    };

    const FitnessContext oneContext{
        .result = oneAccepted,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const FitnessContext manyContext{
        .result = manyAccepted,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown oneBreakdown = evaluator.evaluateWithBreakdown(oneContext);
    const TreeFitnessBreakdown manyBreakdown = evaluator.evaluateWithBreakdown(manyContext);

    EXPECT_DOUBLE_EQ(oneBreakdown.commandScore, 0.0);
    EXPECT_DOUBLE_EQ(manyBreakdown.commandScore, 0.0);

    const FitnessResult manyRejects{
        .lifespan = config.maxSimulationTime,
        .maxEnergy = 0.0,
        .commandsRejected = 1234,
        .idleCancels = 999,
    };
    const FitnessContext rejectsContext{
        .result = manyRejects,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const TreeFitnessBreakdown rejectsBreakdown = evaluator.evaluateWithBreakdown(rejectsContext);

    EXPECT_DOUBLE_EQ(rejectsBreakdown.commandScore, 0.0);
}

TEST(FitnessCalculatorTest, DuckFitnessIgnoresEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0, .maxEnergy = 100.0 };
    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .duckArtifacts =
            DuckEvaluationArtifacts{
                .energyAverage = 0.1,
                .clock =
                    DuckClockEvaluationArtifacts{
                        .pitClears = 1,
                        .pitOpportunities = 1,
                        .traversalProgress = 1.0,
                    },
            },
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .duckArtifacts =
            DuckEvaluationArtifacts{
                .energyAverage = 0.9,
                .clock =
                    DuckClockEvaluationArtifacts{
                        .pitClears = 1,
                        .pitOpportunities = 1,
                        .traversalProgress = 1.0,
                    },
            },
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_DOUBLE_EQ(fitness_low, fitness_high);
}

TEST(FitnessCalculatorTest, DuckClockTraversalRateScalesWithProgress)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult result{ .lifespan = 100.0, .maxEnergy = 0.0 };
    const FitnessContext slowContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .fullTraversals = 1,
                .traversalProgress = 1.0,
            }),
    };
    const FitnessContext fastContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .fullTraversals = 2,
                .traversalProgress = 2.5,
            }),
    };

    const DuckFitnessBreakdown slowBreakdown = DuckEvaluator::evaluateWithBreakdown(slowContext);
    const DuckFitnessBreakdown fastBreakdown = DuckEvaluator::evaluateWithBreakdown(fastContext);

    EXPECT_DOUBLE_EQ(slowBreakdown.traversalRatePer100Seconds, 1.0);
    EXPECT_DOUBLE_EQ(slowBreakdown.traversalPoints, 500.0);
    EXPECT_DOUBLE_EQ(fastBreakdown.traversalRatePer100Seconds, 2.5);
    EXPECT_DOUBLE_EQ(fastBreakdown.traversalPoints, 1250.0);
    EXPECT_GT(fastBreakdown.totalFitness, slowBreakdown.totalFitness);
}

TEST(FitnessCalculatorTest, GooseFitnessRewardsDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult static_move{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const FitnessResult moved{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory staticHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 0.0, 0.0 },
        });
    const OrganismTrackingHistory movedHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
        });
    const FitnessContext static_context{
        .result = static_move,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &staticHistory,
    };
    const FitnessContext moved_context{
        .result = moved,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &movedHistory,
    };

    const double fitness_static = computeFitnessForOrganism(static_context);
    const double fitness_moved = computeFitnessForOrganism(moved_context);

    EXPECT_GT(fitness_moved, fitness_static);
}

TEST(FitnessCalculatorTest, GooseFitnessPenalizesBackAndForthPath)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory directHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
        });
    const OrganismTrackingHistory longPathHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
        });

    const FitnessContext directContext{
        .result = result,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &directHistory,
    };
    const FitnessContext longPathContext{
        .result = result,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &longPathHistory,
    };

    const double directFitness = computeFitnessForOrganism(directContext);
    const double longPathFitness = computeFitnessForOrganism(longPathContext);

    EXPECT_GT(directFitness, longPathFitness);
}

TEST(FitnessCalculatorTest, GooseFitnessPrefersHorizontalMovement)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory horizontalHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 6.0, 0.0 },
        });
    const OrganismTrackingHistory verticalHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 0.0, 6.0 },
        });

    const FitnessContext horizontalContext{
        .result = result,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .organismTrackingHistory = &horizontalHistory,
    };
    const FitnessContext verticalContext{
        .result = result,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .organismTrackingHistory = &verticalHistory,
    };

    const double horizontalFitness = computeFitnessForOrganism(horizontalContext);
    const double verticalFitness = computeFitnessForOrganism(verticalContext);

    EXPECT_GT(horizontalFitness, verticalFitness);
}

TEST(FitnessCalculatorTest, DuckClockSurvivalScalesClockPoints)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult fullResult{ .lifespan = 100.0, .maxEnergy = 0.0 };
    const FitnessResult halfResult{ .lifespan = 50.0, .maxEnergy = 0.0 };
    const DuckClockEvaluationArtifacts clock{
        .fullTraversals = 2,
        .hurdleClears = 1,
        .hurdleOpportunities = 1,
        .pitClears = 1,
        .pitOpportunities = 1,
        .traversalProgress = 2.0,
        .exitDoorDistanceObserved = true,
        .bestExitDoorDistanceCells = 5.0,
    };

    const FitnessContext fullContext{
        .result = fullResult,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(clock),
    };
    const FitnessContext halfContext{
        .result = halfResult,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(clock),
    };

    const DuckFitnessBreakdown fullBreakdown = DuckEvaluator::evaluateWithBreakdown(fullContext);
    const DuckFitnessBreakdown halfBreakdown = DuckEvaluator::evaluateWithBreakdown(halfContext);

    EXPECT_DOUBLE_EQ(fullBreakdown.survivalScore, 1.0);
    EXPECT_DOUBLE_EQ(halfBreakdown.survivalScore, 0.5);
    EXPECT_NEAR(
        halfBreakdown.survivalAdjustedPoints * 2.0, fullBreakdown.survivalAdjustedPoints, 1e-9);
    EXPECT_NEAR(halfBreakdown.totalFitness * 2.0, fullBreakdown.totalFitness, 1e-9);
}

TEST(FitnessCalculatorTest, DuckClockObstacleRateRewardsMoreRepeatedClears)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult result{ .lifespan = 100.0, .maxEnergy = 0.0 };
    const FitnessContext oneClearContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .pitClears = 1,
                .pitOpportunities = 3,
            }),
    };
    const FitnessContext repeatedClearContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .pitClears = 3,
                .pitOpportunities = 3,
            }),
    };

    const DuckFitnessBreakdown oneClearBreakdown =
        DuckEvaluator::evaluateWithBreakdown(oneClearContext);
    const DuckFitnessBreakdown repeatedClearBreakdown =
        DuckEvaluator::evaluateWithBreakdown(repeatedClearContext);

    EXPECT_DOUBLE_EQ(oneClearBreakdown.obstacleClearRatePer100Seconds, 1.0);
    EXPECT_DOUBLE_EQ(oneClearBreakdown.obstacleClearRatePoints, 100.0);
    EXPECT_DOUBLE_EQ(repeatedClearBreakdown.obstacleClearRatePer100Seconds, 3.0);
    EXPECT_DOUBLE_EQ(repeatedClearBreakdown.obstacleClearRatePoints, 300.0);
    EXPECT_GT(
        repeatedClearBreakdown.obstacleCompetencePoints,
        oneClearBreakdown.obstacleCompetencePoints);
    EXPECT_GT(repeatedClearBreakdown.totalFitness, oneClearBreakdown.totalFitness);
}

TEST(FitnessCalculatorTest, DuckClockArtifactsAddTraversalObstacleAndExitRewards)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult result{ .lifespan = 100.0, .maxEnergy = 0.0 };
    const FitnessContext baseContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(DuckClockEvaluationArtifacts{}),
    };
    const FitnessContext boostedContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .fullTraversals = 2,
                .hurdleClears = 1,
                .hurdleOpportunities = 1,
                .pitClears = 1,
                .pitOpportunities = 1,
                .traversalProgress = 2.5,
                .exitDoorDistanceObserved = true,
                .bestExitDoorDistanceCells = 5.0,
            }),
    };

    const DuckFitnessBreakdown baseBreakdown = DuckEvaluator::evaluateWithBreakdown(baseContext);
    const DuckFitnessBreakdown boostedBreakdown =
        DuckEvaluator::evaluateWithBreakdown(boostedContext);
    const double obstacleCompetence = obstacleCompetenceExpected(2.0, 2.0);

    EXPECT_DOUBLE_EQ(baseBreakdown.totalFitness, 0.0);
    EXPECT_DOUBLE_EQ(boostedBreakdown.traversalRatePer100Seconds, 2.5);
    EXPECT_DOUBLE_EQ(boostedBreakdown.traversalPoints, 1250.0);
    EXPECT_DOUBLE_EQ(boostedBreakdown.obstacleClearRatePer100Seconds, 2.0);
    EXPECT_DOUBLE_EQ(boostedBreakdown.obstacleClearRatePoints, 200.0);
    EXPECT_NEAR(boostedBreakdown.obstacleCompetenceScore, obstacleCompetence, 1e-9);
    EXPECT_NEAR(boostedBreakdown.obstacleCompetencePoints, 100.0 * obstacleCompetence, 1e-9);
    EXPECT_DOUBLE_EQ(boostedBreakdown.exitDoorProximityScore, 0.5);
    EXPECT_DOUBLE_EQ(boostedBreakdown.exitDoorProximityPoints, 50.0);
    EXPECT_NEAR(boostedBreakdown.coursePoints, 1250.0 + 200.0 + (100.0 * obstacleCompetence), 1e-9);
    EXPECT_NEAR(
        boostedBreakdown.totalFitness,
        boostedBreakdown.coursePoints + boostedBreakdown.exitDoorProximityPoints,
        1e-9);
}

TEST(FitnessCalculatorTest, DuckClockTraversalPointsIncludePartialReturnProgress)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult result{ .lifespan = 100.0, .maxEnergy = 0.0 };

    const FitnessContext fullTraversalContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .fullTraversals = 1,
                .traversalProgress = 1.0,
            }),
    };
    const FitnessContext partialReturnContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .fullTraversals = 1,
                .traversalProgress = 1.5,
            }),
    };

    const DuckFitnessBreakdown fullTraversalBreakdown =
        DuckEvaluator::evaluateWithBreakdown(fullTraversalContext);
    const DuckFitnessBreakdown partialReturnBreakdown =
        DuckEvaluator::evaluateWithBreakdown(partialReturnContext);

    EXPECT_DOUBLE_EQ(fullTraversalBreakdown.traversalPoints, 500.0);
    EXPECT_DOUBLE_EQ(partialReturnBreakdown.traversalPoints, 750.0);
    EXPECT_GT(partialReturnBreakdown.totalFitness, fullTraversalBreakdown.totalFitness);
}

TEST(FitnessCalculatorTest, DuckClockObstacleCompetenceRewardsRepeatedSuccessfulClears)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult result{ .lifespan = 100.0, .maxEnergy = 0.0 };

    const FitnessContext oneLapContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .hurdleClears = 1,
                .hurdleOpportunities = 1,
                .pitClears = 1,
                .pitOpportunities = 1,
            }),
    };
    const FitnessContext repeatedCleanLapContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .hurdleClears = 3,
                .hurdleOpportunities = 3,
                .pitClears = 3,
                .pitOpportunities = 3,
            }),
    };

    const DuckFitnessBreakdown oneLapBreakdown =
        DuckEvaluator::evaluateWithBreakdown(oneLapContext);
    const DuckFitnessBreakdown repeatedCleanLapBreakdown =
        DuckEvaluator::evaluateWithBreakdown(repeatedCleanLapContext);

    EXPECT_NEAR(
        oneLapBreakdown.obstacleCompetenceScore, obstacleCompetenceExpected(2.0, 2.0), 1e-9);
    EXPECT_NEAR(
        repeatedCleanLapBreakdown.obstacleCompetenceScore,
        obstacleCompetenceExpected(6.0, 6.0),
        1e-9);
    EXPECT_GT(
        repeatedCleanLapBreakdown.obstacleCompetencePoints,
        oneLapBreakdown.obstacleCompetencePoints);
    EXPECT_GT(repeatedCleanLapBreakdown.totalFitness, oneLapBreakdown.totalFitness);
}

TEST(FitnessCalculatorTest, DuckClockExitThroughDoorUsesCompletionPoints)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 100.0;

    const FitnessResult result{ .lifespan = 100.0, .maxEnergy = 0.0 };
    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .duckArtifacts = makeClockArtifacts(
            DuckClockEvaluationArtifacts{
                .exitDoorDistanceObserved = true,
                .exitedThroughDoor = true,
                .bestExitDoorDistanceCells = 1.0,
                .exitDoorTime = 8.0,
            }),
    };

    const DuckFitnessBreakdown breakdown = DuckEvaluator::evaluateWithBreakdown(context);
    EXPECT_TRUE(breakdown.exitedThroughDoor);
    EXPECT_DOUBLE_EQ(breakdown.exitDoorProximityScore, 1.0);
    EXPECT_DOUBLE_EQ(breakdown.exitDoorProximityPoints, 100.0);
    EXPECT_DOUBLE_EQ(breakdown.exitDoorCompletionPoints, 150.0);
    EXPECT_DOUBLE_EQ(breakdown.exitDoorTime, 8.0);
    EXPECT_DOUBLE_EQ(breakdown.totalFitness, 250.0);
}

} // namespace DirtSim
