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
    const TreeResourceTotals low_resources{ .waterAbsorbed = 0.0, .energyProduced = 0.0 };
    const TreeResourceTotals high_resources{ .waterAbsorbed = 100.0, .energyProduced = 100.0 };
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
    const TreeResourceTotals high_resources{ .waterAbsorbed = 100.0, .energyProduced = 100.0 };
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
    const OrganismTrackingHistory history = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 5.0, 0.0 },
        });
    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &history,
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &history,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_DOUBLE_EQ(fitness_low, fitness_high);
}

TEST(FitnessCalculatorTest, DuckFitnessMovementIsBounded)
{
    EvolutionConfig config = makeConfig();
    config.maxSimulationTime = 10.0;

    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory longPathHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 20.0, 0.0 },
            Vector2d{ 40.0, 0.0 },
            Vector2d{ 60.0, 0.0 },
        });
    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &longPathHistory,
    };

    const double fitness = computeFitnessForOrganism(context);
    EXPECT_GT(fitness, 1.0);
    EXPECT_LT(fitness, 2.0);
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

TEST(FitnessCalculatorTest, DuckCoverageRewardsColumnsMoreThanRows)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory columnsHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 1.0, 0.0 },
            Vector2d{ 2.0, 0.0 },
            Vector2d{ 3.0, 0.0 },
            Vector2d{ 4.0, 0.0 },
            Vector2d{ 5.0, 0.0 },
        });
    const OrganismTrackingHistory singleColumnVerticalHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 0.0, 1.0 },
            Vector2d{ 0.0, 2.0 },
            Vector2d{ 0.0, 3.0 },
            Vector2d{ 0.0, 4.0 },
            Vector2d{ 0.0, 5.0 },
        });

    const FitnessContext columnsContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .organismTrackingHistory = &columnsHistory,
    };
    const FitnessContext verticalContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .organismTrackingHistory = &singleColumnVerticalHistory,
    };

    const DuckFitnessBreakdown columnsBreakdown =
        DuckEvaluator::evaluateWithBreakdown(columnsContext);
    const DuckFitnessBreakdown verticalBreakdown =
        DuckEvaluator::evaluateWithBreakdown(verticalContext);

    EXPECT_GT(columnsBreakdown.coverageColumnScore, verticalBreakdown.coverageColumnScore);
    EXPECT_GT(verticalBreakdown.coverageRowScore, columnsBreakdown.coverageRowScore);
    EXPECT_DOUBLE_EQ(columnsBreakdown.coverageCellScore, verticalBreakdown.coverageCellScore);
    EXPECT_GT(columnsBreakdown.coverageScore, verticalBreakdown.coverageScore);
}

TEST(FitnessCalculatorTest, DuckCoverageIncludesSecondaryCellTerm)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory baseHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 1.0, 0.0 },
            Vector2d{ 2.0, 0.0 },
        });
    const OrganismTrackingHistory cellRichHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 1.0, 0.0 },
            Vector2d{ 1.0, 1.0 },
            Vector2d{ 2.0, 1.0 },
        });

    const FitnessContext baseContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .organismTrackingHistory = &baseHistory,
    };
    const FitnessContext cellRichContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .organismTrackingHistory = &cellRichHistory,
    };

    const DuckFitnessBreakdown baseBreakdown = DuckEvaluator::evaluateWithBreakdown(baseContext);
    const DuckFitnessBreakdown cellRichBreakdown =
        DuckEvaluator::evaluateWithBreakdown(cellRichContext);

    EXPECT_DOUBLE_EQ(baseBreakdown.coverageColumnScore, cellRichBreakdown.coverageColumnScore);
    EXPECT_GT(cellRichBreakdown.coverageRowScore, baseBreakdown.coverageRowScore);
    EXPECT_GT(cellRichBreakdown.coverageCellScore, baseBreakdown.coverageCellScore);
    EXPECT_GT(cellRichBreakdown.coverageScore, baseBreakdown.coverageScore);
}

TEST(FitnessCalculatorTest, DuckEffortPenaltyMakesJumpCostlierThanFullRunInput)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = config.maxSimulationTime, .maxEnergy = 0.0 };
    const OrganismTrackingHistory history = makeHistory(
        {
            Vector2d{ 0.0, 0.0 }, Vector2d{ 1.0, 0.0 }, Vector2d{ 2.0, 0.0 }, Vector2d{ 3.0, 0.0 },
            Vector2d{ 4.0, 0.0 }, Vector2d{ 5.0, 0.0 }, Vector2d{ 6.0, 0.0 }, Vector2d{ 7.0, 0.0 },
            Vector2d{ 8.0, 0.0 }, Vector2d{ 9.0, 0.0 }, Vector2d{ 9.0, 1.0 }, Vector2d{ 8.0, 1.0 },
            Vector2d{ 7.0, 1.0 }, Vector2d{ 6.0, 1.0 }, Vector2d{ 5.0, 1.0 }, Vector2d{ 4.0, 1.0 },
            Vector2d{ 3.0, 1.0 }, Vector2d{ 2.0, 1.0 }, Vector2d{ 1.0, 1.0 }, Vector2d{ 0.0, 1.0 },
        });

    auto runDuck = std::make_unique<Duck>(OrganismId{ 101 }, std::unique_ptr<DuckBrain>{});
    auto jumpDuck = std::make_unique<Duck>(OrganismId{ 102 }, std::unique_ptr<DuckBrain>{});
    for (int i = 0; i < 200; ++i) {
        runDuck->setInput({ .move = { 1.0f, 0.0f }, .jump = false });
        jumpDuck->setInput({ .move = { 0.0f, 0.0f }, .jump = true });
    }

    const FitnessContext runContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .finalOrganism = runDuck.get(),
        .organismTrackingHistory = &history,
    };
    const FitnessContext jumpContext{
        .result = result,
        .organismType = OrganismType::DUCK,
        .worldWidth = 20,
        .worldHeight = 20,
        .evolutionConfig = config,
        .finalOrganism = jumpDuck.get(),
        .organismTrackingHistory = &history,
    };

    const DuckFitnessBreakdown runBreakdown = DuckEvaluator::evaluateWithBreakdown(runContext);
    const DuckFitnessBreakdown jumpBreakdown = DuckEvaluator::evaluateWithBreakdown(jumpContext);

    EXPECT_DOUBLE_EQ(runBreakdown.coverageScore, jumpBreakdown.coverageScore);
    EXPECT_GT(jumpBreakdown.effortScore, runBreakdown.effortScore);
    EXPECT_GT(jumpBreakdown.effortPenaltyScore, runBreakdown.effortPenaltyScore);
    EXPECT_LT(jumpBreakdown.totalFitness, runBreakdown.totalFitness);
}

} // namespace DirtSim
