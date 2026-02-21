#pragma once

#include "GenomeMetadata.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace DirtSim {

// Compute median of a sample vector (modifies order via nth_element).
inline double computeMedian(std::vector<double> samples)
{
    if (samples.empty()) {
        return 0.0;
    }

    const size_t mid = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
    const double upper = samples[mid];
    if ((samples.size() % 2) != 0) {
        return upper;
    }

    std::nth_element(samples.begin(), samples.begin() + mid - 1, samples.begin() + mid);
    return (samples[mid - 1] + upper) * 0.5;
}

// Effective robust eval count with fallback for pre-robust-metadata genomes.
inline int effectiveRobustEvalCount(const GenomeMetadata& metadata)
{
    if (metadata.robustEvalCount > 0) {
        return metadata.robustEvalCount;
    }
    if (!metadata.robustFitnessSamples.empty()) {
        return static_cast<int>(metadata.robustFitnessSamples.size());
    }
    return 0;
}

// Effective robust fitness with fallback to raw fitness for old genomes.
inline double effectiveRobustFitness(const GenomeMetadata& metadata)
{
    if (metadata.robustEvalCount > 0 || !metadata.robustFitnessSamples.empty()) {
        return metadata.robustFitness;
    }
    return metadata.fitness;
}

} // namespace DirtSim
