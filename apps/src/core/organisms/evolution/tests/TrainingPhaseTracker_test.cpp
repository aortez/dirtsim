#include "core/organisms/evolution/TrainingPhaseTracker.h"

#include <gtest/gtest.h>

using namespace DirtSim;

TEST(TrainingPhaseTrackerTest, TinyGainDoesNotResetPlateauOrStuck)
{
    TrainingPhaseTracker tracker;

    EvolutionConfig config;
    config.stagnationImprovementEpsilon = 0.1;
    config.stagnationWindowGenerations = 2;
    config.recoveryWindowGenerations = 3;

    TrainingPhaseUpdate update = tracker.updateCompletedGeneration(0, 1.0, config);
    EXPECT_TRUE(update.improved);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Normal);
    EXPECT_EQ(tracker.status().lastImprovementGeneration, 0);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 0);

    update = tracker.updateCompletedGeneration(1, 1.05, config);
    EXPECT_FALSE(update.improved);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Normal);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 1);
    EXPECT_EQ(tracker.status().stagnationLevel, 0);

    update = tracker.updateCompletedGeneration(2, 1.09, config);
    EXPECT_FALSE(update.improved);
    EXPECT_TRUE(update.phaseChanged);
    EXPECT_EQ(update.phase, TrainingPhase::Plateau);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Plateau);
    EXPECT_EQ(tracker.status().lastImprovementGeneration, 0);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 2);
    EXPECT_EQ(tracker.status().stagnationLevel, 1);

    update = tracker.updateCompletedGeneration(3, 1.08, config);
    EXPECT_FALSE(update.improved);
    EXPECT_FALSE(update.phaseChanged);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Plateau);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 3);
    EXPECT_EQ(tracker.status().stagnationLevel, 1);

    update = tracker.updateCompletedGeneration(4, 1.07, config);
    EXPECT_FALSE(update.improved);
    EXPECT_TRUE(update.phaseChanged);
    EXPECT_EQ(update.phase, TrainingPhase::Stuck);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Stuck);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 4);
    EXPECT_EQ(tracker.status().stagnationLevel, 2);
}

TEST(TrainingPhaseTrackerTest, ImprovementAfterStagnationStartsRecovery)
{
    TrainingPhaseTracker tracker;

    EvolutionConfig config;
    config.stagnationImprovementEpsilon = 0.1;
    config.stagnationWindowGenerations = 2;
    config.recoveryWindowGenerations = 3;

    tracker.updateCompletedGeneration(0, 1.0, config);
    tracker.updateCompletedGeneration(1, 1.0, config);
    tracker.updateCompletedGeneration(2, 1.0, config);
    tracker.updateCompletedGeneration(3, 1.0, config);
    TrainingPhaseUpdate update = tracker.updateCompletedGeneration(4, 1.0, config);
    ASSERT_EQ(tracker.status().phase, TrainingPhase::Stuck);
    ASSERT_EQ(tracker.status().stagnationLevel, 2);

    update = tracker.updateCompletedGeneration(5, 1.2, config);
    EXPECT_TRUE(update.improved);
    EXPECT_TRUE(update.phaseChanged);
    EXPECT_EQ(update.previousPhase, TrainingPhase::Stuck);
    EXPECT_EQ(update.phase, TrainingPhase::Recovery);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Recovery);
    EXPECT_EQ(tracker.status().lastImprovementGeneration, 5);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 0);
    EXPECT_EQ(tracker.status().stagnationLevel, 0);
    EXPECT_EQ(tracker.status().recoveryLevel, 3);

    tracker.updateCompletedGeneration(6, 1.25, config);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Recovery);
    EXPECT_EQ(tracker.status().recoveryLevel, 2);

    tracker.updateCompletedGeneration(7, 1.31, config);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Recovery);
    EXPECT_EQ(tracker.status().recoveryLevel, 3);
}

TEST(TrainingPhaseTrackerTest, RecoveryDecaysBackToNormal)
{
    TrainingPhaseTracker tracker;

    EvolutionConfig config;
    config.stagnationImprovementEpsilon = 0.1;
    config.stagnationWindowGenerations = 4;
    config.recoveryWindowGenerations = 2;

    tracker.updateCompletedGeneration(0, 1.0, config);
    tracker.updateCompletedGeneration(1, 1.0, config);
    tracker.updateCompletedGeneration(2, 1.0, config);
    tracker.updateCompletedGeneration(3, 1.0, config);
    tracker.updateCompletedGeneration(4, 1.0, config);
    ASSERT_EQ(tracker.status().phase, TrainingPhase::Plateau);

    TrainingPhaseUpdate update = tracker.updateCompletedGeneration(5, 1.2, config);
    ASSERT_EQ(update.phase, TrainingPhase::Recovery);
    ASSERT_EQ(tracker.status().recoveryLevel, 2);

    update = tracker.updateCompletedGeneration(6, 1.2, config);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Recovery);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 1);
    EXPECT_EQ(tracker.status().recoveryLevel, 1);

    update = tracker.updateCompletedGeneration(7, 1.2, config);
    EXPECT_TRUE(update.phaseChanged);
    EXPECT_EQ(update.previousPhase, TrainingPhase::Recovery);
    EXPECT_EQ(update.phase, TrainingPhase::Normal);
    EXPECT_EQ(tracker.status().phase, TrainingPhase::Normal);
    EXPECT_EQ(tracker.status().generationsSinceImprovement, 2);
    EXPECT_EQ(tracker.status().recoveryLevel, 0);
    EXPECT_EQ(tracker.status().stagnationLevel, 0);
}
