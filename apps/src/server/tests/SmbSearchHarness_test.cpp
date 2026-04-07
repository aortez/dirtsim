#include "SmbSearchTestHelpers.h"
#include "server/search/SmbSearchHarness.h"

#include <gtest/gtest.h>

using namespace DirtSim::Server::SearchSupport;
using DirtSim::Test::expectFrameEq;
using DirtSim::Test::requireSmbRomOrSkip;

namespace {

std::vector<SmbSearchLegalAction> makeReplayActions()
{
    return {
        SmbSearchLegalAction::RightRun,     SmbSearchLegalAction::RightRun,
        SmbSearchLegalAction::RightJumpRun, SmbSearchLegalAction::RightJumpRun,
        SmbSearchLegalAction::RightRun,     SmbSearchLegalAction::LeftRun,
        SmbSearchLegalAction::Duck,         SmbSearchLegalAction::DuckRightJumpRun,
    };
}

} // namespace

TEST(SmbSearchHarnessTest, LegalActionsMapToExpectedFrames)
{
    const auto neutral = smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::Neutral);
    EXPECT_EQ(neutral.xAxis, 0);
    EXPECT_EQ(neutral.yAxis, 0);
    EXPECT_EQ(neutral.buttons, 0u);

    const auto rightRun =
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightJumpRun);
    EXPECT_EQ(rightRun.xAxis, 127);
    EXPECT_EQ(rightRun.yAxis, 0);
    EXPECT_EQ(
        rightRun.buttons,
        DirtSim::PlayerControlButtons::ButtonA | DirtSim::PlayerControlButtons::ButtonB);

    const auto duckLeft =
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::DuckLeftJumpRun);
    EXPECT_EQ(duckLeft.xAxis, -127);
    EXPECT_EQ(duckLeft.yAxis, 127);
    EXPECT_EQ(
        duckLeft.buttons,
        DirtSim::PlayerControlButtons::ButtonA | DirtSim::PlayerControlButtons::ButtonB);
}

TEST(SmbSearchHarnessTest, ReconstructPlanFramesUsesParentChain)
{
    std::vector<SmbSearchNode> nodes(4);
    nodes[0].parentIndex = std::nullopt;
    nodes[1].parentIndex = 0u;
    nodes[1].actionFromParent = SmbSearchLegalAction::RightRun;
    nodes[2].parentIndex = 1u;
    nodes[2].actionFromParent = SmbSearchLegalAction::RightJumpRun;
    nodes[3].parentIndex = 2u;
    nodes[3].actionFromParent = SmbSearchLegalAction::Duck;

    const auto planFramesResult = reconstructPlanFrames(nodes, 3u);
    ASSERT_FALSE(planFramesResult.isError());
    const auto& planFrames = planFramesResult.value();
    ASSERT_EQ(planFrames.size(), 3u);
    expectFrameEq(
        planFrames[0], smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightRun));
    expectFrameEq(
        planFrames[1],
        smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::RightJumpRun));
    expectFrameEq(
        planFrames[2], smbSearchLegalActionToPlayerControlFrame(SmbSearchLegalAction::Duck));
}

TEST(SmbSearchHarnessTest, CaptureFixtureReturnsGameplayRoots)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto flatResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    const auto goombaResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    const auto gapResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGap);
    ASSERT_FALSE(flatResult.isError()) << flatResult.errorValue();
    ASSERT_FALSE(goombaResult.isError()) << goombaResult.errorValue();
    ASSERT_FALSE(gapResult.isError()) << gapResult.errorValue();

    const auto& flat = flatResult.value();
    const auto& goomba = goombaResult.value();
    const auto& gap = gapResult.value();
    const auto flatReplayCheck = harness.replayFromRoot(
        flat.savestate, flat.evaluatorSummary, std::vector<DirtSim::PlayerControlFrame>{});
    const auto goombaReplayCheck = harness.replayFromRoot(
        goomba.savestate, goomba.evaluatorSummary, std::vector<DirtSim::PlayerControlFrame>{});
    const auto gapReplayCheck = harness.replayFromRoot(
        gap.savestate, gap.evaluatorSummary, std::vector<DirtSim::PlayerControlFrame>{});

    EXPECT_EQ(flat.evaluatorSummary.gameState, std::optional<uint8_t>(1u));
    EXPECT_EQ(goomba.evaluatorSummary.gameState, std::optional<uint8_t>(1u));
    EXPECT_EQ(gap.evaluatorSummary.gameState, std::optional<uint8_t>(1u));
    EXPECT_GE(flat.evaluatorSummary.gameplayFrames, 40u);
    EXPECT_GE(goomba.evaluatorSummary.gameplayFrames, 95u);
    EXPECT_GE(gap.evaluatorSummary.gameplayFrames, 240u);
    EXPECT_LT(flat.evaluatorSummary.bestFrontier, gap.evaluatorSummary.bestFrontier);
    ASSERT_FALSE(flatReplayCheck.isError()) << flatReplayCheck.errorValue();
    ASSERT_FALSE(goombaReplayCheck.isError()) << goombaReplayCheck.errorValue();
    ASSERT_FALSE(gapReplayCheck.isError()) << gapReplayCheck.errorValue();
}

TEST(SmbSearchHarnessTest, ReplayFromRootIsDeterministic)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();
    const auto& fixture = fixtureResult.value();

    const std::vector<SmbSearchLegalAction> replayActions = makeReplayActions();
    const auto firstReplayResult =
        harness.replayFromRoot(fixture.savestate, fixture.evaluatorSummary, replayActions);
    const auto secondReplayResult =
        harness.replayFromRoot(fixture.savestate, fixture.evaluatorSummary, replayActions);
    ASSERT_FALSE(firstReplayResult.isError()) << firstReplayResult.errorValue();
    ASSERT_FALSE(secondReplayResult.isError()) << secondReplayResult.errorValue();

    const auto& firstReplay = firstReplayResult.value();
    const auto& secondReplay = secondReplayResult.value();
    ASSERT_TRUE(firstReplay.savestate.has_value());
    ASSERT_TRUE(secondReplay.savestate.has_value());
    ASSERT_TRUE(firstReplay.memorySnapshot.has_value());
    ASSERT_TRUE(secondReplay.memorySnapshot.has_value());
    ASSERT_TRUE(firstReplay.scenarioVideoFrame.has_value());
    ASSERT_TRUE(secondReplay.scenarioVideoFrame.has_value());

    EXPECT_EQ(
        firstReplay.evaluatorSummary.bestFrontier, secondReplay.evaluatorSummary.bestFrontier);
    EXPECT_EQ(
        firstReplay.evaluatorSummary.gameplayFrames, secondReplay.evaluatorSummary.gameplayFrames);
    EXPECT_EQ(
        firstReplay.evaluatorSummary.gameplayFramesSinceProgress,
        secondReplay.evaluatorSummary.gameplayFramesSinceProgress);
    EXPECT_EQ(firstReplay.memorySnapshot->cpuRam, secondReplay.memorySnapshot->cpuRam);
    EXPECT_EQ(firstReplay.memorySnapshot->prgRam, secondReplay.memorySnapshot->prgRam);
    EXPECT_EQ(firstReplay.scenarioVideoFrame->pixels, secondReplay.scenarioVideoFrame->pixels);
    EXPECT_EQ(firstReplay.savestate->bytes, secondReplay.savestate->bytes);
    EXPECT_GE(firstReplay.evaluatorSummary.gameplayFrames, fixture.evaluatorSummary.gameplayFrames);
    EXPECT_GE(firstReplay.evaluatorSummary.bestFrontier, fixture.evaluatorSummary.bestFrontier);
}
