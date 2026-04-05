#include "core/RenderMessage.h"
#include "core/scenarios/tests/NesTestRomPath.h"
#include "server/search/SmbDfsSearch.h"
#include "server/search/SmbPlanExecution.h"
#include "server/search/SmbSearchHarness.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>

using namespace DirtSim::Server::SearchSupport;

namespace {

void requireSmbRomOrSkip()
{
    if (!DirtSim::Test::resolveSmbRomPath().has_value()) {
        GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is required.";
    }
}

// PPM screenshot helpers (from NesSuperMarioBrosRamProbe_test.cpp).

std::optional<uint16_t> readRgb565Pixel(const DirtSim::ScenarioVideoFrame& frame, size_t pixelIndex)
{
    const size_t offset = pixelIndex * 2u;
    if (offset + 1u >= frame.pixels.size()) {
        return std::nullopt;
    }
    const uint8_t lo = std::to_integer<uint8_t>(frame.pixels[offset]);
    const uint8_t hi = std::to_integer<uint8_t>(frame.pixels[offset + 1u]);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}

std::array<uint8_t, 3> rgb565ToRgb888(uint16_t value)
{
    const uint8_t red5 = static_cast<uint8_t>((value >> 11) & 0x1Fu);
    const uint8_t green6 = static_cast<uint8_t>((value >> 5) & 0x3Fu);
    const uint8_t blue5 = static_cast<uint8_t>(value & 0x1Fu);
    const uint8_t red8 = static_cast<uint8_t>((red5 << 3) | (red5 >> 2));
    const uint8_t green8 = static_cast<uint8_t>((green6 << 2) | (green6 >> 4));
    const uint8_t blue8 = static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2));
    return { red8, green8, blue8 };
}

bool writeScenarioFramePpm(
    const DirtSim::ScenarioVideoFrame& frame, const std::filesystem::path& path)
{
    if (frame.width == 0 || frame.height == 0) {
        return false;
    }
    const size_t expectedBytes =
        static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 2u;
    if (frame.pixels.size() != expectedBytes) {
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }
    stream << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const std::optional<uint16_t> rgb565 = readRgb565Pixel(frame, pixelIndex);
        if (!rgb565.has_value()) {
            return false;
        }
        const std::array<uint8_t, 3> rgb = rgb565ToRgb888(rgb565.value());
        stream.write(
            reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    }
    return stream.good();
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
    ASSERT_FALSE(flatResult.isError()) << flatResult.errorValue();

    // Determine the goomba death frontier by replaying pure RightRun from the fixture.
    // The bestFrontier after replay is the furthest Mario gets before dying.
    const size_t holdRightFrameCount = 200u;
    std::vector<SmbSearchLegalAction> holdRightActions(
        holdRightFrameCount, SmbSearchLegalAction::RightRun);
    const auto holdRightReplay = harness.replayFromRoot(
        flatResult.value().savestate, flatResult.value().evaluatorSummary, holdRightActions);
    ASSERT_FALSE(holdRightReplay.isError()) << holdRightReplay.errorValue();
    const uint64_t holdRightDeathFrontier = holdRightReplay.value().evaluatorSummary.bestFrontier;
    ASSERT_GT(holdRightDeathFrontier, 0u) << "Hold-right replay produced no frontier progress.";

    // Target must be past the death frontier to prove the search navigated around the goomba.
    const uint64_t targetFrontier = holdRightDeathFrontier + 64u;
    std::cout << "Hold-right death frontier: " << holdRightDeathFrontier << "\n";
    std::cout << "Target frontier (death + 64): " << targetFrontier << "\n";

    // Run the DFS search.
    constexpr uint64_t kSearchBudget = 100000u;
    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = kSearchBudget,
            .stallFrameLimit = 120u,
            .stopAfterBestFrontier = targetFrontier,
        });
    const auto startResult = search.startFromFixture(flatResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto runResult = runSearchToCompletion(search, kSearchBudget * 2u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();

    // Report results.
    std::cout << "Searched nodes: " << search.getProgress().searchedNodeCount << "\n";
    std::cout << "Best frontier: " << search.getProgress().bestFrontier << "\n";
    std::cout << "Plan frames: " << search.getPlan().frames.size() << "\n";
    const bool reachedTarget = search.getProgress().bestFrontier >= targetFrontier;
    std::cout << "Reached target: " << (reachedTarget ? "YES" : "NO") << "\n";

    // Screenshot the search's best leaf video frame directly.
    const auto screenshotDir = std::filesystem::path(::testing::TempDir());
    if (search.getScenarioVideoFrame().has_value()) {
        const auto bestLeafScreenshot = screenshotDir / "dfs_goomba_best_leaf.ppm";
        writeScenarioFramePpm(search.getScenarioVideoFrame().value(), bestLeafScreenshot);
        std::cout << "Best leaf screenshot: " << bestLeafScreenshot.string() << "\n";
    }

    // Screenshot the first PrunedDead node from the trace to show where death was detected.
    for (const auto& entry : search.getTrace()) {
        if (entry.eventType != SmbDfsSearchTraceEventType::PrunedDead) {
            continue;
        }
        std::cout << "First death prune at node " << entry.nodeIndex << " (frame "
                  << entry.gameplayFrame << ", frontier " << entry.frontier << ")\n";
        break;
    }

    // Count trace events for diagnostics.
    size_t expandedCount = 0;
    size_t prunedDeadCount = 0;
    size_t prunedStalledCount = 0;
    size_t backtrackedCount = 0;
    for (const auto& entry : search.getTrace()) {
        if (entry.eventType == SmbDfsSearchTraceEventType::ExpandedAlive) {
            expandedCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::PrunedDead) {
            prunedDeadCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::PrunedStalled) {
            prunedStalledCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::Backtracked) {
            backtrackedCount++;
        }
    }
    std::cout << "Trace: expanded=" << expandedCount << " prunedDead=" << prunedDeadCount
              << " prunedStalled=" << prunedStalledCount << " backtracked=" << backtrackedCount
              << "\n";

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

TEST(SmbDfsSearchTest, BacktrackSignalsRenderChange)
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

    bool sawBacktrackRenderChange = false;
    for (size_t tickIndex = 0; tickIndex < 5000u && !search.isCompleted(); ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();
        if (tickResult.renderChanged && !tickResult.frameAdvanced
            && search.getProgress().lastSearchEvent
                == DirtSim::Api::SearchProgressEvent::Backtracked) {
            sawBacktrackRenderChange = true;
            break;
        }
    }

    EXPECT_TRUE(sawBacktrackRenderChange);
}

TEST(SmbDfsSearchTest, LegalActionOrderRightRunFirst)
{
    const auto& legalActions = getSmbSearchLegalActions();
    ASSERT_FALSE(legalActions.empty());
    EXPECT_EQ(legalActions[0], SmbSearchLegalAction::RightRun);
}
