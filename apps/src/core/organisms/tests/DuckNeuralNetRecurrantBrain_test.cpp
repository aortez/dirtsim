#include "core/organisms/brains/DuckNeuralNetRecurrantBrain.h"

#include <gtest/gtest.h>
#include <random>

using namespace DirtSim;

TEST(DuckNeuralNetRecurrantBrainTest, GenomeRoundTripPreservesWeights)
{
    std::mt19937 rng(42);
    Genome genome = DuckNeuralNetRecurrantBrain::randomGenome(rng);

    ASSERT_TRUE(DuckNeuralNetRecurrantBrain::isGenomeCompatible(genome));

    DuckNeuralNetRecurrantBrain brain(genome);
    Genome roundTrip = brain.getGenome();

    EXPECT_EQ(roundTrip.weights.size(), genome.weights.size());
    EXPECT_EQ(roundTrip.weights, genome.weights);
}

TEST(DuckNeuralNetRecurrantBrainTest, GenomeCompatibilityRejectsWrongSize)
{
    Genome genome;
    genome.weights.resize(10);

    EXPECT_FALSE(DuckNeuralNetRecurrantBrain::isGenomeCompatible(genome));
}
