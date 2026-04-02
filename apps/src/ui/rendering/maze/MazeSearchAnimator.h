#pragma once

#include "MazeGeneratorSession.h"
#include "MazeSolverSession.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

namespace DirtSim {
namespace Ui {

class MazeSearchAnimator {
public:
    enum class Phase {
        Empty,
        BuildingMaze,
        SolvingMaze,
        TracingSolution,
        HoldingSolution,
    };

    struct Snapshot {
        int activeCellIndex = -1;
        const std::vector<int>* frontier = nullptr;
        const std::vector<uint8_t>* generationVisited = nullptr;
        const MazeModel* model = nullptr;
        Phase phase = Phase::Empty;
        size_t revealedSolutionLength = 0;
        const std::vector<int>* solutionPath = nullptr;
        const std::vector<uint8_t>* solverVisited = nullptr;

        bool hasMaze() const { return model != nullptr; }
    };

    MazeSearchAnimator(int width, int height);

    void advanceTick();
    void reset();

    const Snapshot& snapshot() const { return snapshot_; }

private:
    void beginNewCycle();
    void rebuildSnapshot();

    static constexpr size_t kGenerationStepsPerTick = 48;
    static constexpr size_t kHoldTicks = 75;
    static constexpr size_t kPathCellsPerTick = 4;
    static constexpr uint32_t kSeed = 0x5EA2C123u;
    static constexpr size_t kSolveStepsPerTick = 16;

    size_t holdTicksRemaining_ = 0;
    std::unique_ptr<MazeGeneratorSession> generator_;
    int height_ = 0;
    MazeModel model_;
    Phase phase_ = Phase::Empty;
    size_t revealedSolutionLength_ = 0;
    Snapshot snapshot_{};
    std::unique_ptr<MazeSolverSession> solver_;
    std::mt19937 seedRng_;
    int width_ = 0;
};

} // namespace Ui
} // namespace DirtSim
