#include "TrainingFitnessHistory.h"
#include "server/api/EvolutionProgress.h"
#include <algorithm>
#include <cmath>

namespace DirtSim {
namespace Ui {
namespace State {

namespace {
constexpr double timestampEpsilonSeconds = 1e-6;
}

TrainingFitnessHistory::TrainingFitnessHistory(Config config) : config_(config)
{}

TrainingFitnessHistory::TrainingFitnessHistory() : TrainingFitnessHistory(Config{})
{}

void TrainingFitnessHistory::append(const Api::EvolutionProgress& progress)
{
    if (progress.currentEval <= 0) {
        return;
    }

    if (!samples_.empty()
        && (progress.totalTrainingSeconds + timestampEpsilonSeconds)
            < samples_.back().totalTrainingSeconds) {
        clear();
    }

    const Sample newSample{
        .totalTrainingSeconds = progress.totalTrainingSeconds,
        .averageFitness = static_cast<float>(progress.averageFitness),
        .bestFitnessAllTime = static_cast<float>(progress.bestFitnessAllTime),
    };

    if (!samples_.empty()
        && std::abs(progress.totalTrainingSeconds - samples_.back().totalTrainingSeconds)
            <= timestampEpsilonSeconds) {
        samples_.back() = newSample;
    }
    else {
        samples_.push_back(newSample);
    }

    pruneOldSamples(progress.totalTrainingSeconds);
}

void TrainingFitnessHistory::clear()
{
    samples_.clear();
}

bool TrainingFitnessHistory::hasSamples() const
{
    return !samples_.empty();
}

void TrainingFitnessHistory::getSeries(
    size_t maxPoints, std::vector<float>& averageSeries, std::vector<float>& bestSeries) const
{
    averageSeries.clear();
    bestSeries.clear();

    if (samples_.empty() || maxPoints == 0) {
        return;
    }

    const size_t sampleCount = samples_.size();
    const size_t outputCount = std::min(maxPoints, sampleCount);
    averageSeries.reserve(outputCount);
    bestSeries.reserve(outputCount);

    if (outputCount == sampleCount) {
        for (const auto& sample : samples_) {
            averageSeries.push_back(sample.averageFitness);
            bestSeries.push_back(sample.bestFitnessAllTime);
        }
        return;
    }

    if (outputCount == 1) {
        averageSeries.push_back(samples_.back().averageFitness);
        bestSeries.push_back(samples_.back().bestFitnessAllTime);
        return;
    }

    const double maxSourceIndex = static_cast<double>(sampleCount - 1);
    const double maxOutputIndex = static_cast<double>(outputCount - 1);
    for (size_t i = 0; i < outputCount; ++i) {
        const double source = (static_cast<double>(i) * maxSourceIndex) / maxOutputIndex;
        const size_t sourceIndex = static_cast<size_t>(std::llround(source));
        const size_t clampedIndex = std::min(sourceIndex, sampleCount - 1);
        averageSeries.push_back(samples_[clampedIndex].averageFitness);
        bestSeries.push_back(samples_[clampedIndex].bestFitnessAllTime);
    }
}

void TrainingFitnessHistory::pruneOldSamples(double newestTimeSeconds)
{
    if (samples_.empty() || config_.windowSeconds <= 0.0) {
        return;
    }

    const double cutoff = newestTimeSeconds - config_.windowSeconds;
    while (samples_.size() > 1 && samples_.front().totalTrainingSeconds < cutoff) {
        samples_.pop_front();
    }
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
