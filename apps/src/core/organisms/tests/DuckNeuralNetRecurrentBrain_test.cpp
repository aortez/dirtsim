#include "core/organisms/brains/DuckNeuralNetRecurrentBrain.h"

#include <gtest/gtest.h>
#include <random>

using namespace DirtSim;

TEST(DuckNeuralNetRecurrentBrainTest, GenomeRoundTripPreservesWeights)
{
    std::mt19937 rng(42);
    Genome genome = DuckNeuralNetRecurrentBrain::randomGenome(rng);

    ASSERT_TRUE(DuckNeuralNetRecurrentBrain::isGenomeCompatible(genome));

    DuckNeuralNetRecurrentBrain brain(genome);
    Genome roundTrip = brain.getGenome();

    EXPECT_EQ(roundTrip.weights.size(), genome.weights.size());
    EXPECT_EQ(roundTrip.weights, genome.weights);
}

TEST(DuckNeuralNetRecurrentBrainTest, GenomeCompatibilityRejectsWrongSize)
{
    Genome genome;
    genome.weights.resize(10);

    EXPECT_FALSE(DuckNeuralNetRecurrentBrain::isGenomeCompatible(genome));
}
