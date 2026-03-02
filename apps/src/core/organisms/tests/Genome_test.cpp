#include "core/organisms/brains/Genome.h"

#include <gtest/gtest.h>

using namespace DirtSim;

TEST(GenomeTest, DefaultConstructorCreatesEmptyGenome)
{
    const Genome g;
    EXPECT_EQ(g.weights.size(), 0u);
    EXPECT_EQ(g.getSizeBytes(), 0u);
}

TEST(GenomeTest, SizedConstructorCreatesZeroFilledGenome)
{
    const Genome g(100);
    EXPECT_EQ(g.weights.size(), 100u);
    for (const auto& w : g.weights) {
        EXPECT_EQ(w, 0.0f);
    }
}

TEST(GenomeTest, ValueConstructorFillsWithValue)
{
    const Genome g(50, 1.5f);
    EXPECT_EQ(g.weights.size(), 50u);
    for (const auto& w : g.weights) {
        EXPECT_EQ(w, 1.5f);
    }
}
