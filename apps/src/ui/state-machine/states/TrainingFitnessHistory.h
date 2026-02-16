#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace DirtSim {

namespace Api {
struct EvolutionProgress;
}

namespace Ui {
namespace State {

class TrainingFitnessHistory {
public:
    struct Config {
        double windowSeconds = 120.0;
    };

    TrainingFitnessHistory();
    explicit TrainingFitnessHistory(Config config);

    void append(const Api::EvolutionProgress& progress);
    void clear();

    bool hasSamples() const;

    void getSeries(
        size_t maxPoints, std::vector<float>& averageSeries, std::vector<float>& bestSeries) const;

private:
    struct Sample {
        double totalTrainingSeconds = 0.0;
        float averageFitness = 0.0f;
        float bestFitnessAllTime = 0.0f;
    };

    void pruneOldSamples(double newestTimeSeconds);

    Config config_;
    std::deque<Sample> samples_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
