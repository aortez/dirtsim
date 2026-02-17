#include "DuckEvaluator.h"
#include "FitnessCalculator.h"
#include "OrganismTracker.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double normalize(double value, double reference)
{
    if (reference <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, value) / reference;
}

double computePathDistanceFromSamples(const OrganismTrackingHistory* history)
{
    if (!history || history->samples.size() < 2) {
        return 0.0;
    }

    double totalDistance = 0.0;
    for (size_t i = 1; i < history->samples.size(); ++i) {
        const Vector2d delta = history->samples[i].position - history->samples[i - 1].position;
        totalDistance += delta.mag();
    }
    return totalDistance;
}

double computeDistanceScore(const FitnessContext& context)
{
    const double maxDistance = std::max(
        1.0,
        std::hypot(
            static_cast<double>(context.worldWidth), static_cast<double>(context.worldHeight)));
    const double pathDistance = computePathDistanceFromSamples(context.organismTrackingHistory);
    return normalize(pathDistance, maxDistance);
}

double computeSurvivalScore(const FitnessContext& context)
{
    return clamp01(normalize(context.result.lifespan, context.evolutionConfig.maxSimulationTime));
}

} // namespace

double DuckEvaluator::evaluate(const FitnessContext& context)
{
    return evaluateWithBreakdown(context).totalFitness;
}

DuckFitnessBreakdown DuckEvaluator::evaluateWithBreakdown(const FitnessContext& context)
{
    DuckFitnessBreakdown breakdown;
    breakdown.survivalScore = computeSurvivalScore(context);
    if (breakdown.survivalScore <= 0.0) {
        return breakdown;
    }

    breakdown.distanceScore = computeDistanceScore(context);
    breakdown.totalFitness = breakdown.survivalScore * (1.0 + breakdown.distanceScore);
    return breakdown;
}

} // namespace DirtSim
