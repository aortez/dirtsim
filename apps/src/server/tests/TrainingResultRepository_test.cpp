#include "core/UUID.h"
#include "server/TrainingResultRepository.h"
#include "server/api/TrainingResult.h"
#include <filesystem>
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server;

namespace {
Api::TrainingResult makeResult(GenomeId id, int candidateCount, double bestFitness)
{
    Api::TrainingResult result;
    result.summary.scenarioId = Scenario::EnumType::TreeGermination;
    result.summary.organismType = OrganismType::TREE;
    result.summary.populationSize = candidateCount;
    result.summary.maxGenerations = 10;
    result.summary.completedGenerations = 10;
    result.summary.bestFitness = bestFitness;
    result.summary.averageFitness = bestFitness * 0.5;
    result.summary.totalTrainingSeconds = 123.4;
    result.summary.primaryBrainKind = "TestBrain";
    result.summary.primaryBrainVariant = std::string("v1");
    result.summary.primaryPopulationCount = candidateCount;
    result.summary.trainingSessionId = id;

    result.candidates.clear();
    result.candidates.reserve(static_cast<size_t>(candidateCount));
    for (int i = 0; i < candidateCount; ++i) {
        Api::TrainingResult::Candidate candidate;
        candidate.id = UUID::generate();
        candidate.fitness = bestFitness - static_cast<double>(i);
        candidate.brainKind = "TestBrain";
        candidate.brainVariant = std::nullopt;
        candidate.generation = i;
        result.candidates.push_back(candidate);
    }

    return result;
}
} // namespace

class TrainingResultRepositoryTest : public ::testing::TestWithParam<bool> {
protected:
    void SetUp() override
    {
        if (isPersistent()) {
            testDataDir_ = std::filesystem::temp_directory_path()
                / ("dirtsim-test-training-results-" + UUID::generate().toShortString());
            std::filesystem::create_directories(testDataDir_);
            dbPath_ = testDataDir_ / "training_results.db";
            repository_ = std::make_unique<TrainingResultRepository>(dbPath_);
        }
        else {
            repository_ = std::make_unique<TrainingResultRepository>();
        }
    }

    void TearDown() override
    {
        repository_.reset();
        if (!testDataDir_.empty()) {
            std::filesystem::remove_all(testDataDir_);
        }
    }

    bool isPersistent() const { return GetParam(); }

    std::filesystem::path testDataDir_;
    std::filesystem::path dbPath_;
    std::unique_ptr<TrainingResultRepository> repository_;
};

TEST_P(TrainingResultRepositoryTest, StoreGetListRemove)
{
    const auto sessionId = UUID::generate();
    const auto result = makeResult(sessionId, 2, 1.25);

    auto storeResult = repository_->store(result);
    ASSERT_TRUE(storeResult.isValue());

    auto existsResult = repository_->exists(sessionId);
    ASSERT_TRUE(existsResult.isValue());
    EXPECT_TRUE(existsResult.value());

    auto getResult = repository_->get(sessionId);
    ASSERT_TRUE(getResult.isValue());
    ASSERT_TRUE(getResult.value().has_value());
    EXPECT_EQ(getResult.value()->summary.trainingSessionId, sessionId);
    EXPECT_EQ(getResult.value()->candidates.size(), 2U);

    auto listResult = repository_->list();
    ASSERT_TRUE(listResult.isValue());
    ASSERT_EQ(listResult.value().size(), 1U);
    EXPECT_EQ(listResult.value()[0].summary.trainingSessionId, sessionId);
    EXPECT_EQ(listResult.value()[0].candidateCount, 2);

    auto removeResult = repository_->remove(sessionId);
    ASSERT_TRUE(removeResult.isValue());
    EXPECT_TRUE(removeResult.value());

    auto missingExists = repository_->exists(sessionId);
    ASSERT_TRUE(missingExists.isValue());
    EXPECT_FALSE(missingExists.value());

    auto missingGet = repository_->get(sessionId);
    ASSERT_TRUE(missingGet.isValue());
    EXPECT_FALSE(missingGet.value().has_value());

    auto listAfterRemove = repository_->list();
    ASSERT_TRUE(listAfterRemove.isValue());
    EXPECT_TRUE(listAfterRemove.value().empty());
}

TEST_P(TrainingResultRepositoryTest, StoreOverwrite)
{
    const auto sessionId = UUID::generate();
    const auto initial = makeResult(sessionId, 1, 0.75);
    const auto updated = makeResult(sessionId, 3, 2.5);

    auto storeInitial = repository_->store(initial);
    ASSERT_TRUE(storeInitial.isValue());

    auto storeUpdated = repository_->store(updated);
    ASSERT_TRUE(storeUpdated.isValue());

    auto getResult = repository_->get(sessionId);
    ASSERT_TRUE(getResult.isValue());
    ASSERT_TRUE(getResult.value().has_value());
    EXPECT_EQ(getResult.value()->summary.bestFitness, 2.5);
    EXPECT_EQ(getResult.value()->candidates.size(), 3U);

    auto listResult = repository_->list();
    ASSERT_TRUE(listResult.isValue());
    ASSERT_EQ(listResult.value().size(), 1U);
    EXPECT_EQ(listResult.value()[0].candidateCount, 3);
}

INSTANTIATE_TEST_SUITE_P(
    TrainingResultRepositoryModes, TrainingResultRepositoryTest, ::testing::Values(false, true));
