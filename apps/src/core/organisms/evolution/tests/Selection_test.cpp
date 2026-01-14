#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/Selection.h"

#include <gtest/gtest.h>

using namespace DirtSim;

class SelectionTest : public ::testing::Test {
protected:
    std::mt19937 rng{ 42 };

    std::vector<Genome> createPopulation(int size)
    {
        std::vector<Genome> pop;
        for (int i = 0; i < size; i++) {
            pop.push_back(Genome::constant(static_cast<double>(i)));
        }
        return pop;
    }
};

TEST_F(SelectionTest, TournamentSelectReturnsElementFromPopulation)
{
    const auto population = createPopulation(10);
    const std::vector<double> fitness = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    const Genome selected = tournamentSelect(population, fitness, 3, rng);

    // Selected genome should match one in population.
    bool found = false;
    for (const auto& g : population) {
        if (g.weights == selected.weights) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(SelectionTest, TournamentSizeEqualsPopulationReturnsTheBest)
{
    const auto population = createPopulation(5);
    const std::vector<double> fitness = { 1, 5, 2, 4, 3 }; // Best is index 1.

    // With tournament size == population size, always picks best.
    const Genome selected = tournamentSelect(population, fitness, 5, rng);

    EXPECT_EQ(selected.weights, population[1].weights);
}

TEST_F(SelectionTest, ElitistReplaceKeepsTopGenomes)
{
    const auto parents = createPopulation(3);
    const std::vector<double> parentFitness = { 1.0, 2.0, 3.0 };

    std::vector<Genome> offspring;
    offspring.push_back(Genome::constant(10.0));
    offspring.push_back(Genome::constant(20.0));
    const std::vector<double> offspringFitness = { 5.0, 4.0 };

    const auto next = elitistReplace(parents, parentFitness, offspring, offspringFitness, 3);

    EXPECT_EQ(next.size(), 3u);

    // Top 3 by fitness: offspring[0]=5.0, offspring[1]=4.0, parents[2]=3.0.
    // First should be the one with fitness 5.0 (offspring[0], value 10.0).
    EXPECT_EQ(next[0].weights[0], 10.0);
}

TEST_F(SelectionTest, ElitistReplaceHandlesSmallPool)
{
    const auto parents = createPopulation(2);
    const std::vector<double> parentFitness = { 1.0, 2.0 };

    const std::vector<Genome> offspring; // Empty.
    const std::vector<double> offspringFitness;

    const auto next = elitistReplace(parents, parentFitness, offspring, offspringFitness, 5);

    // Can only return what we have.
    EXPECT_EQ(next.size(), 2u);
}

TEST_F(SelectionTest, ElitistReplaceSortsByFitnessDescending)
{
    std::vector<Genome> parents;
    parents.push_back(Genome::constant(1.0));
    parents.push_back(Genome::constant(2.0));
    parents.push_back(Genome::constant(3.0));
    const std::vector<double> parentFitness = { 10.0, 30.0, 20.0 };

    const std::vector<Genome> offspring;
    const std::vector<double> offspringFitness;

    const auto next = elitistReplace(parents, parentFitness, offspring, offspringFitness, 3);

    // Sorted by fitness: 30, 20, 10 -> values 2.0, 3.0, 1.0.
    EXPECT_EQ(next[0].weights[0], 2.0);
    EXPECT_EQ(next[1].weights[0], 3.0);
    EXPECT_EQ(next[2].weights[0], 1.0);
}
