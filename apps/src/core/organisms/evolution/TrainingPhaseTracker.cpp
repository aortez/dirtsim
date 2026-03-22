#include "TrainingPhaseTracker.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {
namespace {

int normalizeRecoveryWindow(int value)
{
    return std::max(value, 0);
}

int normalizeStagnationWindow(int value)
{
    return std::max(value, 1);
}

int stagnationLevelCompute(int generationsSinceImprovement, int stagnationWindowGenerations)
{
    if (generationsSinceImprovement >= stagnationWindowGenerations * 2) {
        return 2;
    }
    if (generationsSinceImprovement >= stagnationWindowGenerations) {
        return 1;
    }
    return 0;
}

TrainingPhase phaseResolve(const TrainingPhaseStatus& status)
{
    if (status.stagnationLevel >= 2) {
        return TrainingPhase::Stuck;
    }
    if (status.stagnationLevel >= 1) {
        return TrainingPhase::Plateau;
    }
    if (status.recoveryLevel > 0) {
        return TrainingPhase::Recovery;
    }
    return TrainingPhase::Normal;
}

} // namespace

void TrainingPhaseTracker::reset()
{
    lastMeaningfulBestFitness_ = std::numeric_limits<double>::lowest();
    status_ = {};
}

const TrainingPhaseStatus& TrainingPhaseTracker::status() const
{
    return status_;
}

double TrainingPhaseTracker::lastMeaningfulBestFitness() const
{
    return lastMeaningfulBestFitness_;
}

TrainingPhaseUpdate TrainingPhaseTracker::updateCompletedGeneration(
    int generation, double completedBestFitness, const EvolutionConfig& config)
{
    const TrainingPhase previousPhase = status_.phase;
    const int recoveryWindowGenerations = normalizeRecoveryWindow(config.recoveryWindowGenerations);
    const int stagnationWindowGenerations =
        normalizeStagnationWindow(config.stagnationWindowGenerations);
    const double improvementEpsilon = std::max(0.0, config.stagnationImprovementEpsilon);
    const bool improved = !std::isfinite(lastMeaningfulBestFitness_)
        || completedBestFitness > lastMeaningfulBestFitness_ + improvementEpsilon;

    if (improved) {
        const bool wasStagnated =
            status_.stagnationLevel > 0 || status_.phase == TrainingPhase::Recovery;
        lastMeaningfulBestFitness_ = completedBestFitness;
        status_.lastImprovementGeneration = generation;
        status_.generationsSinceImprovement = 0;
        status_.stagnationLevel = 0;
        status_.recoveryLevel = wasStagnated ? recoveryWindowGenerations : 0;
    }
    else {
        status_.generationsSinceImprovement++;
        status_.stagnationLevel = stagnationLevelCompute(
            status_.generationsSinceImprovement, stagnationWindowGenerations);
        if (status_.recoveryLevel > 0) {
            status_.recoveryLevel--;
        }
    }

    status_.phase = phaseResolve(status_);

    return TrainingPhaseUpdate{
        .improved = improved,
        .phaseChanged = status_.phase != previousPhase,
        .previousPhase = previousPhase,
        .phase = status_.phase,
    };
}

} // namespace DirtSim
