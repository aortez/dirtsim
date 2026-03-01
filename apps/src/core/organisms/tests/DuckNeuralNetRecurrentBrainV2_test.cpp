#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"

#include <gtest/gtest.h>
#include <random>

using namespace DirtSim;

TEST(DuckNeuralNetRecurrentBrainV2Test, GenomeRoundTripPreservesWeights)
{
    std::mt19937 rng(42);
    Genome genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng);

    ASSERT_TRUE(DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome));

    DuckNeuralNetRecurrentBrainV2 brain(genome);
    Genome roundTrip = brain.getGenome();

    EXPECT_EQ(roundTrip.weights.size(), genome.weights.size());
    EXPECT_EQ(roundTrip.weights, genome.weights);
}

TEST(DuckNeuralNetRecurrentBrainV2Test, GenomeCompatibilityRejectsWrongSize)
{
    Genome genome;
    genome.weights.resize(10);

    EXPECT_FALSE(DuckNeuralNetRecurrentBrainV2::isGenomeCompatible(genome));
}
