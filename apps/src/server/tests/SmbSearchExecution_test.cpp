#include "core/UUID.h"
#include "core/scenarios/tests/NesTestRomPath.h"
#include "server/PlanRepository.h"
#include "server/search/SmbPlanExecution.h"
#include "server/search/SmbSearchExecution.h"

#include <filesystem>
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::SearchSupport;

namespace {

void requireSmbRomOrSkip()
{
    if (!DirtSim::Test::resolveSmbRomPath().has_value()) {
        GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is required.";
    }
}

SmbSearchExecutionParams makeParams(
    uint32_t beamWidth, uint32_t segmentFrameBudget, uint32_t maxSegments)
{
    return SmbSearchExecutionParams{
        .beamWidth = beamWidth,
        .maxSegments = maxSegments,
        .segmentFrameBudget = segmentFrameBudget,
    };
}

struct ScopedTempDir {
    ScopedTempDir()
    {
        path = std::filesystem::temp_directory_path()
            / ("dirtsim-test-search-plan-" + UUID::generate().toShortString());
        std::filesystem::create_directories(path);
    }

    ~ScopedTempDir()
    {
        if (!path.empty()) {
            std::filesystem::remove_all(path);
        }
    }

    std::filesystem::path path;
};

Result<Api::Plan, std::string> runSearchUntilPlanAvailable(const SmbSearchExecutionParams& params)
{
    SmbSearchExecution execution(params);
    const auto startResult = execution.start();
    if (startResult.isError()) {
        return Result<Api::Plan, std::string>::error(startResult.errorValue());
    }

    for (int tickIndex = 0; tickIndex < 512 && !execution.isCompleted(); ++tickIndex) {
        const auto tickResult = execution.tick();
        if (tickResult.error.has_value()) {
            return Result<Api::Plan, std::string>::error(tickResult.error.value());
        }
        if (execution.hasPersistablePlan()) {
            execution.stop();
        }
    }

    if (!execution.hasPersistablePlan()) {
        return Result<Api::Plan, std::string>::error(
            "SMB search did not produce a persistable plan");
    }

    for (int tickIndex = 0; tickIndex < 8 && !execution.isCompleted(); ++tickIndex) {
        const auto tickResult = execution.tick();
        if (tickResult.error.has_value()) {
            return Result<Api::Plan, std::string>::error(tickResult.error.value());
        }
    }

    return Result<Api::Plan, std::string>::okay(execution.getPlan());
}

Result<std::monostate, std::string> runPlanPlaybackToCompletion(
    const Api::Plan& plan, Api::SearchProgress& finalProgress)
{
    SmbPlanExecution playback;
    const auto startResult = playback.startPlayback(plan);
    if (startResult.isError()) {
        return Result<std::monostate, std::string>::error(startResult.errorValue());
    }

    const int maxTickCount = static_cast<int>(plan.frames.size()) + 512;
    for (int tickIndex = 0; tickIndex < maxTickCount && !playback.isCompleted(); ++tickIndex) {
        const auto tickResult = playback.tick();
        if (tickResult.error.has_value()) {
            return Result<std::monostate, std::string>::error(tickResult.error.value());
        }
    }

    if (!playback.isCompleted()) {
        return Result<std::monostate, std::string>::error(
            "SMB plan playback did not complete within the expected tick budget");
    }

    finalProgress = playback.getProgress();
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

} // namespace

TEST(SmbSearchExecutionTest, RunsToCompletionAndProducesSearchPlan)
{
    requireSmbRomOrSkip();

    SmbSearchExecution execution(makeParams(1u, 12u, 2u));
    const auto startResult = execution.start();
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    EXPECT_TRUE(execution.hasRenderableFrame());
    EXPECT_EQ(execution.getProgress().beamWidth, 1u);
    EXPECT_EQ(execution.getProgress().maxSegments, 2u);
    EXPECT_EQ(execution.getProgress().maxSteps, 12u);
    EXPECT_EQ(execution.getProgress().segmentIndex, 1u);

    for (int tickIndex = 0; tickIndex < 256 && !execution.isCompleted(); ++tickIndex) {
        const auto tickResult = execution.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value_or("");
    }

    EXPECT_TRUE(execution.isCompleted());
    EXPECT_TRUE(execution.hasPersistablePlan());
    EXPECT_FALSE(execution.getPlan().frames.empty());
    EXPECT_GT(execution.getProgress().bestFrontier, 0u);
    EXPECT_GT(execution.getProgress().expandedNodeCount, 0u);
    EXPECT_EQ(execution.getPlan().summary.elapsedFrames, execution.getPlan().frames.size());
}

TEST(SmbSearchExecutionTest, StopFinalizesBestSoFarPlan)
{
    requireSmbRomOrSkip();

    SmbSearchExecution execution(makeParams(1u, 12u, 4u));
    const auto startResult = execution.start();
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    bool reachedPromotableProgress = false;
    for (int tickIndex = 0; tickIndex < 128 && !execution.isCompleted(); ++tickIndex) {
        const auto tickResult = execution.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value_or("");
        if (execution.hasPersistablePlan()) {
            reachedPromotableProgress = true;
            break;
        }
    }
    ASSERT_TRUE(reachedPromotableProgress);

    execution.stop();
    for (int tickIndex = 0; tickIndex < 8 && !execution.isCompleted(); ++tickIndex) {
        const auto tickResult = execution.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value_or("");
    }

    EXPECT_TRUE(execution.isCompleted());
    EXPECT_TRUE(execution.hasPersistablePlan());
    EXPECT_FALSE(execution.getPlan().frames.empty());
    EXPECT_GT(execution.getPlan().summary.bestFrontier, 0u);
}

TEST(SmbSearchExecutionTest, ZeroMaxSegmentsAllowsSearchBeyondLegacyCap)
{
    requireSmbRomOrSkip();

    SmbSearchExecution execution(makeParams(1u, 3u, 0u));
    const auto startResult = execution.start();
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    EXPECT_FALSE(execution.isCompleted());

    bool progressedBeyondFourSegments = false;
    for (int tickIndex = 0; tickIndex < 256 && !execution.isCompleted(); ++tickIndex) {
        const auto tickResult = execution.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value_or("");
        if (execution.getProgress().segmentIndex > 4u) {
            progressedBeyondFourSegments = true;
            break;
        }
    }

    ASSERT_TRUE(progressedBeyondFourSegments);
    EXPECT_FALSE(execution.isCompleted());
    EXPECT_EQ(execution.getProgress().maxSegments, 0u);
}

TEST(SmbSearchExecutionTest, PersistedSearchPlanPlaybackReachesSavedFrontier)
{
    requireSmbRomOrSkip();

    const auto searchPlanResult = runSearchUntilPlanAvailable(makeParams(2u, 14u, 32u));
    ASSERT_FALSE(searchPlanResult.isError()) << searchPlanResult.errorValue();
    ASSERT_TRUE(searchPlanResult.value().smbPlaybackRoot.has_value());
    ASSERT_FALSE(searchPlanResult.value().smbPlaybackRoot->savestateBytes.empty());

    ScopedTempDir tempDir;
    const std::filesystem::path dbPath = tempDir.path / "plans.db";
    PlanRepository repository(dbPath);

    const auto storeResult = repository.store(searchPlanResult.value());
    ASSERT_FALSE(storeResult.isError()) << storeResult.errorValue();

    const auto getResult = repository.get(searchPlanResult.value().summary.id);
    ASSERT_FALSE(getResult.isError()) << getResult.errorValue();
    ASSERT_TRUE(getResult.value().has_value());
    ASSERT_TRUE(getResult.value()->smbPlaybackRoot.has_value());
    EXPECT_EQ(
        getResult.value()->smbPlaybackRoot->savestateBytes,
        searchPlanResult.value().smbPlaybackRoot->savestateBytes);

    Api::SearchProgress playbackProgress{};
    const auto playbackResult =
        runPlanPlaybackToCompletion(getResult.value().value(), playbackProgress);
    ASSERT_FALSE(playbackResult.isError()) << playbackResult.errorValue();

    EXPECT_EQ(playbackProgress.bestFrontier, getResult.value()->summary.bestFrontier);
    EXPECT_EQ(playbackProgress.elapsedFrames, getResult.value()->summary.elapsedFrames);
}
