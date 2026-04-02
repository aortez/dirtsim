#pragma once

#include "MazeModel.h"
#include <cstdint>
#include <random>
#include <vector>

namespace DirtSim {
namespace Ui {

class MazeGeneratorSession {
public:
    MazeGeneratorSession(MazeModel& model, uint32_t seed);

    void advance(size_t stepBudget);
    bool isComplete() const { return complete_; }

    int activeCellIndex() const { return activeCellIndex_; }
    const std::vector<uint8_t>& visitedFlags() const { return visitedFlags_; }

private:
    struct NeighborChoice {
        MazeDirection direction = MazeDirection::North;
        int index = -1;
    };

    void advanceOneStep();
    std::vector<NeighborChoice> collectUnvisitedNeighbors(int index) const;
    void openPassage(int fromIndex, MazeDirection direction, int toIndex);

    int activeCellIndex_ = -1;
    bool complete_ = false;
    MazeModel& model_;
    std::mt19937 rng_;
    std::vector<int> stack_;
    std::vector<uint8_t> visitedFlags_;
    size_t visitedCount_ = 0;
};

} // namespace Ui
} // namespace DirtSim
