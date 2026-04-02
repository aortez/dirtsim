#include "MazeGeneratorSession.h"
#include "core/Assert.h"

namespace DirtSim {
namespace Ui {

MazeGeneratorSession::MazeGeneratorSession(MazeModel& model, uint32_t seed)
    : model_(model), rng_(seed), visitedFlags_(static_cast<size_t>(model.cellCount()), 0)
{
    DIRTSIM_ASSERT(model.cellCount() > 0, "MazeGeneratorSession requires a non-empty model");

    std::uniform_int_distribution<int> startDist(0, model.cellCount() - 1);
    activeCellIndex_ = startDist(rng_);
    stack_.push_back(activeCellIndex_);
    visitedFlags_.at(static_cast<size_t>(activeCellIndex_)) = 1;
    visitedCount_ = 1;
}

void MazeGeneratorSession::advance(size_t stepBudget)
{
    if (complete_) {
        return;
    }

    for (size_t i = 0; i < stepBudget && !complete_; ++i) {
        advanceOneStep();
    }
}

void MazeGeneratorSession::advanceOneStep()
{
    if (stack_.empty()) {
        complete_ = true;
        activeCellIndex_ = -1;
        return;
    }

    const int currentIndex = stack_.back();
    activeCellIndex_ = currentIndex;

    const auto choices = collectUnvisitedNeighbors(currentIndex);
    if (choices.empty()) {
        stack_.pop_back();
        if (stack_.empty()) {
            complete_ = true;
            activeCellIndex_ = -1;
        }
        else {
            activeCellIndex_ = stack_.back();
        }
        return;
    }

    std::uniform_int_distribution<size_t> neighborDist(0, choices.size() - 1);
    const NeighborChoice choice = choices.at(neighborDist(rng_));
    openPassage(currentIndex, choice.direction, choice.index);

    visitedFlags_.at(static_cast<size_t>(choice.index)) = 1;
    ++visitedCount_;
    stack_.push_back(choice.index);
    activeCellIndex_ = choice.index;
}

std::vector<MazeGeneratorSession::NeighborChoice> MazeGeneratorSession::collectUnvisitedNeighbors(
    int index) const
{
    const MazeCoord coord = model_.coordForIndex(index);
    std::vector<NeighborChoice> choices;
    choices.reserve(4);

    for (const MazeDirection direction :
         { MazeDirection::North, MazeDirection::East, MazeDirection::South, MazeDirection::West }) {
        const MazeCoord delta = mazeDirectionDelta(direction);
        const MazeCoord neighborCoord{
            .x = coord.x + delta.x,
            .y = coord.y + delta.y,
        };

        if (!model_.isValidCoord(neighborCoord)) {
            continue;
        }

        const int neighborIndex = model_.indexForCoord(neighborCoord);
        if (visitedFlags_.at(static_cast<size_t>(neighborIndex)) != 0) {
            continue;
        }

        choices.push_back(
            NeighborChoice{
                .direction = direction,
                .index = neighborIndex,
            });
    }

    return choices;
}

void MazeGeneratorSession::openPassage(int fromIndex, MazeDirection direction, int toIndex)
{
    model_.cellAt(fromIndex).openings |= mazeDirectionBit(direction);
    model_.cellAt(toIndex).openings |= mazeDirectionBit(mazeOppositeDirection(direction));
}

} // namespace Ui
} // namespace DirtSim
