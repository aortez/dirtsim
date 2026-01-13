#include "core/UUID.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/GenomeRepository.h"

#include <gtest/gtest.h>

using namespace DirtSim;

class GenomeRepositoryTest : public ::testing::Test {
protected:
    GenomeRepository repo;

    Genome createTestGenome(double value) { return Genome::constant(value); }

    GenomeMetadata createTestMetadata(const std::string& name, double fitness)
    {
        return GenomeMetadata{
            .name = name,
            .fitness = fitness,
            .generation = 1,
            .createdTimestamp = 1234567890,
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "",
        };
    }
};

TEST_F(GenomeRepositoryTest, StoreAndRetrieveGenome)
{
    auto genome = createTestGenome(0.5);
    auto meta = createTestMetadata("test_genome", 1.5);
    GenomeId id = UUID::generate();

    repo.store(id, genome, meta);

    EXPECT_TRUE(repo.exists(id));

    auto retrieved = repo.get(id);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->weights.size(), genome.weights.size());

    auto retrievedMeta = repo.getMetadata(id);
    ASSERT_TRUE(retrievedMeta.has_value());
    EXPECT_EQ(retrievedMeta->name, "test_genome");
    EXPECT_DOUBLE_EQ(retrievedMeta->fitness, 1.5);
}

TEST_F(GenomeRepositoryTest, GetNonexistentReturnsNullopt)
{
    GenomeId bogusId = UUID::generate(); // Not stored.

    EXPECT_FALSE(repo.exists(bogusId));
    EXPECT_FALSE(repo.get(bogusId).has_value());
    EXPECT_FALSE(repo.getMetadata(bogusId).has_value());
}

TEST_F(GenomeRepositoryTest, ListReturnsAllStoredGenomes)
{
    repo.store(UUID::generate(), createTestGenome(0.1), createTestMetadata("genome_a", 1.0));
    repo.store(UUID::generate(), createTestGenome(0.2), createTestMetadata("genome_b", 2.0));
    repo.store(UUID::generate(), createTestGenome(0.3), createTestMetadata("genome_c", 3.0));

    auto list = repo.list();

    EXPECT_EQ(list.size(), 3u);
}

TEST_F(GenomeRepositoryTest, RemoveDeletesGenome)
{
    GenomeId id = UUID::generate();
    repo.store(id, createTestGenome(0.5), createTestMetadata("doomed", 1.0));

    EXPECT_TRUE(repo.get(id).has_value());
    EXPECT_EQ(repo.count(), 1u);

    repo.remove(id);

    EXPECT_FALSE(repo.get(id).has_value());
    EXPECT_FALSE(repo.getMetadata(id).has_value());
    EXPECT_EQ(repo.count(), 0u);
}

TEST_F(GenomeRepositoryTest, ClearRemovesAllGenomes)
{
    repo.store(UUID::generate(), createTestGenome(0.1), createTestMetadata("a", 1.0));
    repo.store(UUID::generate(), createTestGenome(0.2), createTestMetadata("b", 2.0));

    EXPECT_EQ(repo.count(), 2u);
    EXPECT_FALSE(repo.empty());

    repo.clear();

    EXPECT_EQ(repo.count(), 0u);
    EXPECT_TRUE(repo.empty());
}

TEST_F(GenomeRepositoryTest, BestTrackingWorks)
{
    // Store two genomes, only use id2.
    GenomeId id1 = UUID::generate();
    GenomeId id2 = UUID::generate();
    repo.store(id1, createTestGenome(0.1), createTestMetadata("mediocre", 1.0));
    repo.store(id2, createTestGenome(0.2), createTestMetadata("champion", 5.0));

    // Initially no best.
    EXPECT_FALSE(repo.getBestId().has_value());
    EXPECT_FALSE(repo.getBest().has_value());

    // Mark id2 as best.
    repo.markAsBest(id2);

    ASSERT_TRUE(repo.getBestId().has_value());
    EXPECT_EQ(*repo.getBestId(), id2);
    EXPECT_TRUE(repo.getBest().has_value());
}

TEST_F(GenomeRepositoryTest, RemovingBestClearsBestId)
{
    GenomeId id = UUID::generate();
    repo.store(id, createTestGenome(0.5), createTestMetadata("champ", 5.0));
    repo.markAsBest(id);

    EXPECT_TRUE(repo.getBestId().has_value());

    repo.remove(id);

    EXPECT_FALSE(repo.getBestId().has_value());
}

TEST_F(GenomeRepositoryTest, ClearAlsoClearsBestId)
{
    GenomeId id = UUID::generate();
    repo.store(id, createTestGenome(0.5), createTestMetadata("champ", 5.0));
    repo.markAsBest(id);

    repo.clear();

    EXPECT_FALSE(repo.getBestId().has_value());
}

TEST_F(GenomeRepositoryTest, MarkAsBestWithInvalidIdDoesNothing)
{
    GenomeId bogusId = UUID::generate(); // Not stored.

    repo.markAsBest(bogusId);

    EXPECT_FALSE(repo.getBestId().has_value());
}

TEST_F(GenomeRepositoryTest, StoreOverwritesExistingGenome)
{
    GenomeId id = UUID::generate();
    repo.store(id, createTestGenome(0.1), createTestMetadata("original", 1.0));
    repo.store(id, createTestGenome(0.9), createTestMetadata("updated", 9.0));

    EXPECT_EQ(repo.count(), 1u);

    auto meta = repo.getMetadata(id);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->name, "updated");
    EXPECT_DOUBLE_EQ(meta->fitness, 9.0);
}
