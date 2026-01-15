#include "core/organisms/brains/Genome.h"

#include <gtest/gtest.h>

using namespace DirtSim;

TEST(GenomeTest, SizeMatchesExpected)
{
    Genome g;
    EXPECT_EQ(g.weights.size(), Genome::EXPECTED_WEIGHT_COUNT);
    EXPECT_EQ(g.getSizeBytes(), Genome::EXPECTED_SIZE_BYTES);
    EXPECT_EQ(g.getSizeBytes(), 480352u);
}
