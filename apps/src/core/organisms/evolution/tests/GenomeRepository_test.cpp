#include "core/UUID.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/GenomeRepository.h"

#include <atomic>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

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
            .robustFitness = fitness,
            .robustEvalCount = 1,
            .robustFitnessSamples = { fitness },
            .generation = 1,
            .createdTimestamp = 1234567890,
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "",
            .organismType = std::nullopt,
            .brainKind = std::nullopt,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        };
    }

    GenomeMetadata createManagedMetadata(const std::string& name, double fitness)
    {
        auto meta = createTestMetadata(name, fitness);
        meta.trainingSessionId = UUID::generate();
        return meta;
    }

    GenomeMetadata createManagedMetadataForBucket(
        const std::string& name,
        double fitness,
        OrganismType organismType,
        const std::string& brainKind)
    {
        auto meta = createManagedMetadata(name, fitness);
        meta.organismType = organismType;
        meta.brainKind = brainKind;
        return meta;
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

TEST_F(GenomeRepositoryTest, StoreOrUpdateByHashReusesExistingGenomeId)
{
    const auto genome = createTestGenome(0.42);
    const auto initial = createManagedMetadata("initial", 1.0);
    const auto updated = createManagedMetadata("updated", 9.0);

    const auto first = repo.storeOrUpdateByHash(genome, initial);
    const auto second = repo.storeOrUpdateByHash(genome, updated);

    EXPECT_EQ(repo.count(), 1u);
    EXPECT_TRUE(first.inserted);
    EXPECT_FALSE(first.deduplicated);
    EXPECT_FALSE(second.inserted);
    EXPECT_TRUE(second.deduplicated);
    EXPECT_EQ(first.id, second.id);

    const auto metadata = repo.getMetadata(first.id);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->name, "updated");
    EXPECT_DOUBLE_EQ(metadata->fitness, 9.0);
    EXPECT_EQ(metadata->robustEvalCount, 2);
    EXPECT_EQ(metadata->robustFitnessSamples.size(), 2u);
}

TEST_F(GenomeRepositoryTest, StoreOrUpdateByHashKeepsPeakFitnessAndTracksRobustFitness)
{
    const auto genome = createTestGenome(0.77);
    auto highOutlier = createManagedMetadata("high", 9999.0);
    highOutlier.robustFitness = 9999.0;
    highOutlier.robustEvalCount = 1;
    highOutlier.robustFitnessSamples = { 9999.0 };

    auto typical = createManagedMetadata("typical", 10.0);
    typical.robustFitness = 10.0;
    typical.robustEvalCount = 1;
    typical.robustFitnessSamples = { 10.0 };

    const auto first = repo.storeOrUpdateByHash(genome, highOutlier);
    const auto second = repo.storeOrUpdateByHash(genome, typical);

    ASSERT_EQ(first.id, second.id);
    const auto metadata = repo.getMetadata(first.id);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_DOUBLE_EQ(metadata->fitness, 9999.0);
    EXPECT_EQ(metadata->robustEvalCount, 2);
    EXPECT_DOUBLE_EQ(metadata->robustFitness, 5004.5);
    ASSERT_EQ(metadata->robustFitnessSamples.size(), 2u);
    EXPECT_DOUBLE_EQ(metadata->robustFitnessSamples[0], 9999.0);
    EXPECT_DOUBLE_EQ(metadata->robustFitnessSamples[1], 10.0);
}

TEST_F(GenomeRepositoryTest, PruneManagedByFitnessKeepsBestId)
{
    const GenomeId idLow = UUID::generate();
    const GenomeId idMidA = UUID::generate();
    const GenomeId idMidB = UUID::generate();
    const GenomeId idHigh = UUID::generate();

    repo.store(idLow, createTestGenome(0.1), createManagedMetadata("low", 1.0));
    repo.store(idMidA, createTestGenome(0.2), createManagedMetadata("mid_a", 2.0));
    repo.store(idMidB, createTestGenome(0.3), createManagedMetadata("mid_b", 3.0));
    repo.store(idHigh, createTestGenome(0.4), createManagedMetadata("high", 4.0));
    repo.markAsBest(idLow);

    const size_t removed = repo.pruneManagedByFitness(2);
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(repo.count(), 2u);
    EXPECT_TRUE(repo.exists(idLow));
    EXPECT_TRUE(repo.exists(idHigh));
    EXPECT_FALSE(repo.exists(idMidA));
    EXPECT_FALSE(repo.exists(idMidB));
}

TEST_F(GenomeRepositoryTest, PruneManagedByFitnessAppliesPerOrganismBrainBucket)
{
    const GenomeId treeLow = UUID::generate();
    const GenomeId treeHigh = UUID::generate();
    const GenomeId duckLow = UUID::generate();
    const GenomeId duckHigh = UUID::generate();

    repo.store(
        treeLow,
        createTestGenome(0.1),
        createManagedMetadataForBucket("tree_low", 1.0, OrganismType::TREE, "NeuralNet"));
    repo.store(
        treeHigh,
        createTestGenome(0.2),
        createManagedMetadataForBucket("tree_high", 9.0, OrganismType::TREE, "NeuralNet"));
    repo.store(
        duckLow,
        createTestGenome(0.3),
        createManagedMetadataForBucket("duck_low", 2.0, OrganismType::DUCK, "NeuralNet"));
    repo.store(
        duckHigh,
        createTestGenome(0.4),
        createManagedMetadataForBucket("duck_high", 8.0, OrganismType::DUCK, "NeuralNet"));

    const size_t removed = repo.pruneManagedByFitness(1);
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(repo.count(), 2u);
    EXPECT_FALSE(repo.exists(treeLow));
    EXPECT_TRUE(repo.exists(treeHigh));
    EXPECT_FALSE(repo.exists(duckLow));
    EXPECT_TRUE(repo.exists(duckHigh));
}

TEST_F(GenomeRepositoryTest, ConcurrentStoreAndReadIsThreadSafe)
{
    constexpr int threadCount = 4;
    constexpr int genomesPerThread = 50;
    const size_t expectedCount = static_cast<size_t>(threadCount * genomesPerThread);

    std::vector<std::vector<GenomeId>> threadIds(threadCount);
    std::vector<std::thread> writers;
    writers.reserve(threadCount);
    std::atomic<int> completed{ 0 };
    std::atomic<bool> start{ false };

    for (int t = 0; t < threadCount; ++t) {
        threadIds[t].reserve(genomesPerThread);
        writers.emplace_back([this, t, &threadIds, &completed, &start]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < genomesPerThread; ++i) {
                GenomeId id = UUID::generate();
                threadIds[t].push_back(id);
                const double value = static_cast<double>((t + 1) * (i + 1)) * 0.01;
                repo.store(
                    id,
                    createTestGenome(value),
                    createTestMetadata(
                        "gen_" + std::to_string(t) + "_" + std::to_string(i), value));
                repo.exists(id);
                repo.getMetadata(id);
            }
            if (!threadIds[t].empty()) {
                repo.markAsBest(threadIds[t].back());
            }
            completed.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread reader([this, &completed, threadCount]() {
        while (completed.load(std::memory_order_acquire) < threadCount) {
            repo.count();
            repo.list();
            repo.getBestId();
            std::this_thread::yield();
        }
    });

    start.store(true, std::memory_order_release);

    for (auto& writer : writers) {
        writer.join();
    }
    reader.join();

    EXPECT_EQ(repo.count(), expectedCount);

    std::unordered_set<GenomeId> allIds;
    allIds.reserve(expectedCount);
    for (const auto& ids : threadIds) {
        for (const auto& id : ids) {
            allIds.insert(id);
        }
    }

    EXPECT_EQ(allIds.size(), expectedCount);

    for (const auto& id : allIds) {
        EXPECT_TRUE(repo.exists(id));
        EXPECT_TRUE(repo.get(id).has_value());
    }

    auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value());
    EXPECT_TRUE(allIds.find(*bestId) != allIds.end());
    EXPECT_TRUE(repo.getBest().has_value());
}

// ============================================================================
// Persistence Tests - verify SQLite write-through behavior.
// ============================================================================

class GenomeRepositoryPersistenceTest : public ::testing::Test {
protected:
    std::filesystem::path dbPath_;

    void SetUp() override
    {
        // Create a unique temp file for each test.
        dbPath_ = std::filesystem::temp_directory_path()
            / ("genome_test_" + UUID::generate().toString() + ".db");
    }

    void TearDown() override
    {
        // Clean up the test database.
        std::filesystem::remove(dbPath_);
    }

    Genome createTestGenome(float value) { return Genome::constant(value); }

    GenomeMetadata createTestMetadata(const std::string& name, double fitness)
    {
        return GenomeMetadata{
            .name = name,
            .fitness = fitness,
            .robustFitness = fitness,
            .robustEvalCount = 1,
            .robustFitnessSamples = { fitness },
            .generation = 42,
            .createdTimestamp = 1234567890,
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "test notes",
            .organismType = std::nullopt,
            .brainKind = std::nullopt,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        };
    }
};

TEST_F(GenomeRepositoryPersistenceTest, IsPersistentReturnsTrueWhenPathProvided)
{
    GenomeRepository repo(dbPath_);
    EXPECT_TRUE(repo.isPersistent());
}

TEST_F(GenomeRepositoryPersistenceTest, IsPersistentReturnsFalseForInMemory)
{
    GenomeRepository repo;
    EXPECT_FALSE(repo.isPersistent());
}

TEST_F(GenomeRepositoryPersistenceTest, GenomePersistsAcrossReopen)
{
    GenomeId id = UUID::generate();
    auto genome = createTestGenome(0.42f);
    auto meta = createTestMetadata("persistent_genome", 3.14);

    // Store in first instance.
    {
        GenomeRepository repo(dbPath_);
        repo.store(id, genome, meta);
        EXPECT_EQ(repo.count(), 1u);
    }

    // Reopen and verify data persisted.
    {
        GenomeRepository repo(dbPath_);
        EXPECT_EQ(repo.count(), 1u);
        EXPECT_TRUE(repo.exists(id));

        auto retrieved = repo.get(id);
        ASSERT_TRUE(retrieved.has_value());
        EXPECT_EQ(retrieved->weights.size(), genome.weights.size());
        // Check first few weights match.
        for (size_t i = 0; i < 10 && i < retrieved->weights.size(); i++) {
            EXPECT_FLOAT_EQ(retrieved->weights[i], genome.weights[i]);
        }

        auto retrievedMeta = repo.getMetadata(id);
        ASSERT_TRUE(retrievedMeta.has_value());
        EXPECT_EQ(retrievedMeta->name, "persistent_genome");
        EXPECT_DOUBLE_EQ(retrievedMeta->fitness, 3.14);
        EXPECT_EQ(retrievedMeta->generation, 42);
        EXPECT_EQ(retrievedMeta->notes, "test notes");
    }
}

TEST_F(GenomeRepositoryPersistenceTest, BestIdPersistsAcrossReopen)
{
    GenomeId id1 = UUID::generate();
    GenomeId id2 = UUID::generate();

    // Store genomes and mark one as best.
    {
        GenomeRepository repo(dbPath_);
        repo.store(id1, createTestGenome(0.1f), createTestMetadata("first", 1.0));
        repo.store(id2, createTestGenome(0.2f), createTestMetadata("second", 2.0));
        repo.markAsBest(id2);
        EXPECT_EQ(*repo.getBestId(), id2);
    }

    // Reopen and verify best ID persisted.
    {
        GenomeRepository repo(dbPath_);
        ASSERT_TRUE(repo.getBestId().has_value());
        EXPECT_EQ(*repo.getBestId(), id2);
    }
}

TEST_F(GenomeRepositoryPersistenceTest, RemovePersistsAcrossReopen)
{
    GenomeId id1 = UUID::generate();
    GenomeId id2 = UUID::generate();

    // Store two genomes, remove one.
    {
        GenomeRepository repo(dbPath_);
        repo.store(id1, createTestGenome(0.1f), createTestMetadata("keep", 1.0));
        repo.store(id2, createTestGenome(0.2f), createTestMetadata("remove", 2.0));
        repo.remove(id2);
        EXPECT_EQ(repo.count(), 1u);
    }

    // Reopen and verify removal persisted.
    {
        GenomeRepository repo(dbPath_);
        EXPECT_EQ(repo.count(), 1u);
        EXPECT_TRUE(repo.exists(id1));
        EXPECT_FALSE(repo.exists(id2));
    }
}

TEST_F(GenomeRepositoryPersistenceTest, ClearPersistsAcrossReopen)
{
    // Store some genomes then clear.
    {
        GenomeRepository repo(dbPath_);
        repo.store(UUID::generate(), createTestGenome(0.1f), createTestMetadata("a", 1.0));
        repo.store(UUID::generate(), createTestGenome(0.2f), createTestMetadata("b", 2.0));
        repo.clear();
        EXPECT_EQ(repo.count(), 0u);
    }

    // Reopen and verify clear persisted.
    {
        GenomeRepository repo(dbPath_);
        EXPECT_EQ(repo.count(), 0u);
        EXPECT_TRUE(repo.empty());
    }
}

TEST_F(GenomeRepositoryPersistenceTest, MultipleGenomesPersist)
{
    std::vector<GenomeId> ids;
    for (int i = 0; i < 5; i++) {
        ids.push_back(UUID::generate());
    }

    // Store multiple genomes.
    {
        GenomeRepository repo(dbPath_);
        for (int i = 0; i < 5; i++) {
            repo.store(
                ids[i],
                createTestGenome(static_cast<float>(i) * 0.1f),
                createTestMetadata("genome_" + std::to_string(i), static_cast<double>(i)));
        }
        EXPECT_EQ(repo.count(), 5u);
    }

    // Reopen and verify all persisted.
    {
        GenomeRepository repo(dbPath_);
        EXPECT_EQ(repo.count(), 5u);
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(repo.exists(ids[i]));
            auto meta = repo.getMetadata(ids[i]);
            ASSERT_TRUE(meta.has_value());
            EXPECT_EQ(meta->name, "genome_" + std::to_string(i));
        }
    }
}

TEST_F(GenomeRepositoryPersistenceTest, OverwritePersistsAcrossReopen)
{
    GenomeId id = UUID::generate();

    // Store then overwrite.
    {
        GenomeRepository repo(dbPath_);
        repo.store(id, createTestGenome(0.1f), createTestMetadata("original", 1.0));
        repo.store(id, createTestGenome(0.9f), createTestMetadata("updated", 9.0));
        EXPECT_EQ(repo.count(), 1u);
    }

    // Reopen and verify overwrite persisted.
    {
        GenomeRepository repo(dbPath_);
        EXPECT_EQ(repo.count(), 1u);

        auto meta = repo.getMetadata(id);
        ASSERT_TRUE(meta.has_value());
        EXPECT_EQ(meta->name, "updated");
        EXPECT_DOUBLE_EQ(meta->fitness, 9.0);
    }
}
