#include "core/scenarios/tests/NesTestRomPath.h"
#include "server/search/SmbSegmentBeamSearch.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace DirtSim::Server::SearchSupport;

namespace {

void expectFrameEq(
    const DirtSim::PlayerControlFrame& actual, const DirtSim::PlayerControlFrame& expected)
{
    EXPECT_EQ(actual.xAxis, expected.xAxis);
    EXPECT_EQ(actual.yAxis, expected.yAxis);
    EXPECT_EQ(actual.buttons, expected.buttons);
}

bool isFrameEq(
    const DirtSim::PlayerControlFrame& actual, const DirtSim::PlayerControlFrame& expected)
{
    return actual.xAxis == expected.xAxis && actual.yAxis == expected.yAxis
        && actual.buttons == expected.buttons;
}

void requireSmbRomOrSkip()
{
    if (!DirtSim::Test::resolveSmbRomPath().has_value()) {
        GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is required.";
    }
}

SmbSegmentBeamSearchParams makeParams(uint32_t beamWidth, uint32_t segmentFrameBudget)
{
    return SmbSegmentBeamSearchParams{
        .beamWidth = beamWidth,
        .segmentFrameBudget = segmentFrameBudget,
    };
}

struct CandidateTrace {
    uint64_t candidateIndex = 0;
    uint64_t bestFrontier = 0;
    double evaluationScore = 0.0;
    bool successful = false;
    bool terminal = false;
    std::vector<DirtSim::PlayerControlFrame> frames;
};

SmbSearchRootFixture promoteFixture(
    const SmbSearchRootFixture& currentRoot, const SmbSearchNode& node, const std::string& suffix)
{
    return SmbSearchRootFixture{
        .id = currentRoot.id,
        .evaluatorSummary = node.evaluatorSummary,
        .memorySnapshot = node.memorySnapshot,
        .scenarioVideoFrame = node.scenarioVideoFrame,
        .savestate = node.savestate,
        .name = currentRoot.name + suffix,
    };
}

bool allFramesMatch(
    const std::vector<DirtSim::PlayerControlFrame>& frames,
    const DirtSim::PlayerControlFrame& expected)
{
    return std::all_of(frames.begin(), frames.end(), [&](const auto& frame) {
        return isFrameEq(frame, expected);
    });
}

bool anyFrameDiffers(
    const std::vector<DirtSim::PlayerControlFrame>& frames,
    const DirtSim::PlayerControlFrame& expected)
{
    return std::any_of(frames.begin(), frames.end(), [&](const auto& frame) {
        return !isFrameEq(frame, expected);
    });
}

} // namespace

TEST(SmbSegmentBeamSearchTest, RunIsDeterministicForFixedRootAndParams)
{
    requireSmbRomOrSkip();

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    const SmbSegmentBeamSearch search;
    const auto firstResult = search.run(fixtureResult.value(), makeParams(2u, 2u));
    const auto secondResult = search.run(fixtureResult.value(), makeParams(2u, 2u));
    if (firstResult.isError()) {
        FAIL() << firstResult.errorValue();
    }
    if (secondResult.isError()) {
        FAIL() << secondResult.errorValue();
    }

    const auto& first = firstResult.value();
    const auto& second = secondResult.value();
    EXPECT_EQ(first.expandedNodeCount, second.expandedNodeCount);
    EXPECT_EQ(first.frontierImproved, second.frontierImproved);
    ASSERT_EQ(first.frontierNodeIndices.size(), second.frontierNodeIndices.size());
    EXPECT_EQ(first.bestNodeIndex.has_value(), second.bestNodeIndex.has_value());

    if (first.bestNodeIndex.has_value()) {
        const auto firstPlanResult =
            reconstructPlanFrames(first.nodes, first.bestNodeIndex.value());
        const auto secondPlanResult =
            reconstructPlanFrames(second.nodes, second.bestNodeIndex.value());
        ASSERT_FALSE(firstPlanResult.isError()) << firstPlanResult.errorValue();
        ASSERT_FALSE(secondPlanResult.isError()) << secondPlanResult.errorValue();
        const auto& firstPlan = firstPlanResult.value();
        const auto& secondPlan = secondPlanResult.value();
        ASSERT_EQ(firstPlan.size(), secondPlan.size());
        for (size_t index = 0; index < firstPlan.size(); ++index) {
            expectFrameEq(firstPlan[index], secondPlan[index]);
        }
    }
}

TEST(SmbSegmentBeamSearchTest, RunFindsPromotableCheckpointOnFlatGroundFixture)
{
    requireSmbRomOrSkip();

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();
    const auto& fixture = fixtureResult.value();

    const SmbSegmentBeamSearch search;
    const auto searchResult = search.run(fixture, makeParams(2u, 3u));
    if (searchResult.isError()) {
        FAIL() << searchResult.errorValue();
    }
    const auto& result = searchResult.value();

    EXPECT_TRUE(result.frontierImproved);
    ASSERT_TRUE(result.bestNodeIndex.has_value());
    EXPECT_EQ(result.nextCandidateIndex, 1u);
    EXPECT_EQ(result.searchDepth, 2u);
    EXPECT_EQ(result.totalCandidateCount, 121u);
    const SmbSearchNode& bestNode = result.nodes[result.bestNodeIndex.value()];
    EXPECT_TRUE(bestNode.checkpointEligible);
    EXPECT_GT(bestNode.evaluatorSummary.bestFrontier, result.rootFrontier);

    const auto planFramesResult = reconstructPlanFrames(result.nodes, result.bestNodeIndex.value());
    ASSERT_FALSE(planFramesResult.isError()) << planFramesResult.errorValue();
    ASSERT_EQ(planFramesResult.value().size(), 3u);
    const auto expectedFrame =
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightRun);
    for (const auto& frame : planFramesResult.value()) {
        expectFrameEq(frame, expectedFrame);
    }
    const auto replayResult = harness.replayFromRoot(
        fixture.savestate, fixture.evaluatorSummary, planFramesResult.value());
    ASSERT_FALSE(replayResult.isError()) << replayResult.errorValue();
    ASSERT_TRUE(replayResult.value().savestate.has_value());
    EXPECT_EQ(
        replayResult.value().evaluatorSummary.bestFrontier, bestNode.evaluatorSummary.bestFrontier);
    EXPECT_EQ(replayResult.value().savestate->bytes, bestNode.savestate.bytes);
}

TEST(SmbSegmentBeamSearchTest, RunFindsPromotableCheckpointOnFirstGoombaFixture)
{
    requireSmbRomOrSkip();

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();
    const auto& fixture = fixtureResult.value();

    const SmbSegmentBeamSearch search;
    const auto searchResult = search.run(fixture, makeParams(2u, 12u));
    if (searchResult.isError()) {
        FAIL() << searchResult.errorValue();
    }
    const auto& result = searchResult.value();

    ASSERT_TRUE(result.bestNodeIndex.has_value());
    const SmbSearchNode& bestNode = result.nodes[result.bestNodeIndex.value()];
    EXPECT_TRUE(bestNode.checkpointEligible);
    EXPECT_GT(bestNode.evaluatorSummary.bestFrontier, result.rootFrontier);

    const auto planFramesResult = reconstructPlanFrames(result.nodes, result.bestNodeIndex.value());
    ASSERT_FALSE(planFramesResult.isError()) << planFramesResult.errorValue();
    ASSERT_FALSE(planFramesResult.value().empty());

    EXPECT_FALSE(planFramesResult.value().empty());
}

TEST(SmbSegmentBeamSearchTest, BaselineDeadEndExhaustsCandidatesBeforeParentFindsAlternative)
{
    requireSmbRomOrSkip();

    constexpr uint32_t kSearchDepth = 2u;
    constexpr uint32_t kSegmentFrameBudget = 14u;
    constexpr size_t kMaxBaselineSegments = 16u;
    const DirtSim::PlayerControlFrame baselineFrame =
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightRun);

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    const SmbSegmentBeamSearch search;
    SmbSearchRootFixture parentRoot = fixtureResult.value();
    SmbSearchRootFixture currentRoot = fixtureResult.value();
    size_t successfulBaselineSegments = 0u;
    bool foundBaselineFailureRoot = false;

    for (; successfulBaselineSegments < kMaxBaselineSegments; ++successfulBaselineSegments) {
        auto stateResult = search.start(currentRoot, 0u);
        ASSERT_FALSE(stateResult.isError()) << stateResult.errorValue();

        SmbSegmentBeamSearchState state = std::move(stateResult.value());
        const auto tickResult = search.tick(state, makeParams(kSearchDepth, kSegmentFrameBudget));
        ASSERT_FALSE(tickResult.isError()) << tickResult.errorValue();
        ASSERT_FALSE(state.frontierNodeIndices.empty());
        EXPECT_EQ(state.nextCandidateIndex, 1u);

        const size_t endpointIndex = state.frontierNodeIndices.front();
        const auto endpointFramesResult = reconstructPlanFrames(state.nodes, endpointIndex);
        ASSERT_FALSE(endpointFramesResult.isError()) << endpointFramesResult.errorValue();
        ASSERT_EQ(endpointFramesResult.value().size(), kSegmentFrameBudget);
        EXPECT_TRUE(allFramesMatch(endpointFramesResult.value(), baselineFrame));

        if (!state.bestNodeIndex.has_value()) {
            foundBaselineFailureRoot = true;
            break;
        }

        ASSERT_EQ(state.bestNodeIndex.value(), endpointIndex);
        ASSERT_TRUE(state.nodes[endpointIndex].checkpointEligible);
        parentRoot = currentRoot;
        currentRoot = promoteFixture(
            currentRoot,
            state.nodes[endpointIndex],
            "_baseline_" + std::to_string(successfulBaselineSegments));
    }

    ASSERT_TRUE(foundBaselineFailureRoot) << "Baseline RightRun candidate never failed within "
                                          << kMaxBaselineSegments << " promoted segments";
    EXPECT_GT(successfulBaselineSegments, 0u);

    auto stateResult = search.start(currentRoot, 0u);
    ASSERT_FALSE(stateResult.isError()) << stateResult.errorValue();

    SmbSegmentBeamSearchState state = std::move(stateResult.value());
    std::vector<CandidateTrace> traces;
    traces.reserve(121u);

    while (!state.completed) {
        const uint64_t candidateIndex = state.nextCandidateIndex;
        const auto tickResult = search.tick(state, makeParams(kSearchDepth, kSegmentFrameBudget));
        ASSERT_FALSE(tickResult.isError()) << tickResult.errorValue();
        ASSERT_FALSE(state.frontierNodeIndices.empty());

        const size_t endpointIndex = state.frontierNodeIndices.front();
        const auto endpointFramesResult = reconstructPlanFrames(state.nodes, endpointIndex);
        ASSERT_FALSE(endpointFramesResult.isError()) << endpointFramesResult.errorValue();

        const SmbSearchNode& endpointNode = state.nodes[endpointIndex];
        traces.push_back(
            CandidateTrace{
                .candidateIndex = candidateIndex,
                .bestFrontier = endpointNode.evaluatorSummary.bestFrontier,
                .evaluationScore = endpointNode.evaluatorSummary.evaluationScore,
                .successful =
                    state.bestNodeIndex.has_value() && state.bestNodeIndex.value() == endpointIndex,
                .terminal = endpointNode.evaluatorSummary.terminal,
                .frames = endpointFramesResult.value(),
            });
    }

    ASSERT_FALSE(traces.empty());
    for (size_t traceIndex = 0u; traceIndex < traces.size(); ++traceIndex) {
        EXPECT_EQ(traces[traceIndex].candidateIndex, traceIndex);
    }

    const CandidateTrace& baselineTrace = traces.front();
    EXPECT_TRUE(allFramesMatch(baselineTrace.frames, baselineFrame));
    EXPECT_FALSE(baselineTrace.successful);
    EXPECT_LE(baselineTrace.evaluationScore, currentRoot.evaluatorSummary.evaluationScore);
    EXPECT_FALSE(state.bestNodeIndex.has_value());
    EXPECT_EQ(traces.size(), state.totalCandidateCount);

    auto parentStateResult = search.start(parentRoot, 1u);
    ASSERT_FALSE(parentStateResult.isError()) << parentStateResult.errorValue();

    SmbSegmentBeamSearchState parentState = std::move(parentStateResult.value());
    std::vector<CandidateTrace> parentTraces;
    parentTraces.reserve(120u);

    while (!parentState.completed) {
        const uint64_t candidateIndex = parentState.nextCandidateIndex;
        const auto tickResult =
            search.tick(parentState, makeParams(kSearchDepth, kSegmentFrameBudget));
        ASSERT_FALSE(tickResult.isError()) << tickResult.errorValue();
        ASSERT_FALSE(parentState.frontierNodeIndices.empty());

        const size_t endpointIndex = parentState.frontierNodeIndices.front();
        const auto endpointFramesResult = reconstructPlanFrames(parentState.nodes, endpointIndex);
        ASSERT_FALSE(endpointFramesResult.isError()) << endpointFramesResult.errorValue();

        const SmbSearchNode& endpointNode = parentState.nodes[endpointIndex];
        parentTraces.push_back(
            CandidateTrace{
                .candidateIndex = candidateIndex,
                .bestFrontier = endpointNode.evaluatorSummary.bestFrontier,
                .evaluationScore = endpointNode.evaluatorSummary.evaluationScore,
                .successful = parentState.bestNodeIndex.has_value()
                    && parentState.bestNodeIndex.value() == endpointIndex,
                .terminal = endpointNode.evaluatorSummary.terminal,
                .frames = endpointFramesResult.value(),
            });
    }

    ASSERT_FALSE(parentTraces.empty());
    EXPECT_EQ(parentTraces.front().candidateIndex, 1u);
    ASSERT_TRUE(parentState.bestNodeIndex.has_value())
        << "Parent root did not find a successful alternative after backtracking";
    const CandidateTrace& successfulTrace = parentTraces.back();
    EXPECT_TRUE(successfulTrace.successful);
    EXPECT_GT(successfulTrace.candidateIndex, 0u);
    EXPECT_GT(successfulTrace.evaluationScore, parentRoot.evaluatorSummary.evaluationScore);
    EXPECT_TRUE(anyFrameDiffers(successfulTrace.frames, baselineFrame));
}

TEST(SmbSegmentBeamSearchTest, ZeroBudgetReturnsRootOnlyWithoutExpansion)
{
    requireSmbRomOrSkip();

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    const SmbSegmentBeamSearch search;
    const auto searchResult = search.run(fixtureResult.value(), makeParams(3u, 0u));
    if (searchResult.isError()) {
        FAIL() << searchResult.errorValue();
    }
    const auto& result = searchResult.value();

    EXPECT_FALSE(result.bestNodeIndex.has_value());
    EXPECT_FALSE(result.frontierImproved);
    EXPECT_EQ(result.expandedNodeCount, 0u);
    ASSERT_EQ(result.frontierNodeIndices.size(), 1u);
    EXPECT_EQ(result.frontierNodeIndices.front(), 0u);
    ASSERT_EQ(result.nodes.size(), 1u);
    EXPECT_EQ(result.nodes.front().depth, 0u);
    EXPECT_EQ(result.rootFrontier, fixtureResult.value().evaluatorSummary.bestFrontier);
}
