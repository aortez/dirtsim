#pragma once

#include "MazeModel.h"
#include <cstddef>
#include <vector>

namespace DirtSim {
namespace Ui {

class MazeSolverSession {
public:
    explicit MazeSolverSession(const MazeModel& model);

    void advance(size_t stepBudget);
    bool isComplete() const { return complete_; }

    const std::vector<int>& frontier() const { return frontier_; }
    const std::vector<uint8_t>& frontierFlags() const { return frontierFlags_; }
    const std::vector<int>& solutionPath() const { return solutionPath_; }
    const std::vector<uint8_t>& visitedFlags() const { return visitedFlags_; }

private:
    void advanceOneStep();
    void rebuildFrontier();
    void rebuildSolutionPath(int goalIndex);

    bool complete_ = false;
    const MazeModel& model_;
    std::vector<int> frontier_;
    std::vector<uint8_t> frontierFlags_;
    std::vector<uint8_t> enqueuedFlags_;
    std::vector<int> parents_;
    size_t queueHead_ = 0;
    std::vector<int> queue_;
    std::vector<int> solutionPath_;
    std::vector<uint8_t> visitedFlags_;
};

} // namespace Ui
} // namespace DirtSim
