#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

NesSuperMarioBrosState makeGameplayState(
    uint8_t world, uint8_t level, uint16_t absoluteX, uint8_t lives)
{
    NesSuperMarioBrosState state;
    state.phase = SmbPhase::Gameplay;
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
    EXPECT_FALSE(first.done);

    const NesSuperMarioBrosEvaluatorOutput second = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 32, 3),
        });
    EXPECT_DOUBLE_EQ(second.rewardDelta, 0.0);
    EXPECT_FALSE(second.done);
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
    EXPECT_FALSE(forward.done);

    const NesSuperMarioBrosEvaluatorOutput backward = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 45, 3),
        });
    EXPECT_DOUBLE_EQ(backward.rewardDelta, 0.0);
    EXPECT_FALSE(backward.done);
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

    const NesSuperMarioBrosEvaluatorOutput timeout = evaluator.evaluate(
        {
            .advancedFrames = 1,
            .state = makeGameplayState(0, 0, 16, 3),
        });
    EXPECT_TRUE(timeout.done);
    EXPECT_DOUBLE_EQ(timeout.rewardDelta, 0.0);
}
