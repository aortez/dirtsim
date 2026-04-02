#include "MazeSolverSession.h"
#include "core/Assert.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {

MazeSolverSession::MazeSolverSession(const MazeModel& model)
    : model_(model),
      enqueuedFlags_(static_cast<size_t>(model.cellCount()), 0),
      parents_(static_cast<size_t>(model.cellCount()), -1),
      visitedFlags_(static_cast<size_t>(model.cellCount()), 0)
{
    DIRTSIM_ASSERT(model.cellCount() > 0, "MazeSolverSession requires a non-empty model");

    const int startIndex = model_.startIndex();
    queue_.push_back(startIndex);
    enqueuedFlags_.at(static_cast<size_t>(startIndex)) = 1;
    rebuildFrontier();
}

void MazeSolverSession::advance(size_t stepBudget)
{
    if (complete_) {
        return;
    }

    for (size_t i = 0; i < stepBudget && !complete_; ++i) {
        advanceOneStep();
    }
}

void MazeSolverSession::advanceOneStep()
{
    if (queueHead_ >= queue_.size()) {
        complete_ = true;
        frontier_.clear();
        return;
    }

    const int currentIndex = queue_.at(queueHead_++);
    visitedFlags_.at(static_cast<size_t>(currentIndex)) = 1;

    if (currentIndex == model_.goalIndex()) {
        rebuildSolutionPath(currentIndex);
        frontier_.clear();
        complete_ = true;
        return;
    }

    const MazeCell& currentCell = model_.cellAt(currentIndex);
    const MazeCoord currentCoord = model_.coordForIndex(currentIndex);
    for (const MazeDirection direction :
         { MazeDirection::North, MazeDirection::East, MazeDirection::South, MazeDirection::West }) {
        if ((currentCell.openings & mazeDirectionBit(direction)) == 0) {
            continue;
        }

        const MazeCoord delta = mazeDirectionDelta(direction);
        const MazeCoord neighborCoord{
            .x = currentCoord.x + delta.x,
            .y = currentCoord.y + delta.y,
        };
        DIRTSIM_ASSERT(
            model_.isValidCoord(neighborCoord), "MazeSolverSession neighbor out of range");

        const int neighborIndex = model_.indexForCoord(neighborCoord);
        if (enqueuedFlags_.at(static_cast<size_t>(neighborIndex)) != 0) {
            continue;
        }

        enqueuedFlags_.at(static_cast<size_t>(neighborIndex)) = 1;
        parents_.at(static_cast<size_t>(neighborIndex)) = currentIndex;
        queue_.push_back(neighborIndex);
    }

    rebuildFrontier();
}

void MazeSolverSession::rebuildFrontier()
{
    frontier_.clear();
    frontier_.reserve(queue_.size() - queueHead_);
    for (size_t i = queueHead_; i < queue_.size(); ++i) {
        frontier_.push_back(queue_.at(i));
    }
}

void MazeSolverSession::rebuildSolutionPath(int goalIndex)
{
    solutionPath_.clear();

    int currentIndex = goalIndex;
    while (currentIndex >= 0) {
        solutionPath_.push_back(currentIndex);
        currentIndex = parents_.at(static_cast<size_t>(currentIndex));
    }

    std::reverse(solutionPath_.begin(), solutionPath_.end());
}

} // namespace Ui
} // namespace DirtSim
