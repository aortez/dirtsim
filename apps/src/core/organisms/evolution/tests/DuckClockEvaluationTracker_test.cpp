#include "core/organisms/evolution/DuckClockEvaluationTracker.h"

#include <array>
#include <gtest/gtest.h>

namespace DirtSim {

TEST(DuckClockEvaluationTrackerTest, RecordsWallTouchesTraversalsAndExitDistance)
{
    DuckClockEvaluationTracker tracker;
    tracker.reset();

    const std::array<FloorObstacle, 0> noObstacles{};
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 1, 8 },
            .duckOnGround = true,
            .obstacles = noObstacles,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 10, 8 },
            .duckOnGround = true,
            .obstacles = noObstacles,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 18, 8 },
            .duckOnGround = true,
            .exitDoorActive = true,
            .exitDoorCell = Vector2i{ 19, 8 },
            .obstacles = noObstacles,
        });

    const DuckClockEvaluationArtifacts artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.leftWallTouches, 1);
    EXPECT_EQ(artifacts.rightWallTouches, 1);
    EXPECT_EQ(artifacts.fullTraversals, 1);
    EXPECT_DOUBLE_EQ(artifacts.traversalProgress, 1.0);
    EXPECT_TRUE(artifacts.exitDoorDistanceObserved);
    EXPECT_DOUBLE_EQ(artifacts.bestExitDoorDistanceCells, 1.0);
}

TEST(DuckClockEvaluationTrackerTest, TracksPartialTraversalProgressBetweenWallTouches)
{
    DuckClockEvaluationTracker tracker;
    tracker.reset();

    const std::array<FloorObstacle, 0> noObstacles{};
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 1, 8 },
            .duckOnGround = true,
            .obstacles = noObstacles,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 10, 8 },
            .duckOnGround = true,
            .obstacles = noObstacles,
        });

    DuckClockEvaluationArtifacts artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.fullTraversals, 0);
    EXPECT_NEAR(artifacts.traversalProgress, 8.0 / 15.0, 1e-9);

    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 18, 8 },
            .duckOnGround = true,
            .obstacles = noObstacles,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 10, 8 },
            .duckOnGround = true,
            .obstacles = noObstacles,
        });

    artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.fullTraversals, 1);
    EXPECT_NEAR(artifacts.traversalProgress, 1.0 + (7.0 / 15.0), 1e-9);
}

TEST(DuckClockEvaluationTrackerTest, CountsPitAndHurdleClearsAcrossJumpArcs)
{
    DuckClockEvaluationTracker tracker;
    tracker.reset();

    const std::array<FloorObstacle, 1> pit{
        FloorObstacle{
            .start_x = 5,
            .width = 2,
            .type = FloorObstacleType::PIT,
        },
    };
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 4, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 5, 7 },
            .duckOnGround = false,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 6, 7 },
            .duckOnGround = false,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 7, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });

    const std::array<FloorObstacle, 1> hurdle{
        FloorObstacle{
            .start_x = 10,
            .width = 1,
            .type = FloorObstacleType::HURDLE,
        },
    };
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 9, 8 },
            .duckOnGround = true,
            .obstacles = hurdle,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 10, 7 },
            .duckOnGround = false,
            .obstacles = hurdle,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 11, 8 },
            .duckOnGround = true,
            .obstacles = hurdle,
        });

    const DuckClockEvaluationArtifacts artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.pitClears, 1);
    EXPECT_EQ(artifacts.pitOpportunities, 1);
    EXPECT_EQ(artifacts.hurdleClears, 1);
    EXPECT_EQ(artifacts.hurdleOpportunities, 1);
}

TEST(DuckClockEvaluationTrackerTest, DoesNotRewardFallingAcrossPitWithoutJumpLift)
{
    DuckClockEvaluationTracker tracker;
    tracker.reset();

    const std::array<FloorObstacle, 1> pit{
        FloorObstacle{
            .start_x = 5,
            .width = 2,
            .type = FloorObstacleType::PIT,
        },
    };
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 4, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 5, 8 },
            .duckOnGround = false,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 6, 9 },
            .duckOnGround = false,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 7, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });

    const DuckClockEvaluationArtifacts artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.pitClears, 0);
    EXPECT_EQ(artifacts.pitOpportunities, 1);
}

TEST(DuckClockEvaluationTrackerTest, RearmsObstacleOpportunityAfterOppositeWallTraversal)
{
    DuckClockEvaluationTracker tracker;
    tracker.reset();

    const std::array<FloorObstacle, 1> pit{
        FloorObstacle{
            .start_x = 5,
            .width = 2,
            .type = FloorObstacleType::PIT,
        },
    };

    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 1, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });

    DuckClockEvaluationArtifacts artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.pitOpportunities, 1);

    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 4, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 8, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 4, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });

    artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.pitOpportunities, 1);

    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 18, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });
    tracker.update(
        DuckClockTrackerFrame{
            .worldWidth = 20,
            .duckAnchorCell = { 7, 8 },
            .duckOnGround = true,
            .obstacles = pit,
        });

    artifacts = tracker.buildArtifacts();
    EXPECT_EQ(artifacts.pitOpportunities, 2);
}

TEST(DuckClockEvaluationTrackerTest, MarkExitedThroughDoorSetsFullExitState)
{
    DuckClockEvaluationTracker tracker;
    tracker.reset();

    tracker.markExitedThroughDoor(12.5);

    const DuckClockEvaluationArtifacts artifacts = tracker.buildArtifacts();
    EXPECT_TRUE(artifacts.exitDoorDistanceObserved);
    EXPECT_TRUE(artifacts.exitedThroughDoor);
    EXPECT_DOUBLE_EQ(artifacts.bestExitDoorDistanceCells, 0.0);
    EXPECT_DOUBLE_EQ(artifacts.exitDoorTime, 12.5);
}

} // namespace DirtSim
