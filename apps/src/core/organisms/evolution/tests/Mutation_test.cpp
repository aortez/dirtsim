#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeLayout.h"
#include "core/organisms/evolution/Mutation.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

// Small layout for most tests: 3 segments of varying sizes.
GenomeLayout makeTestLayout()
{
    return GenomeLayout{
        .segments = {
            { "a", 40 },
            { "b", 50 },
            { "c", 10 },
        },
    };
}

constexpr int kTestGenomeSize = 100; // Must match sum of makeTestLayout() segments.

} // namespace

class MutationTest : public ::testing::Test {
protected:
    std::mt19937 rng{ 42 };
};

TEST_F(MutationTest, MutationChangesWeights)
{
    const Genome parent(kTestGenomeSize, 1.0f);
    const auto layout = makeTestLayout();
    const MutationConfig config{
        .useBudget = false,
        .rate = 0.5, // High rate to ensure changes.
        .sigma = 0.1,
        .resetRate = 0.0,
    };

    const Genome child = mutate(parent, config, layout, rng);

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
    const Genome parent(kTestGenomeSize, 1.0f);
    const auto layout = makeTestLayout();
    const MutationConfig config{
        .useBudget = false,
        .rate = 0.0,
        .sigma = 0.1,
        .resetRate = 0.0,
    };

    const Genome child = mutate(parent, config, layout, rng);

    EXPECT_EQ(parent.weights, child.weights);
}

TEST_F(MutationTest, MutationPreservesGenomeSize)
{
    std::mt19937 genRng(99);
    const Genome parent = NeuralNetBrain::randomGenome(genRng);
    const auto layout = NeuralNetBrain::getGenomeLayout();
    const MutationConfig config{
        .useBudget = false,
        .rate = 0.1,
        .sigma = 0.05,
        .resetRate = 0.001,
    };

    const Genome child = mutate(parent, config, layout, rng);

    EXPECT_EQ(parent.weights.size(), child.weights.size());
}

TEST_F(MutationTest, HighResetRateChangesWeightsSignificantly)
{
    const Genome parent(kTestGenomeSize, 0.0f);
    const auto layout = makeTestLayout();
    const MutationConfig config{
        .useBudget = false,
        .rate = 0.0,
        .sigma = 0.5,
        .resetRate = 1.0, // Reset everything.
    };

    const Genome child = mutate(parent, config, layout, rng);

    // With full reset and non-zero sigma, weights should change.
    int changed = 0;
    for (size_t i = 0; i < parent.weights.size(); i++) {
        if (parent.weights[i] != child.weights[i]) {
            changed++;
        }
    }
    EXPECT_EQ(changed, static_cast<int>(parent.weights.size()));
}

TEST_F(MutationTest, StatsCountsMutations)
{
    const Genome parent(kTestGenomeSize, 0.0f);
    const auto layout = makeTestLayout();
    const MutationConfig config{
        .useBudget = false,
        .rate = 0.0,
        .sigma = 0.5,
        .resetRate = 1.0, // Reset everything.
    };

    MutationStats stats;
    (void)mutate(parent, config, layout, rng, &stats);

    EXPECT_EQ(stats.resets, static_cast<int>(parent.weights.size()));
    EXPECT_EQ(stats.perturbations, 0);
    EXPECT_EQ(stats.totalChanges(), static_cast<int>(parent.weights.size()));
}

TEST_F(MutationTest, BudgetedMutationUsesFixedCounts)
{
    const Genome parent(kTestGenomeSize, 0.0f);
    const auto layout = makeTestLayout();
    const MutationConfig config{
        .useBudget = true,
        .perturbationsPerOffspring = 10,
        .resetsPerOffspring = 5,
        .sigma = 0.2,
    };

    MutationStats stats;
    (void)mutate(parent, config, layout, rng, &stats);

    EXPECT_EQ(stats.perturbations, 10);
    EXPECT_EQ(stats.resets, 5);
    EXPECT_EQ(stats.totalChanges(), 15);
}

TEST_F(MutationTest, BudgetedMutationClampsToGenomeSize)
{
    const Genome parent(kTestGenomeSize, 0.0f);
    const auto layout = makeTestLayout();
    const MutationConfig config{
        .useBudget = true,
        .perturbationsPerOffspring = kTestGenomeSize,
        .resetsPerOffspring = kTestGenomeSize + 10,
        .sigma = 0.2,
    };

    MutationStats stats;
    (void)mutate(parent, config, layout, rng, &stats);

    EXPECT_EQ(stats.resets, kTestGenomeSize);
    EXPECT_EQ(stats.perturbations, 0);
    EXPECT_EQ(stats.totalChanges(), kTestGenomeSize);
}

TEST_F(MutationTest, LayerAwareFloorOfOnePerSegment)
{
    // Layout with 5 tiny segments (1 weight each) and 1 large segment.
    const GenomeLayout layout{
        .segments = {
            { "tiny_a", 1 },
            { "tiny_b", 1 },
            { "tiny_c", 1 },
            { "tiny_d", 1 },
            { "tiny_e", 1 },
            { "big", 95 },
        },
    };
    const Genome parent(100, 0.0f);
    const MutationConfig config{
        .useBudget = true,
        .perturbationsPerOffspring = 10,
        .resetsPerOffspring = 0,
        .sigma = 1.0,
    };

    // Run many trials and check each tiny segment gets touched at least sometimes.
    std::vector<int> segmentHitCounts(6, 0);
    constexpr int trials = 100;
    for (int t = 0; t < trials; ++t) {
        std::mt19937 trialRng(static_cast<uint32_t>(t));
        const Genome child = mutate(parent, config, layout, trialRng);

        int offset = 0;
        for (int s = 0; s < 6; ++s) {
            for (int i = 0; i < layout.segments[s].size; ++i) {
                if (child.weights[offset + i] != parent.weights[offset + i]) {
                    segmentHitCounts[s]++;
                    break;
                }
            }
            offset += layout.segments[s].size;
        }
    }

    // Each tiny segment should be hit in every trial (floor of 1 guarantees it).
    for (int s = 0; s < 5; ++s) {
        EXPECT_EQ(segmentHitCounts[s], trials)
            << "Segment " << layout.segments[s].name << " should be hit every trial";
    }
    // The big segment should also be hit every trial.
    EXPECT_EQ(segmentHitCounts[5], trials);
}

TEST_F(MutationTest, LayerAwareProportionalDistribution)
{
    // Layout: one 90-weight segment and one 10-weight segment.
    const GenomeLayout layout{
        .segments = {
            { "large", 90 },
            { "small", 10 },
        },
    };
    const Genome parent(100, 0.0f);
    const MutationConfig config{
        .useBudget = true,
        .perturbationsPerOffspring = 50,
        .resetsPerOffspring = 0,
        .sigma = 1.0,
    };

    // Run many trials and count mutations in each segment.
    int largeMutations = 0;
    int smallMutations = 0;
    constexpr int trials = 200;
    for (int t = 0; t < trials; ++t) {
        std::mt19937 trialRng(static_cast<uint32_t>(t));
        MutationStats stats;
        const Genome child = mutate(parent, config, layout, trialRng, &stats);

        for (int i = 0; i < 90; ++i) {
            if (child.weights[i] != parent.weights[i]) {
                largeMutations++;
            }
        }
        for (int i = 90; i < 100; ++i) {
            if (child.weights[i] != parent.weights[i]) {
                smallMutations++;
            }
        }
    }

    // Large segment should get substantially more mutations than small.
    // With 50 perturbations over 100 weights: floor gives 1 each, remainder 48 split ~43/5.
    // Large gets ~44, small gets ~6 per trial. Over 200 trials large >> small.
    EXPECT_GT(largeMutations, smallMutations * 3)
        << "Large segment should get proportionally more mutations";

    // Small segment should still get mutations (floor of 1 minimum).
    EXPECT_GT(smallMutations, 0) << "Small segment should still get mutations";
}
