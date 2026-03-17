#include "core/scenarios/nes/NesSuperMarioBrosResponseProbe.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

SmolnesRuntime::MemorySnapshot makeSmbSnapshot(
    uint8_t playerState,
    uint8_t playerFloatState,
    uint8_t facingDirection,
    uint8_t movementDirection,
    uint8_t playerXPage,
    uint8_t playerXScreen,
    uint8_t horizontalSpeed,
    uint8_t verticalSpeed,
    uint8_t playerYScreen,
    uint8_t powerupState = 0u,
    uint8_t duckingState = 0u,
    uint8_t playerOneButtonsPressed = 0u)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0u);
    snapshot.prgRam.fill(0u);
    snapshot.cpuRam[0x000E] = playerState;
    snapshot.cpuRam[0x001D] = playerFloatState;
    snapshot.cpuRam[0x0033] = facingDirection;
    snapshot.cpuRam[0x0045] = movementDirection;
    snapshot.cpuRam[0x0057] = horizontalSpeed;
    snapshot.cpuRam[0x006D] = playerXPage;
    snapshot.cpuRam[0x0086] = playerXScreen;
    snapshot.cpuRam[0x009F] = verticalSpeed;
    snapshot.cpuRam[0x00CE] = playerYScreen;
    snapshot.cpuRam[0x0714] = duckingState;
    snapshot.cpuRam[0x074A] = playerOneButtonsPressed;
    snapshot.cpuRam[0x0756] = powerupState;
    snapshot.cpuRam[0x075A] = 3u;
    snapshot.cpuRam[0x075F] = 1u;
    snapshot.cpuRam[0x0760] = 1u;
    snapshot.cpuRam[0x0770] = 1u;
    return snapshot;
}

std::optional<NesControllerTelemetry> makeLiveTelemetry(
    uint8_t controllerMask,
    uint64_t sequenceId,
    uint64_t observedTimestampNs,
    uint64_t requestTimestampNs,
    uint64_t latchTimestampNs,
    std::optional<uint64_t> controllerAppliedFrameId = std::nullopt)
{
    return NesControllerTelemetry{
        .aRaw = (controllerMask & SMOLNES_RUNTIME_BUTTON_A) != 0 ? 1.0f : 0.0f,
        .bRaw = (controllerMask & SMOLNES_RUNTIME_BUTTON_B) != 0 ? 1.0f : 0.0f,
        .xRaw = 0.0f,
        .yRaw = 0.0f,
        .inferredControllerMask = controllerMask,
        .resolvedControllerMask = controllerMask,
        .controllerSource = NesGameAdapterControllerSource::LiveInput,
        .controllerSourceFrameIndex = 0u,
        .controllerAppliedFrameId = controllerAppliedFrameId,
        .controllerObservedTimestampNs = observedTimestampNs,
        .controllerLatchTimestampNs = latchTimestampNs,
        .controllerRequestTimestampNs = requestTimestampNs,
        .controllerSequenceId = sequenceId,
    };
}

} // namespace

TEST(NesSuperMarioBrosResponseProbeTest, DetectsImmediateRightResponseFromStationaryState)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x21u, 16u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_RIGHT, 1u, 900u, 1000u, 2000u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::MoveRight);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::GroundedStart);
    EXPECT_EQ(response->milestone, NesSuperMarioBrosResponseMilestone::Motion);
    EXPECT_EQ(response->controllerAppliedFrameId, 301u);
    EXPECT_EQ(response->responseFrameId, 301u);
}

TEST(NesSuperMarioBrosResponseProbeTest, DetectsImmediateJumpResponseFromGroundedState)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(
            0x08u,
            0x01u,
            1u,
            1u,
            0x00u,
            0x20u,
            0u,
            static_cast<uint8_t>(static_cast<int8_t>(-24)),
            118u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_A, 1u, 900u, 1000u, 2000u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::Jump);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::StandingJump);
    EXPECT_EQ(response->milestone, NesSuperMarioBrosResponseMilestone::Acknowledge);
    EXPECT_EQ(response->responseFrameId, 301u);
}

TEST(NesSuperMarioBrosResponseProbeTest, DetectsLeftResponseFromFacingChangeBeforeVelocityTurns)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x40u, 20u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 2u, 1u, 0x00u, 0x40u, 8u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_LEFT, 1u, 900u, 1000u, 2000u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::MoveLeft);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::GroundedTurnaround);
    EXPECT_EQ(response->milestone, NesSuperMarioBrosResponseMilestone::Acknowledge);
    EXPECT_EQ(response->responseFrameId, 301u);
}

TEST(NesSuperMarioBrosResponseProbeTest, DoesNotArmJumpResponseWhenAlreadyAirborne)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x01u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x01u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 119u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_A, 1u, 900u, 1000u, 2000u));

    EXPECT_FALSE(response.has_value());
}

TEST(NesSuperMarioBrosResponseProbeTest, IgnoresSameDirectionRepressWhileAlreadyCommitted)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x40u, 20u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x41u, 22u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_RIGHT, 1u, 900u, 1000u, 2000u));

    EXPECT_FALSE(response.has_value());
}

TEST(NesSuperMarioBrosResponseProbeTest, IgnoresAirborneMovementProbe)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x01u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x01u, 2u, 2u, 0x00u, 0x20u, 0u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_LEFT, 1u, 900u, 1000u, 2000u));

    EXPECT_FALSE(response.has_value());
}

TEST(
    NesSuperMarioBrosResponseProbeTest,
    DetectsRightTurnaroundFromMovementDirectionChangeWhenFacingAlreadyMatches)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 2u, 0x00u, 0x40u, 0xF0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x40u, 0xF8u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_RIGHT, 2u, 2900u, 3000u, 4000u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::MoveRight);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::GroundedTurnaround);
    EXPECT_EQ(response->milestone, NesSuperMarioBrosResponseMilestone::Commit);
    EXPECT_EQ(response->responseFrameId, 301u);
}

TEST(
    NesSuperMarioBrosResponseProbeTest,
    DetectsStandingJumpFromFloatStateTransitionBeforeMovementChanges)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x01u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_A, 2u, 2900u, 3000u, 4000u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::Jump);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::StandingJump);
    EXPECT_EQ(response->milestone, NesSuperMarioBrosResponseMilestone::Acknowledge);
    EXPECT_EQ(response->responseFrameId, 301u);
}

TEST(NesSuperMarioBrosResponseProbeTest, DetectsBigMarioDuckFromRawDuckRegister)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u, 1u, 0u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u, 1u, 0x04u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_DOWN, 3u, 3900u, 4000u, 5000u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::Duck);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::GroundedDuck);
    EXPECT_EQ(response->milestone, NesSuperMarioBrosResponseMilestone::Acknowledge);
    EXPECT_EQ(response->responseFrameId, 301u);
}

TEST(NesSuperMarioBrosResponseProbeTest, IgnoresDuckProbeForSmallMario)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u, 0u, 0u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u, 0u, 0x04u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_DOWN, 3u, 3900u, 4000u, 5000u));

    EXPECT_FALSE(response.has_value());
}

TEST(NesSuperMarioBrosResponseProbeTest, EmitsTurnaroundMilestonesInOrderAcrossFrames)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x40u, 20u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto acknowledge = probe.observeFrame(
        301u,
        makeSmbSnapshot(0x08u, 0x00u, 2u, 1u, 0x00u, 0x40u, 8u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_LEFT, 4u, 4900u, 5000u, 6000u));

    ASSERT_TRUE(acknowledge.has_value());
    EXPECT_EQ(acknowledge->kind, NesSuperMarioBrosResponseKind::MoveLeft);
    EXPECT_EQ(acknowledge->context, NesSuperMarioBrosResponseContext::GroundedTurnaround);
    EXPECT_EQ(acknowledge->milestone, NesSuperMarioBrosResponseMilestone::Acknowledge);
    EXPECT_EQ(acknowledge->responseEventId, 1u);
    EXPECT_EQ(acknowledge->responseFrameId, 301u);

    const auto commit = probe.observeFrame(
        302u, makeSmbSnapshot(0x08u, 0x00u, 2u, 2u, 0x00u, 0x40u, 2u, 0u, 120u), std::nullopt);

    ASSERT_TRUE(commit.has_value());
    EXPECT_EQ(commit->milestone, NesSuperMarioBrosResponseMilestone::Commit);
    EXPECT_EQ(commit->responseEventId, 2u);
    EXPECT_EQ(commit->responseFrameId, 302u);

    const auto motion = probe.observeFrame(
        303u, makeSmbSnapshot(0x08u, 0x00u, 2u, 2u, 0x00u, 0x3Fu, 0xF0u, 0u, 120u), std::nullopt);

    ASSERT_TRUE(motion.has_value());
    EXPECT_EQ(motion->milestone, NesSuperMarioBrosResponseMilestone::Motion);
    EXPECT_EQ(motion->responseEventId, 3u);
    EXPECT_EQ(motion->responseFrameId, 303u);
}

TEST(NesSuperMarioBrosResponseProbeTest, CarriesFirstGameInputCopiedFrameIntoLaterResponse)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    EXPECT_FALSE(
        probe
            .observeFrame(
                301u,
                makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u, 0u, 0u, 0x01u),
                makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_RIGHT, 5u, 5900u, 6000u, 7000u))
            .has_value());

    const auto response = probe.observeFrame(
        302u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x21u, 16u, 0u, 120u, 0u, 0u, 0x01u),
        std::nullopt);

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, NesSuperMarioBrosResponseKind::MoveRight);
    EXPECT_EQ(response->context, NesSuperMarioBrosResponseContext::GroundedStart);
    EXPECT_EQ(response->gameInputCopiedFrameId, 301u);
}

TEST(NesSuperMarioBrosResponseProbeTest, PreservesTelemetryAppliedFrameIdWhenResponseArrivesLater)
{
    NesSuperMarioBrosResponseProbe probe;

    EXPECT_FALSE(probe
                     .observeFrame(
                         300u,
                         makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x20u, 0u, 0u, 120u),
                         std::nullopt)
                     .has_value());

    const auto response = probe.observeFrame(
        305u,
        makeSmbSnapshot(0x08u, 0x00u, 1u, 1u, 0x00u, 0x21u, 16u, 0u, 120u),
        makeLiveTelemetry(SMOLNES_RUNTIME_BUTTON_RIGHT, 6u, 6900u, 7000u, 8000u, 301u));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->controllerAppliedFrameId, 301u);
    EXPECT_EQ(response->responseFrameId, 305u);
}
