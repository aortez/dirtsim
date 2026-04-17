#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

NesSuperMarioBrosState makeGameplayState(
    uint8_t world, uint8_t level, uint16_t absoluteX, uint8_t lives)
{
    NesSuperMarioBrosState state;
    state.phase = SmbPhase::Gameplay;
    state.gameMode = SmbGameMode::Normal;
    state.lifeState = SmbLifeState::Alive;
    state.world = world;
    state.level = level;
    state.absoluteX = absoluteX;
    state.lives = lives;
    return state;
}

} // namespace

TEST(NesSuperMarioBrosEvaluatorTest, DoesNotAwardPerFrameSurvival)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    const NesSuperMarioBrosEvaluatorOutput first = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 32, 3),
        });
    EXPECT_DOUBLE_EQ(first.rewardDelta, 0.0);
    EXPECT_DOUBLE_EQ(first.distanceRewardDelta, 0.0);
    EXPECT_DOUBLE_EQ(first.levelClearRewardDelta, 0.0);
    EXPECT_FALSE(first.done);
    EXPECT_DOUBLE_EQ(first.snapshot.totalReward, 0.0);

    const NesSuperMarioBrosEvaluatorOutput second = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 32, 3),
        });
    EXPECT_DOUBLE_EQ(second.rewardDelta, 0.0);
    EXPECT_FALSE(second.done);
    EXPECT_EQ(second.snapshot.gameplayFrames, 2u);
}

TEST(NesSuperMarioBrosEvaluatorTest, RewardsOnlyNewBestForwardProgress)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 40, 3),
        });

    const NesSuperMarioBrosEvaluatorOutput forward = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 50, 3),
        });
    EXPECT_DOUBLE_EQ(forward.rewardDelta, 5.0);
    EXPECT_DOUBLE_EQ(forward.distanceRewardDelta, 5.0);
    EXPECT_DOUBLE_EQ(forward.snapshot.totalReward, 5.0);
    EXPECT_EQ(forward.snapshot.bestAbsoluteX, 50u);
    EXPECT_FALSE(forward.done);

    const NesSuperMarioBrosEvaluatorOutput backward = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 45, 3),
        });
    EXPECT_DOUBLE_EQ(backward.rewardDelta, 0.0);
    EXPECT_FALSE(backward.done);
    EXPECT_DOUBLE_EQ(backward.snapshot.totalReward, 5.0);
    EXPECT_EQ(backward.snapshot.bestAbsoluteX, 50u);
}

TEST(NesSuperMarioBrosEvaluatorTest, RewardsLevelAdvanceAndNewLevelProgress)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 200, 3),
        });

    const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 1, 20, 3),
        });
    EXPECT_DOUBLE_EQ(output.rewardDelta, 1010.0);
    EXPECT_DOUBLE_EQ(output.levelClearRewardDelta, 1000.0);
    EXPECT_DOUBLE_EQ(output.distanceRewardDelta, 10.0);
    EXPECT_DOUBLE_EQ(output.snapshot.totalReward, 1010.0);
    EXPECT_EQ(output.snapshot.bestStageIndex, 1u);
    EXPECT_EQ(output.snapshot.bestAbsoluteX, 20u);
    EXPECT_FALSE(output.done);
}

TEST(NesSuperMarioBrosEvaluatorTest, TerminatesOnFirstLifeLoss)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 40, 3),
        });

    NesSuperMarioBrosState deathState = makeGameplayState(0, 0, 40, 2);
    deathState.lifeState = SmbLifeState::Dead;

    const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = deathState,
        });
    EXPECT_DOUBLE_EQ(output.rewardDelta, 0.0);
    EXPECT_TRUE(output.done);
    EXPECT_EQ(output.endReason, SmbEpisodeEndReason::LifeLost);
    EXPECT_TRUE(output.snapshot.done);
}

TEST(NesSuperMarioBrosEvaluatorTest, IgnoresPregameLifeRegistersBeforeGameplayStarts)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    NesSuperMarioBrosState pregameState = makeGameplayState(0, 0, 0, 0);
    pregameState.phase = SmbPhase::NonGameplay;
    pregameState.gameMode = SmbGameMode::StartDemo;
    pregameState.lifeState = SmbLifeState::Dead;

    const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = pregameState,
        });
    EXPECT_FALSE(output.done);
    EXPECT_EQ(output.endReason, SmbEpisodeEndReason::None);
    EXPECT_EQ(output.snapshot.gameplayFrames, 0u);
    EXPECT_FALSE(output.snapshot.done);
}

TEST(NesSuperMarioBrosEvaluatorTest, TerminatesOnDyingStateBeforeLivesDecrease)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 40, 3),
        });

    NesSuperMarioBrosState deathState = makeGameplayState(0, 0, 40, 3);
    deathState.lifeState = SmbLifeState::Dying;

    const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = deathState,
        });
    EXPECT_DOUBLE_EQ(output.rewardDelta, 0.0);
    EXPECT_TRUE(output.done);
    EXPECT_EQ(output.endReason, SmbEpisodeEndReason::LifeLost);
    EXPECT_TRUE(output.snapshot.done);
}

TEST(NesSuperMarioBrosEvaluatorTest, TerminatesWhenPlayerFallsBelowScreen)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 40, 3),
        });

    NesSuperMarioBrosState fallState = makeGameplayState(0, 0, 40, 3);
    fallState.playerYScreen = 224u;

    const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = fallState,
        });
    EXPECT_DOUBLE_EQ(output.rewardDelta, 0.0);
    EXPECT_TRUE(output.done);
    EXPECT_EQ(output.endReason, SmbEpisodeEndReason::FellBelowScreen);
    EXPECT_TRUE(output.snapshot.done);
}

TEST(NesSuperMarioBrosEvaluatorTest, DoesNotTreatLevelEndModeAsLoss)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 40, 3),
        });

    NesSuperMarioBrosState levelEndState = makeGameplayState(0, 0, 40, 3);
    levelEndState.phase = SmbPhase::NonGameplay;
    levelEndState.gameMode = SmbGameMode::EndCurrentWorld;

    const NesSuperMarioBrosEvaluatorOutput output = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = levelEndState,
        });
    EXPECT_FALSE(output.done);
    EXPECT_EQ(output.endReason, SmbEpisodeEndReason::None);
    EXPECT_FALSE(output.snapshot.done);
}

TEST(NesSuperMarioBrosEvaluatorTest, TerminatesAfterNoProgressTimeout)
{
    NesSuperMarioBrosEvaluator evaluator;
    evaluator.reset();

    (void)evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 16, 3),
        });

    const NesSuperMarioBrosEvaluatorOutput beforeTimeout = evaluator.evaluate(
        {
            .advancedFrames = 1799,
            .state = makeGameplayState(0, 0, 16, 3),
        });
    EXPECT_FALSE(beforeTimeout.done);
    EXPECT_DOUBLE_EQ(beforeTimeout.rewardDelta, 0.0);
    EXPECT_EQ(beforeTimeout.snapshot.framesSinceProgress, 1799u);

    const NesSuperMarioBrosEvaluatorOutput timeout = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 16, 3),
        });
    EXPECT_TRUE(timeout.done);
    EXPECT_DOUBLE_EQ(timeout.rewardDelta, 0.0);
    EXPECT_EQ(timeout.endReason, SmbEpisodeEndReason::NoProgressTimeout);
    EXPECT_TRUE(timeout.snapshot.done);
    EXPECT_EQ(timeout.snapshot.noProgressTimeoutFrames, 1800u);
}
