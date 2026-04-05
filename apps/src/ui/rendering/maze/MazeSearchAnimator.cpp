#include "MazeSearchAnimator.h"
#include "core/Assert.h"

namespace DirtSim {
namespace Ui {

MazeSearchAnimator::MazeSearchAnimator(int width, int height)
    : height_(height), seedRng_(kSeed), width_(width)
{
    DIRTSIM_ASSERT(width > 0, "MazeSearchAnimator width must be positive");
    DIRTSIM_ASSERT(height > 0, "MazeSearchAnimator height must be positive");
    rebuildSnapshot();
}

void MazeSearchAnimator::advanceTick()
{
    if (phase_ == Phase::Empty) {
        beginNewCycle();
    }

    switch (phase_) {
        case Phase::Empty:
            break;

        case Phase::BuildingMaze:
            DIRTSIM_ASSERT(generator_, "MazeSearchAnimator generator must exist");
            generator_->advance(kGenerationStepsPerTick);
            if (generator_->isComplete()) {
                solver_ = std::make_unique<MazeSolverSession>(model_);
                phase_ = Phase::SolvingMaze;
            }
            break;

        case Phase::SolvingMaze:
            DIRTSIM_ASSERT(solver_, "MazeSearchAnimator solver must exist");
            solver_->advance(kSolveStepsPerTick);
            if (solver_->isComplete()) {
                revealedSolutionLength_ = 0;
                phase_ = Phase::TracingSolution;
            }
            break;

        case Phase::TracingSolution:
            DIRTSIM_ASSERT(solver_, "MazeSearchAnimator solver must exist");
            revealedSolutionLength_ += kPathCellsPerTick;
            if (revealedSolutionLength_ >= solver_->solutionPath().size()) {
                revealedSolutionLength_ = solver_->solutionPath().size();
                holdTicksRemaining_ = kHoldTicks;
                phase_ = Phase::HoldingSolution;
            }
            break;

        case Phase::HoldingSolution:
            if (holdTicksRemaining_ > 0) {
                --holdTicksRemaining_;
            }
            if (holdTicksRemaining_ == 0) {
                phase_ = Phase::Empty;
                generator_.reset();
                solver_.reset();
            }
            break;
    }

    rebuildSnapshot();
}

void MazeSearchAnimator::reset()
{
    generator_.reset();
    solver_.reset();
    model_ = MazeModel{};
    phase_ = Phase::Empty;
    holdTicksRemaining_ = 0;
    revealedSolutionLength_ = 0;
    rebuildSnapshot();
}

void MazeSearchAnimator::beginNewCycle()
{
    model_ = MazeModel(width_, height_);
    model_.setEndpoints(0, model_.cellCount() - 1);

    std::uniform_int_distribution<uint32_t> seedDist;
    generator_ = std::make_unique<MazeGeneratorSession>(model_, seedDist(seedRng_));
    solver_.reset();
    phase_ = Phase::BuildingMaze;
    revealedSolutionLength_ = 0;
}

void MazeSearchAnimator::rebuildSnapshot()
{
    snapshot_ = Snapshot{
        .activeCellIndex = -1,
        .frontier = nullptr,
        .frontierFlags = nullptr,
        .generationVisited = nullptr,
        .model = nullptr,
        .phase = phase_,
        .revealedSolutionLength = revealedSolutionLength_,
        .solutionPath = nullptr,
        .solverVisited = nullptr,
    };

    if (phase_ == Phase::Empty) {
        return;
    }

    snapshot_.model = &model_;

    if (generator_) {
        snapshot_.activeCellIndex = generator_->activeCellIndex();
        snapshot_.generationVisited = &generator_->visitedFlags();
    }

    if (solver_) {
        snapshot_.frontier = &solver_->frontier();
        snapshot_.frontierFlags = &solver_->frontierFlags();
        snapshot_.solutionPath = &solver_->solutionPath();
        snapshot_.solverVisited = &solver_->visitedFlags();
    }
}

} // namespace Ui
} // namespace DirtSim
