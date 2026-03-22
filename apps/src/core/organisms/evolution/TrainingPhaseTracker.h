#pragma once

#include "EvolutionConfig.h"
#include "TrainingPhase.h"

#include <limits>

namespace DirtSim {

struct TrainingPhaseStatus {
    int generationsSinceImprovement = 0;
    int lastImprovementGeneration = -1;
    int stagnationLevel = 0;
    int recoveryLevel = 0; // Remaining generations in the recovery decay window.
    TrainingPhase phase = TrainingPhase::Normal;
};

struct TrainingPhaseUpdate {
    bool improved = false;
    bool phaseChanged = false;
    TrainingPhase previousPhase = TrainingPhase::Normal;
    TrainingPhase phase = TrainingPhase::Normal;
};

class TrainingPhaseTracker {
public:
    void reset();

    const TrainingPhaseStatus& status() const;
    double lastMeaningfulBestFitness() const;

    TrainingPhaseUpdate updateCompletedGeneration(
        int generation, double completedBestFitness, const EvolutionConfig& config);

private:
    double lastMeaningfulBestFitness_ = std::numeric_limits<double>::lowest();
    TrainingPhaseStatus status_{};
};

} // namespace DirtSim
