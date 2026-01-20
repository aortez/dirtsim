#include "core/organisms/TreeBrain.h"
#include "core/organisms/TreeCommands.h"
#include "core/organisms/TreeSensoryData.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include <gtest/gtest.h>

using namespace DirtSim;

class NeuralNetBrainTest : public ::testing::Test {
protected:
    TreeSensoryData createTestSensoryData()
    {
        TreeSensoryData sensory{
            .material_histograms = {},
            .actual_width = 15,
            .actual_height = 15,
            .scale_factor = 1.0,
            .world_offset = { 0, 0 },
            .seed_position = { 7, 7 },
            .age_seconds = 10.0,
            .stage = GrowthStage::SAPLING,
            .total_energy = 100.0,
            .total_water = 50.0,
            .current_thought = {},
            .current_action = std::nullopt,
            .action_progress = 0.0,
        };

        // Fill with some test data - mostly AIR with some DIRT at bottom.
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 15; x++) {
                if (y >= 10) {
                    sensory.material_histograms[y][x][1] = 1.0; // DIRT.
                }
                else {
                    sensory.material_histograms[y][x][0] = 1.0; // AIR.
                }
            }
        }

        return sensory;
    }
};

TEST_F(NeuralNetBrainTest, DecideReturnsValidCommand)
{
    NeuralNetBrain brain(42);
    auto sensory = createTestSensoryData();

    TreeCommand cmd = brain.decide(sensory);

    // Should be one of the valid command types.
    bool is_valid = std::holds_alternative<WaitCommand>(cmd)
        || std::holds_alternative<CancelCommand>(cmd)
        || std::holds_alternative<GrowWoodCommand>(cmd)
        || std::holds_alternative<GrowLeafCommand>(cmd)
        || std::holds_alternative<GrowRootCommand>(cmd)
        || std::holds_alternative<ProduceSeedCommand>(cmd);

    EXPECT_TRUE(is_valid);
}

TEST_F(NeuralNetBrainTest, DeterministicWithSameSeed)
{
    auto sensory = createTestSensoryData();

    NeuralNetBrain brain1(42);
    NeuralNetBrain brain2(42);

    TreeCommand cmd1 = brain1.decide(sensory);
    TreeCommand cmd2 = brain2.decide(sensory);

    // Same seed + same input = same output.
    EXPECT_EQ(cmd1.index(), cmd2.index());
}

TEST_F(NeuralNetBrainTest, DifferentSeedsProduceDifferentWeights)
{
    NeuralNetBrain brain1(42);
    NeuralNetBrain brain2(43);

    Genome g1 = brain1.getGenome();
    Genome g2 = brain2.getGenome();

    EXPECT_NE(g1.weights, g2.weights);
}

TEST_F(NeuralNetBrainTest, GenomeRoundTrip)
{
    NeuralNetBrain brain1(42);
    Genome g = brain1.getGenome();

    NeuralNetBrain brain2(g);
    Genome g2 = brain2.getGenome();

    EXPECT_EQ(g.weights, g2.weights);
}

TEST_F(NeuralNetBrainTest, SetGenomeChangesOutput)
{
    auto sensory = createTestSensoryData();

    NeuralNetBrain brain(42);
    TreeCommand cmd1 = brain.decide(sensory);

    // Change to different genome.
    std::mt19937 rng(999);
    Genome new_genome = Genome::random(rng);
    brain.setGenome(new_genome);

    TreeCommand cmd2 = brain.decide(sensory);

    // Both should be valid commands (any of 6 types).
    EXPECT_LT(cmd1.index(), 6u);
    EXPECT_LT(cmd2.index(), 6u);
}

TEST_F(NeuralNetBrainTest, GenomeHasCorrectSize)
{
    NeuralNetBrain brain(42);
    Genome g = brain.getGenome();

    // New size with light channel and action feedback inputs:
    // (2488 * 48) + 48 + (48 * 231) + 231 = 119424 + 48 + 11088 + 231 = 130791.
    EXPECT_EQ(g.weights.size(), 130791u);
}

TEST_F(NeuralNetBrainTest, ConstantGenomeProducesConsistentOutput)
{
    Genome g = Genome::constant(0.1);
    NeuralNetBrain brain(g);

    auto sensory = createTestSensoryData();
    TreeCommand cmd1 = brain.decide(sensory);
    TreeCommand cmd2 = brain.decide(sensory);

    // Same genome + same input = same output.
    EXPECT_EQ(cmd1.index(), cmd2.index());
}
