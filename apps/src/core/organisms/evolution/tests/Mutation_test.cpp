#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/Mutation.h"

#include <gtest/gtest.h>

using namespace DirtSim;

class MutationTest : public ::testing::Test {
protected:
    std::mt19937 rng{ 42 };
};

TEST_F(MutationTest, MutationChangesWeights)
{
    const Genome parent = Genome::constant(1.0);
    const MutationConfig config{
        .rate = 0.5, // High rate to ensure changes.
        .sigma = 0.1,
        .resetRate = 0.0,
    };

    const Genome child = mutate(parent, config, rng);

    // At least some weights should differ.
    int changed = 0;
    for (size_t i = 0; i < parent.weights.size(); i++) {
        if (parent.weights[i] != child.weights[i]) {
            changed++;
        }
    }
    EXPECT_GT(changed, 0);
}

TEST_F(MutationTest, ZeroRateProducesIdenticalGenome)
{
    const Genome parent = Genome::constant(1.0);
    const MutationConfig config{
        .rate = 0.0,
        .sigma = 0.1,
        .resetRate = 0.0,
    };

    const Genome child = mutate(parent, config, rng);

    EXPECT_EQ(parent.weights, child.weights);
}

TEST_F(MutationTest, MutationPreservesGenomeSize)
{
    const Genome parent = Genome::random(rng);
    const MutationConfig config{
        .rate = 0.1,
        .sigma = 0.05,
        .resetRate = 0.001,
    };

    const Genome child = mutate(parent, config, rng);

    EXPECT_EQ(parent.weights.size(), child.weights.size());
}

TEST_F(MutationTest, HighResetRateChangesWeightsSignificantly)
{
    const Genome parent = Genome::constant(0.0);
    const MutationConfig config{
        .rate = 0.0,
        .sigma = 0.5,
        .resetRate = 1.0, // Reset everything.
    };

    const Genome child = mutate(parent, config, rng);

    // With full reset and non-zero sigma, weights should change.
    int changed = 0;
    for (size_t i = 0; i < parent.weights.size(); i++) {
        if (parent.weights[i] != child.weights[i]) {
            changed++;
        }
    }
    EXPECT_EQ(changed, static_cast<int>(parent.weights.size()));
}
