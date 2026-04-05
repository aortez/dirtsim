#include "ui/rendering/maze/MazeSearchAnimator.h"
#include <gtest/gtest.h>
#include <set>

using namespace DirtSim::Ui;

namespace {

std::vector<int> collectReachableCells(const MazeModel& model)
{
    std::vector<int> reachable;
    std::vector<uint8_t> visited(static_cast<size_t>(model.cellCount()), 0);
    std::vector<int> queue{ model.startIndex() };
    visited.at(static_cast<size_t>(model.startIndex())) = 1;

    for (size_t head = 0; head < queue.size(); ++head) {
        const int index = queue.at(head);
        reachable.push_back(index);

        const MazeCell& cell = model.cellAt(index);
        const MazeCoord coord = model.coordForIndex(index);
        for (const MazeDirection direction : { MazeDirection::North,
                                               MazeDirection::East,
                                               MazeDirection::South,
                                               MazeDirection::West }) {
            if ((cell.openings & mazeDirectionBit(direction)) == 0) {
                continue;
            }

            const MazeCoord delta = mazeDirectionDelta(direction);
            const MazeCoord neighborCoord{
                .x = coord.x + delta.x,
                .y = coord.y + delta.y,
            };
            const int neighborIndex = model.indexForCoord(neighborCoord);
            if (visited.at(static_cast<size_t>(neighborIndex)) != 0) {
                continue;
            }

            visited.at(static_cast<size_t>(neighborIndex)) = 1;
            queue.push_back(neighborIndex);
        }
    }

    return reachable;
}

bool areConnected(const MazeModel& model, int fromIndex, int toIndex)
{
    const MazeCoord fromCoord = model.coordForIndex(fromIndex);
    const MazeCoord toCoord = model.coordForIndex(toIndex);
    const MazeCoord delta{
        .x = toCoord.x - fromCoord.x,
        .y = toCoord.y - fromCoord.y,
    };

    for (const MazeDirection direction :
         { MazeDirection::North, MazeDirection::East, MazeDirection::South, MazeDirection::West }) {
        const MazeCoord directionDelta = mazeDirectionDelta(direction);
        if (directionDelta.x != delta.x || directionDelta.y != delta.y) {
            continue;
        }

        return (model.cellAt(fromIndex).openings & mazeDirectionBit(direction)) != 0;
    }

    return false;
}

} // namespace

TEST(MazeSearchAnimatorTest, BuildsConnectedMazeAndSolutionPath)
{
    MazeSearchAnimator animator(21, 13);

    for (int i = 0; i < 4000; ++i) {
        animator.advanceTick();
        if (animator.snapshot().phase == MazeSearchAnimator::Phase::HoldingSolution) {
            break;
        }
    }

    const auto& snapshot = animator.snapshot();
    ASSERT_TRUE(snapshot.hasMaze());
    ASSERT_EQ(snapshot.phase, MazeSearchAnimator::Phase::HoldingSolution);
    ASSERT_NE(snapshot.solutionPath, nullptr);
    ASSERT_NE(snapshot.model, nullptr);

    const MazeModel& model = *snapshot.model;
    EXPECT_EQ(collectReachableCells(model).size(), static_cast<size_t>(model.cellCount()));

    const auto& path = *snapshot.solutionPath;
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), model.startIndex());
    EXPECT_EQ(path.back(), model.goalIndex());
    for (size_t i = 1; i < path.size(); ++i) {
        EXPECT_TRUE(areConnected(model, path.at(i - 1), path.at(i)));
    }
}

TEST(MazeSearchAnimatorTest, ResetReturnsToEmptyAndRestartsLazyBuild)
{
    MazeSearchAnimator animator(21, 13);

    animator.advanceTick();
    EXPECT_EQ(animator.snapshot().phase, MazeSearchAnimator::Phase::BuildingMaze);
    EXPECT_TRUE(animator.snapshot().hasMaze());

    animator.reset();
    EXPECT_EQ(animator.snapshot().phase, MazeSearchAnimator::Phase::Empty);
    EXPECT_FALSE(animator.snapshot().hasMaze());

    animator.advanceTick();
    EXPECT_EQ(animator.snapshot().phase, MazeSearchAnimator::Phase::BuildingMaze);
    EXPECT_TRUE(animator.snapshot().hasMaze());
}

TEST(MazeSearchAnimatorTest, SolverFrontierAppearsBeforeSolvedPath)
{
    MazeSearchAnimator animator(17, 11);

    bool sawSolving = false;
    bool sawFrontier = false;
    for (int i = 0; i < 4000; ++i) {
        animator.advanceTick();
        const auto& snapshot = animator.snapshot();
        if (snapshot.phase == MazeSearchAnimator::Phase::SolvingMaze) {
            sawSolving = true;
            ASSERT_NE(snapshot.frontier, nullptr);
            if (!snapshot.frontier->empty()) {
                sawFrontier = true;
                break;
            }
        }
    }

    EXPECT_TRUE(sawSolving);
    EXPECT_TRUE(sawFrontier);
}
