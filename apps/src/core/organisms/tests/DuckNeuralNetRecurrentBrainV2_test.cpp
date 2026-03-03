#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"

#include "DuckTestUtils.h"
#include "core/LoggingChannels.h"
#include "core/organisms/Duck.h"
#include "core/organisms/OrganismManager.h"

#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>

using namespace DirtSim;
using namespace DirtSim::Test;

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

TEST(DuckNeuralNetRecurrentBrainV2Test, RandomGenomesProduceCommandDiversity)
{
    // Suppress noisy per-frame duck logging.
    LoggingChannels::get(LogChannel::Brain)->set_level(spdlog::level::warn);

    constexpr int NUM_BRAINS = 10;
    constexpr double SIM_SECONDS = 10.0;
    constexpr double DT = 0.016;
    constexpr int STEPS = static_cast<int>(SIM_SECONDS / DT);

    const auto seed = std::random_device{}();
    std::cerr << "Seed: " << seed << "\n\n";
    std::mt19937 rng(seed);

    // Track command counts per brain for cross-brain analysis.
    std::set<std::string> all_signatures;
    std::vector<std::map<std::string, int>> brain_commands(NUM_BRAINS);

    for (int i = 0; i < NUM_BRAINS; ++i) {
        const Genome genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng);
        auto brain = std::make_unique<DuckNeuralNetRecurrentBrainV2>(genome);

        auto world = createFlatWorld(32, 20);
        OrganismManager& manager = world->getOrganismManager();
        // Spawn on the floor (height-2).
        const OrganismId id = manager.createDuck(*world, 16, 18, std::move(brain));
        Duck* duck = manager.getDuck(id);
        ASSERT_NE(duck, nullptr);

        for (int step = 0; step < STEPS; ++step) {
            world->advanceTime(DT);
        }

        const auto commands = duck->getTopCommandSignatures(20);
        int total = 0;
        for (const auto& [sig, count] : commands) {
            total += count;
            brain_commands[i][sig] = count;
            all_signatures.insert(sig);
        }

        std::cerr << "Brain " << i << ":\n";
        for (const auto& [sig, count] : commands) {
            const double pct = 100.0 * count / total;
            std::cerr << "  " << sig << ": " << count << " (" << std::fixed << std::setprecision(1)
                      << pct << "%)\n";
        }
        std::cerr << "  Total: " << total << "\n";
        std::cerr << "  Unique commands: " << commands.size() << "\n\n";
    }

    // Cross-brain spread analysis: show how each command varies across brains.
    std::cerr << "=== Command spread across " << NUM_BRAINS << " brains ===\n";
    std::cerr << std::left << std::setw(30) << "Command" << std::right << std::setw(8) << "Min%"
              << std::setw(8) << "Max%" << std::setw(8) << "Avg%" << std::setw(8) << "Brains"
              << "\n";
    std::cerr << std::string(62, '-') << "\n";

    for (const auto& sig : all_signatures) {
        double min_pct = 100.0;
        double max_pct = 0.0;
        double sum_pct = 0.0;
        int brains_with_cmd = 0;

        for (int i = 0; i < NUM_BRAINS; ++i) {
            // Compute this brain's total.
            int total = 0;
            for (const auto& [s, c] : brain_commands[i]) {
                total += c;
            }
            if (total == 0) continue;

            auto it = brain_commands[i].find(sig);
            const double pct = (it != brain_commands[i].end()) ? 100.0 * it->second / total : 0.0;

            min_pct = std::min(min_pct, pct);
            max_pct = std::max(max_pct, pct);
            sum_pct += pct;
            if (pct > 0.0) ++brains_with_cmd;
        }

        const double avg_pct = sum_pct / NUM_BRAINS;
        std::cerr << std::left << std::setw(30) << sig << std::right << std::fixed
                  << std::setprecision(1) << std::setw(8) << min_pct << std::setw(8) << max_pct
                  << std::setw(8) << avg_pct << std::setw(8) << brains_with_cmd << "/" << NUM_BRAINS
                  << "\n";
    }
    std::cerr << "\n";

    // Restore logging.
    LoggingChannels::get(LogChannel::Brain)->set_level(spdlog::level::info);
}
