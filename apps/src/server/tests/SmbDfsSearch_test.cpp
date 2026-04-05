#include "core/scenarios/tests/NesTestRomPath.h"
#include "server/search/SmbDfsSearch.h"
#include "server/search/SmbPlanExecution.h"
#include "server/search/SmbSearchHarness.h"

#include <gtest/gtest.h>

using namespace DirtSim::Server::SearchSupport;

namespace {

void requireSmbRomOrSkip()
{
    if (!DirtSim::Test::resolveSmbRomPath().has_value()) {
        GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is required.";
    }
}

void expectFrameEq(
    const DirtSim::PlayerControlFrame& actual, const DirtSim::PlayerControlFrame& expected)
{
    EXPECT_EQ(actual.xAxis, expected.xAxis);
    EXPECT_EQ(actual.yAxis, expected.yAxis);
    EXPECT_EQ(actual.buttons, expected.buttons);
}

Result<std::monostate, std::string> runSearchToCompletion(
    SmbDfsSearch& search, size_t maxTicks = 200000u)
{
    for (size_t tickIndex = 0; tickIndex < maxTicks; ++tickIndex) {
        const auto tickResult = search.tick();
        if (tickResult.error.has_value()) {
            return Result<std::monostate, std::string>::error(tickResult.error.value());
        }
        if (tickResult.completed) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }
    }

    return Result<std::monostate, std::string>::error("DFS search did not complete in time");
}

void expectTraceEq(const SmbDfsSearchTraceEntry& actual, const SmbDfsSearchTraceEntry& expected)
{
    EXPECT_EQ(actual.eventType, expected.eventType);
    EXPECT_EQ(actual.nodeIndex, expected.nodeIndex);
    EXPECT_EQ(actual.parentIndex, expected.parentIndex);
    EXPECT_EQ(actual.action, expected.action);
    EXPECT_EQ(actual.gameplayFrame, expected.gameplayFrame);
    EXPECT_EQ(actual.frontier, expected.frontier);
    EXPECT_DOUBLE_EQ(actual.evaluationScore, expected.evaluationScore);
    EXPECT_EQ(actual.framesSinceProgress, expected.framesSinceProgress);
}

void expectPlanFramesEq(
    const std::vector<DirtSim::PlayerControlFrame>& actual,
    const std::vector<DirtSim::PlayerControlFrame>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        expectFrameEq(actual[i], expected[i]);
    }
}

} // namespace

TEST(SmbDfsSearchTest, StartCapturesRoot)
{
    requireSmbRomOrSkip();

    SmbDfsSearch search;
    const auto startResult = search.startDfs();
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    EXPECT_FALSE(search.isCompleted());
    EXPECT_TRUE(search.hasRenderableFrame());
    EXPECT_GT(search.getProgress().bestFrontier, 0u);
    ASSERT_FALSE(search.getTrace().empty());
    EXPECT_EQ(search.getTrace().front().eventType, SmbDfsSearchTraceEventType::RootInitialized);
}

TEST(SmbDfsSearchTest, TickAdvancesSearch)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search;
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    uint64_t lastBestFrontier = search.getProgress().bestFrontier;
    uint64_t lastSearchedNodeCount = search.getProgress().searchedNodeCount;
    for (size_t tickIndex = 0; tickIndex < 16u; ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();
        EXPECT_GE(search.getProgress().bestFrontier, lastBestFrontier);
        EXPECT_GT(search.getProgress().searchedNodeCount, lastSearchedNodeCount);
        lastBestFrontier = search.getProgress().bestFrontier;
        lastSearchedNodeCount = search.getProgress().searchedNodeCount;
        if (tickResult.completed) {
            break;
        }
    }
}

TEST(SmbDfsSearchTest, PrunesAndBacktracksHazardBranches)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 5000u,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    bool sawPrune = false;
    bool sawBacktrack = false;
    for (size_t tickIndex = 0; tickIndex < 5000u && !search.isCompleted(); ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();

        for (const auto& entry : search.getTrace()) {
            sawPrune |= entry.eventType == SmbDfsSearchTraceEventType::PrunedDead
                || entry.eventType == SmbDfsSearchTraceEventType::PrunedStalled;
            sawBacktrack |= entry.eventType == SmbDfsSearchTraceEventType::Backtracked;
        }

        if (sawPrune && sawBacktrack) {
            break;
        }
    }

    EXPECT_TRUE(sawPrune);
    EXPECT_TRUE(sawBacktrack);
}

TEST(SmbDfsSearchTest, FindsPlanPastGoomba)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto flatResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    const auto goombaResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(flatResult.isError()) << flatResult.errorValue();
    ASSERT_FALSE(goombaResult.isError()) << goombaResult.errorValue();

    const uint64_t targetFrontier = goombaResult.value().evaluatorSummary.bestFrontier + 32u;
    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 250000u,
            .stallFrameLimit = 120u,
            .stopAfterBestFrontier = targetFrontier,
        });
    const auto startResult = search.startFromFixture(flatResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto runResult = runSearchToCompletion(search, 250000u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();
    ASSERT_TRUE(search.hasPersistablePlan());
    EXPECT_GE(search.getPlan().summary.bestFrontier, targetFrontier);
    EXPECT_GT(search.getPlan().summary.elapsedFrames, 0u);
}

TEST(SmbDfsSearchTest, DeterministicTrace)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    const SmbDfsSearchOptions options{
        .maxSearchedNodeCount = 2000u,
        .stallFrameLimit = 120u,
    };
    SmbDfsSearch firstSearch(options);
    SmbDfsSearch secondSearch(options);
    const auto firstStartResult = firstSearch.startFromFixture(fixtureResult.value());
    const auto secondStartResult = secondSearch.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(firstStartResult.isError()) << firstStartResult.errorValue();
    ASSERT_FALSE(secondStartResult.isError()) << secondStartResult.errorValue();

    const auto firstRunResult = runSearchToCompletion(firstSearch, 3000u);
    const auto secondRunResult = runSearchToCompletion(secondSearch, 3000u);
    ASSERT_FALSE(firstRunResult.isError()) << firstRunResult.errorValue();
    ASSERT_FALSE(secondRunResult.isError()) << secondRunResult.errorValue();

    ASSERT_EQ(firstSearch.getTrace().size(), secondSearch.getTrace().size());
    for (size_t i = 0; i < firstSearch.getTrace().size(); ++i) {
        expectTraceEq(firstSearch.getTrace()[i], secondSearch.getTrace()[i]);
    }

    EXPECT_EQ(
        firstSearch.getPlan().summary.bestFrontier, secondSearch.getPlan().summary.bestFrontier);
    EXPECT_EQ(
        firstSearch.getPlan().summary.elapsedFrames, secondSearch.getPlan().summary.elapsedFrames);
    expectPlanFramesEq(firstSearch.getPlan().frames, secondSearch.getPlan().frames);
}

TEST(SmbDfsSearchTest, ReconstructedPlanIsPlayable)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto flatResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    const auto goombaResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(flatResult.isError()) << flatResult.errorValue();
    ASSERT_FALSE(goombaResult.isError()) << goombaResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 250000u,
            .stallFrameLimit = 120u,
            .stopAfterBestFrontier = goombaResult.value().evaluatorSummary.bestFrontier + 32u,
        });
    const auto startResult = search.startFromFixture(flatResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    const auto runResult = runSearchToCompletion(search, 250000u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();
    ASSERT_TRUE(search.hasPersistablePlan());

    SmbPlanExecution playback;
    const auto playbackStartResult = playback.startPlayback(search.getPlan());
    ASSERT_FALSE(playbackStartResult.isError()) << playbackStartResult.errorValue();

    for (size_t tickIndex = 0; tickIndex < search.getPlan().frames.size() + 2000u; ++tickIndex) {
        const auto tickResult = playback.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();
        if (tickResult.completed) {
            EXPECT_EQ(
                playback.getCompletionReason(),
                std::optional<SmbPlanExecutionCompletionReason>{
                    SmbPlanExecutionCompletionReason::Completed });
            return;
        }
    }

    FAIL() << "Playback did not complete in time.";
}

TEST(SmbDfsSearchTest, StopCompletes)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search;
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto firstTickResult = search.tick();
    ASSERT_FALSE(firstTickResult.error.has_value()) << firstTickResult.error.value();
    search.stop();

    EXPECT_TRUE(search.isCompleted());
    EXPECT_EQ(search.getCompletionReason(), SmbDfsSearchCompletionReason::Stopped);
}

TEST(SmbDfsSearchTest, PauseHaltsTicks)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search;
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    search.pauseSet(true);
    const uint64_t pausedSearchedNodeCount = search.getProgress().searchedNodeCount;
    const auto pausedTickResult = search.tick();
    ASSERT_FALSE(pausedTickResult.error.has_value()) << pausedTickResult.error.value();
    EXPECT_FALSE(pausedTickResult.frameAdvanced);
    EXPECT_FALSE(pausedTickResult.completed);
    EXPECT_EQ(search.getProgress().searchedNodeCount, pausedSearchedNodeCount);

    search.pauseSet(false);
    const auto resumedTickResult = search.tick();
    ASSERT_FALSE(resumedTickResult.error.has_value()) << resumedTickResult.error.value();
    EXPECT_TRUE(resumedTickResult.frameAdvanced);
    EXPECT_GT(search.getProgress().searchedNodeCount, pausedSearchedNodeCount);
}

TEST(SmbDfsSearchTest, LegalActionOrderRightRunFirst)
{
    const auto& legalActions = getSmbSearchLegalActions();
    ASSERT_FALSE(legalActions.empty());
    EXPECT_EQ(legalActions[0], SmbSearchLegalAction::RightRun);
}
