#include "core/scenarios/tests/NesTestRomPath.h"
#include "server/search/SmbSearchHarness.h"
#include "server/search/SmbSegmentSearchSession.h"

#include <gtest/gtest.h>

using namespace DirtSim::Server::SearchSupport;

namespace {

void requireSmbRomOrSkip()
{
    if (!DirtSim::Test::resolveSmbRomPath().has_value()) {
        GTEST_SKIP() << "DIRTSIM_NES_SMB_TEST_ROM_PATH or testdata/roms/smb.nes is required.";
    }
}

SmbSegmentSearchSessionParams makeParams(
    uint32_t beamWidth, uint32_t segmentFrameBudget, uint32_t maxSegments)
{
    return SmbSegmentSearchSessionParams{
        .maxSegments = maxSegments,
        .segmentParams =
            SmbSegmentBeamSearchParams{
                .beamWidth = beamWidth,
                .segmentFrameBudget = segmentFrameBudget,
            },
    };
}

} // namespace

TEST(SmbSegmentSearchSessionTest, PromotesRootsAndAccumulatesReplayablePlan)
{
    requireSmbRomOrSkip();

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();
    const auto& initialRoot = fixtureResult.value();

    const SmbSegmentSearchSession session;
    const auto sessionResult = session.run(initialRoot, makeParams(2u, 3u, 2u));
    ASSERT_FALSE(sessionResult.isError()) << sessionResult.errorValue();
    const auto& result = sessionResult.value();

    EXPECT_EQ(result.outcome, SmbSegmentSearchSessionOutcome::ReachedSegmentLimit);
    EXPECT_EQ(result.segmentAttempts, 2u);
    EXPECT_EQ(result.successfulSegments, 2u);
    ASSERT_EQ(result.committedRoots.size(), 3u);
    ASSERT_EQ(result.promotions.size(), 2u);
    EXPECT_FALSE(result.planFrames.empty());
    EXPECT_GT(result.expandedNodeCount, 0u);

    EXPECT_LT(
        result.committedRoots[0].evaluatorSummary.bestFrontier,
        result.committedRoots[1].evaluatorSummary.bestFrontier);
    EXPECT_LT(
        result.committedRoots[1].evaluatorSummary.bestFrontier,
        result.committedRoots[2].evaluatorSummary.bestFrontier);

    for (size_t promotionIndex = 0; promotionIndex < result.promotions.size(); ++promotionIndex) {
        const auto& previousRoot = result.committedRoots[promotionIndex];
        const auto& promotion = result.promotions[promotionIndex];
        ASSERT_FALSE(promotion.segmentFrames.empty());

        const auto replayResult = harness.replayFromRoot(
            previousRoot.savestate, previousRoot.evaluatorSummary, promotion.segmentFrames);
        ASSERT_FALSE(replayResult.isError()) << replayResult.errorValue();
        ASSERT_TRUE(replayResult.value().savestate.has_value());
        EXPECT_EQ(
            replayResult.value().evaluatorSummary.bestFrontier,
            promotion.committedRoot.evaluatorSummary.bestFrontier);
        EXPECT_EQ(replayResult.value().savestate->bytes, promotion.committedRoot.savestate.bytes);
    }

    const auto accumulatedReplayResult = harness.replayFromRoot(
        initialRoot.savestate, initialRoot.evaluatorSummary, result.planFrames);
    ASSERT_FALSE(accumulatedReplayResult.isError()) << accumulatedReplayResult.errorValue();
    ASSERT_TRUE(accumulatedReplayResult.value().savestate.has_value());
    EXPECT_EQ(
        accumulatedReplayResult.value().evaluatorSummary.bestFrontier,
        result.committedRoots.back().evaluatorSummary.bestFrontier);
    EXPECT_EQ(
        accumulatedReplayResult.value().savestate->bytes,
        result.committedRoots.back().savestate.bytes);
}

TEST(SmbSegmentSearchSessionTest, SegmentFailureEndsSessionAtCurrentCommittedRoot)
{
    requireSmbRomOrSkip();

    const SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    const SmbSegmentSearchSession session;
    const auto sessionResult = session.run(fixtureResult.value(), makeParams(3u, 0u, 3u));
    ASSERT_FALSE(sessionResult.isError()) << sessionResult.errorValue();
    const auto& result = sessionResult.value();

    EXPECT_EQ(result.outcome, SmbSegmentSearchSessionOutcome::SegmentFailure);
    EXPECT_EQ(result.segmentAttempts, 1u);
    EXPECT_EQ(result.successfulSegments, 0u);
    EXPECT_EQ(result.expandedNodeCount, 0u);
    ASSERT_EQ(result.committedRoots.size(), 1u);
    EXPECT_TRUE(result.promotions.empty());
    EXPECT_TRUE(result.planFrames.empty());
    EXPECT_EQ(result.committedRoots.front().savestate.bytes, fixtureResult.value().savestate.bytes);
}
