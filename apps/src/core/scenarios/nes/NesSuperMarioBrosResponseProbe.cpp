#include "core/scenarios/nes/NesSuperMarioBrosResponseProbe.h"

#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace DirtSim {

namespace {

constexpr uint8_t kFacingDirectionAddr = 0x0033;
constexpr size_t kDuckingStateAddr = 0x0714;
constexpr uint8_t kDuckingStateActive = 0x04;
constexpr size_t kFloatStateAddr = 0x001D;
constexpr size_t kPlayerOneButtonsPressedAddr = 0x074A;
constexpr uint8_t kMovementDirectionAddr = 0x0045;
constexpr double kHorizontalSpeedCommittedThreshold = 0.15;
constexpr double kHorizontalSpeedResponseThreshold = 0.02;
constexpr double kVerticalSpeedResponseThreshold = 0.02;
constexpr uint64_t kResponseProbeTimeoutFrames = 12u;
constexpr uint8_t kSmbButtonRight = 1u << 0;
constexpr uint8_t kSmbButtonLeft = 1u << 1;
constexpr uint8_t kSmbButtonDown = 1u << 2;
constexpr uint8_t kSmbButtonUp = 1u << 3;
constexpr uint8_t kSmbButtonStart = 1u << 4;
constexpr uint8_t kSmbButtonSelect = 1u << 5;
constexpr uint8_t kSmbButtonB = 1u << 6;
constexpr uint8_t kSmbButtonA = 1u << 7;

uint64_t steadyClockNowNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

NesSuperMarioBrosResponseProbe::FacingDirection decodeDirection(uint8_t rawValue)
{
    switch (rawValue) {
        case 1u:
            return NesSuperMarioBrosResponseProbe::FacingDirection::Right;
        case 2u:
            return NesSuperMarioBrosResponseProbe::FacingDirection::Left;
        default:
            return NesSuperMarioBrosResponseProbe::FacingDirection::Unknown;
    }
}

NesSuperMarioBrosResponseProbe::FacingDirection decodeFacingDirection(
    const SmolnesRuntime::MemorySnapshot& snapshot)
{
    return decodeDirection(snapshot.cpuRam.at(kFacingDirectionAddr));
}

NesSuperMarioBrosResponseProbe::FacingDirection decodeMovementDirection(
    const SmolnesRuntime::MemorySnapshot& snapshot)
{
    return decodeDirection(snapshot.cpuRam.at(kMovementDirectionAddr));
}

bool isAirborneFloatState(uint8_t playerFloatState)
{
    return playerFloatState == 0x01u || playerFloatState == 0x02u;
}

bool isGroundedFloatState(uint8_t playerFloatState)
{
    return playerFloatState == 0x00u;
}

bool isGameplayStateEligible(const NesSuperMarioBrosState& state)
{
    return state.phase == SmbPhase::Gameplay && state.lifeState == SmbLifeState::Alive;
}

bool hasConflictingHorizontalInput(uint8_t controllerMask)
{
    return (controllerMask & SMOLNES_RUNTIME_BUTTON_LEFT) != 0
        && (controllerMask & SMOLNES_RUNTIME_BUTTON_RIGHT) != 0;
}

bool isDucking(const SmolnesRuntime::MemorySnapshot& snapshot)
{
    return snapshot.cpuRam.at(kDuckingStateAddr) == kDuckingStateActive;
}

uint8_t smbButtonsMaskFromRuntimeMask(uint8_t runtimeMask)
{
    uint8_t smbMask = 0u;
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_RIGHT) != 0) {
        smbMask |= kSmbButtonRight;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_LEFT) != 0) {
        smbMask |= kSmbButtonLeft;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_DOWN) != 0) {
        smbMask |= kSmbButtonDown;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_UP) != 0) {
        smbMask |= kSmbButtonUp;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_START) != 0) {
        smbMask |= kSmbButtonStart;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_SELECT) != 0) {
        smbMask |= kSmbButtonSelect;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_B) != 0) {
        smbMask |= kSmbButtonB;
    }
    if ((runtimeMask & SMOLNES_RUNTIME_BUTTON_A) != 0) {
        smbMask |= kSmbButtonA;
    }
    return smbMask;
}

bool isExpectedGameInputCopied(
    uint8_t baselineGameInputMask, uint8_t expectedGameInputMask, uint8_t currentGameInputMask)
{
    if (expectedGameInputMask == 0u) {
        return false;
    }

    if ((currentGameInputMask & expectedGameInputMask) != expectedGameInputMask) {
        return false;
    }

    if ((baselineGameInputMask & expectedGameInputMask) == 0u) {
        return true;
    }

    return currentGameInputMask != baselineGameInputMask;
}

bool isDuckEligible(const NesSuperMarioBrosState& baselineState)
{
    return !baselineState.airborne && baselineState.powerupState != SmbPowerupState::Small;
}

NesSuperMarioBrosResponseContext classifyJumpContext(const NesSuperMarioBrosState& baselineState)
{
    return std::abs(baselineState.horizontalSpeedNormalized) >= kHorizontalSpeedCommittedThreshold
        ? NesSuperMarioBrosResponseContext::RunningJump
        : NesSuperMarioBrosResponseContext::StandingJump;
}

std::optional<NesSuperMarioBrosResponseContext> classifyMoveLeftContext(
    const NesSuperMarioBrosState& baselineState,
    NesSuperMarioBrosResponseProbe::FacingDirection baselineFacingDirection,
    NesSuperMarioBrosResponseProbe::FacingDirection baselineMovementDirection)
{
    if (baselineState.airborne) {
        return std::nullopt;
    }

    if (baselineState.horizontalSpeedNormalized <= -kHorizontalSpeedCommittedThreshold) {
        return std::nullopt;
    }

    if (baselineState.horizontalSpeedNormalized >= kHorizontalSpeedCommittedThreshold
        || baselineFacingDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Right
        || baselineMovementDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Right) {
        return NesSuperMarioBrosResponseContext::GroundedTurnaround;
    }

    return NesSuperMarioBrosResponseContext::GroundedStart;
}

std::optional<NesSuperMarioBrosResponseContext> classifyMoveRightContext(
    const NesSuperMarioBrosState& baselineState,
    NesSuperMarioBrosResponseProbe::FacingDirection baselineFacingDirection,
    NesSuperMarioBrosResponseProbe::FacingDirection baselineMovementDirection)
{
    if (baselineState.airborne) {
        return std::nullopt;
    }

    if (baselineState.horizontalSpeedNormalized >= kHorizontalSpeedCommittedThreshold) {
        return std::nullopt;
    }

    if (baselineState.horizontalSpeedNormalized <= -kHorizontalSpeedCommittedThreshold
        || baselineFacingDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Left
        || baselineMovementDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Left) {
        return NesSuperMarioBrosResponseContext::GroundedTurnaround;
    }

    return NesSuperMarioBrosResponseContext::GroundedStart;
}

bool isJumpResponse(
    uint8_t baselineFloatState,
    const NesSuperMarioBrosState& baselineState,
    uint8_t currentFloatState,
    const NesSuperMarioBrosState& currentState)
{
    if (isGroundedFloatState(baselineFloatState) && isAirborneFloatState(currentFloatState)) {
        return true;
    }

    if (currentState.airborne && !baselineState.airborne) {
        return true;
    }

    if ((baselineState.verticalSpeedNormalized - currentState.verticalSpeedNormalized)
        > kVerticalSpeedResponseThreshold) {
        return true;
    }

    return currentState.playerYScreen < baselineState.playerYScreen;
}

bool isDuckResponse(bool baselineDucking, const SmolnesRuntime::MemorySnapshot& currentSnapshot)
{
    return !baselineDucking && isDucking(currentSnapshot);
}

struct MoveMilestones {
    bool acknowledge = false;
    bool commit = false;
    bool motion = false;
};

MoveMilestones detectMoveLeftMilestones(
    NesSuperMarioBrosResponseContext context,
    const NesSuperMarioBrosResponseProbe::FacingDirection baselineFacingDirection,
    const NesSuperMarioBrosResponseProbe::FacingDirection baselineMovementDirection,
    const NesSuperMarioBrosState& baselineState,
    NesSuperMarioBrosResponseProbe::FacingDirection currentFacingDirection,
    const NesSuperMarioBrosResponseProbe::FacingDirection currentMovementDirection,
    const NesSuperMarioBrosState& currentState)
{
    MoveMilestones milestones;

    if (context == NesSuperMarioBrosResponseContext::GroundedTurnaround) {
        if (currentFacingDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Left
            && baselineFacingDirection != NesSuperMarioBrosResponseProbe::FacingDirection::Left) {
            milestones.acknowledge = true;
        }
    }

    if (currentMovementDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Left
        && baselineMovementDirection != NesSuperMarioBrosResponseProbe::FacingDirection::Left) {
        milestones.commit = true;
    }

    if ((baselineState.horizontalSpeedNormalized - currentState.horizontalSpeedNormalized)
            > kHorizontalSpeedResponseThreshold
        && currentState.horizontalSpeedNormalized < -kHorizontalSpeedResponseThreshold) {
        milestones.motion = true;
    }

    if (currentState.absoluteX < baselineState.absoluteX
        && baselineState.horizontalSpeedNormalized >= -kHorizontalSpeedResponseThreshold) {
        milestones.motion = true;
    }

    if (context == NesSuperMarioBrosResponseContext::GroundedStart
        && (milestones.commit || milestones.motion)) {
        milestones.acknowledge = false;
    }

    return milestones;
}

MoveMilestones detectMoveRightMilestones(
    NesSuperMarioBrosResponseContext context,
    const NesSuperMarioBrosResponseProbe::FacingDirection baselineFacingDirection,
    const NesSuperMarioBrosResponseProbe::FacingDirection baselineMovementDirection,
    const NesSuperMarioBrosState& baselineState,
    NesSuperMarioBrosResponseProbe::FacingDirection currentFacingDirection,
    const NesSuperMarioBrosResponseProbe::FacingDirection currentMovementDirection,
    const NesSuperMarioBrosState& currentState)
{
    MoveMilestones milestones;

    if (context == NesSuperMarioBrosResponseContext::GroundedTurnaround) {
        if (currentFacingDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Right
            && baselineFacingDirection != NesSuperMarioBrosResponseProbe::FacingDirection::Right) {
            milestones.acknowledge = true;
        }
    }

    if (currentMovementDirection == NesSuperMarioBrosResponseProbe::FacingDirection::Right
        && baselineMovementDirection != NesSuperMarioBrosResponseProbe::FacingDirection::Right) {
        milestones.commit = true;
    }

    if ((currentState.horizontalSpeedNormalized - baselineState.horizontalSpeedNormalized)
            > kHorizontalSpeedResponseThreshold
        && currentState.horizontalSpeedNormalized > kHorizontalSpeedResponseThreshold) {
        milestones.motion = true;
    }

    if (currentState.absoluteX > baselineState.absoluteX
        && baselineState.horizontalSpeedNormalized <= kHorizontalSpeedResponseThreshold) {
        milestones.motion = true;
    }

    if (context == NesSuperMarioBrosResponseContext::GroundedStart
        && (milestones.commit || milestones.motion)) {
        milestones.acknowledge = false;
    }

    return milestones;
}

} // namespace

void NesSuperMarioBrosResponseProbe::reset()
{
    for (auto& probe : pendingProbes_) {
        probe.reset();
    }
    lastState_.reset();
    lastFacingDirection_ = FacingDirection::Unknown;
    lastFloatState_ = 0;
    lastMovementDirection_ = FacingDirection::Unknown;
    lastGameInputMask_ = 0;
    lastDucking_ = false;
    lastControllerMask_ = 0;
    lastControllerSequenceId_ = 0;
    pendingTelemetry_.clear();
    nextResponseEventId_ = 1;
}

std::optional<NesSuperMarioBrosResponseTelemetry> NesSuperMarioBrosResponseProbe::observeFrame(
    uint64_t frameId,
    const SmolnesRuntime::MemorySnapshot& snapshot,
    const std::optional<NesControllerTelemetry>& controllerTelemetry)
{
    const auto enqueueTelemetry =
        [this,
         frameId](const PendingProbe& pendingProbe, NesSuperMarioBrosResponseMilestone milestone) {
            pendingTelemetry_.push_back(
                NesSuperMarioBrosResponseTelemetry{
                    .kind = pendingProbe.kind,
                    .context = pendingProbe.context,
                    .milestone = milestone,
                    .responseEventId = nextResponseEventId_++,
                    .controllerSequenceId = pendingProbe.controllerSequenceId,
                    .controllerAppliedFrameId = pendingProbe.controllerAppliedFrameId,
                    .responseFrameId = frameId,
                    .controllerObservedTimestampNs = pendingProbe.controllerObservedTimestampNs,
                    .controllerRequestTimestampNs = pendingProbe.controllerRequestTimestampNs,
                    .controllerLatchTimestampNs = pendingProbe.controllerLatchTimestampNs,
                    .gameInputCopiedFrameId = pendingProbe.gameInputCopiedFrameId,
                    .gameInputCopiedTimestampNs = pendingProbe.gameInputCopiedTimestampNs,
                    .responseDetectedTimestampNs = steadyClockNowNs(),
                });
        };
    const auto isProbeComplete = [](const PendingProbe& pendingProbe) {
        if (pendingProbe.kind == NesSuperMarioBrosResponseKind::Jump
            || pendingProbe.kind == NesSuperMarioBrosResponseKind::Duck) {
            return pendingProbe.acknowledgeEmitted;
        }

        if (pendingProbe.context == NesSuperMarioBrosResponseContext::GroundedTurnaround) {
            return pendingProbe.acknowledgeEmitted && pendingProbe.commitEmitted
                && pendingProbe.motionEmitted;
        }

        return pendingProbe.commitEmitted || pendingProbe.motionEmitted;
    };
    const bool setupComplete = frameId >= getNesSuperMarioBrosSetupScriptEndFrame();
    const uint8_t currentFloatState = snapshot.cpuRam.at(kFloatStateAddr);
    const uint8_t currentGameInputMask = snapshot.cpuRam.at(kPlayerOneButtonsPressedAddr);
    const NesSuperMarioBrosState currentState = extractor_.extract(snapshot, setupComplete);
    const FacingDirection currentFacingDirection = decodeFacingDirection(snapshot);
    const FacingDirection currentMovementDirection = decodeMovementDirection(snapshot);

    for (auto& pendingProbe : pendingProbes_) {
        if (pendingProbe.has_value() && frameId > pendingProbe->deadlineFrameId) {
            pendingProbe.reset();
        }
    }

    if (controllerTelemetry.has_value()
        && controllerTelemetry->controllerSource == NesGameAdapterControllerSource::LiveInput
        && controllerTelemetry->controllerSequenceId.has_value()
        && controllerTelemetry->controllerRequestTimestampNs.has_value()
        && controllerTelemetry->controllerLatchTimestampNs.has_value()) {
        const uint64_t controllerSequenceId = controllerTelemetry->controllerSequenceId.value();
        uint64_t controllerAppliedFrameId =
            controllerTelemetry->controllerAppliedFrameId.value_or(frameId);
        if (controllerAppliedFrameId == 0) {
            controllerAppliedFrameId = frameId;
        }
        const uint8_t controllerMask = controllerTelemetry->resolvedControllerMask;
        if (controllerSequenceId != lastControllerSequenceId_) {
            const uint8_t addedMask = controllerMask & static_cast<uint8_t>(~lastControllerMask_);
            if (lastState_.has_value() && isGameplayStateEligible(lastState_.value())) {
                if ((addedMask & SMOLNES_RUNTIME_BUTTON_A) != 0
                    && isGroundedFloatState(lastFloatState_)) {
                    pendingProbes_[responseKindIndex(NesSuperMarioBrosResponseKind::Jump)] =
                        PendingProbe{
                            .kind = NesSuperMarioBrosResponseKind::Jump,
                            .context = classifyJumpContext(lastState_.value()),
                            .controllerSequenceId = controllerSequenceId,
                            .controllerAppliedFrameId = controllerAppliedFrameId,
                            .controllerObservedTimestampNs =
                                controllerTelemetry->controllerObservedTimestampNs.value_or(0),
                            .controllerRequestTimestampNs =
                                controllerTelemetry->controllerRequestTimestampNs.value(),
                            .controllerLatchTimestampNs =
                                controllerTelemetry->controllerLatchTimestampNs.value(),
                            .deadlineFrameId = frameId + kResponseProbeTimeoutFrames,
                            .baselineFacingDirection = lastFacingDirection_,
                            .baselineMovementDirection = lastMovementDirection_,
                            .baselineFloatState = lastFloatState_,
                            .baselineGameInputMask = lastGameInputMask_,
                            .expectedGameInputMask =
                                smbButtonsMaskFromRuntimeMask(SMOLNES_RUNTIME_BUTTON_A),
                            .baselineState = lastState_.value(),
                        };
                }
                if ((addedMask & SMOLNES_RUNTIME_BUTTON_DOWN) != 0
                    && isDuckEligible(lastState_.value())) {
                    pendingProbes_[responseKindIndex(NesSuperMarioBrosResponseKind::Duck)] =
                        PendingProbe{
                            .kind = NesSuperMarioBrosResponseKind::Duck,
                            .context = NesSuperMarioBrosResponseContext::GroundedDuck,
                            .controllerSequenceId = controllerSequenceId,
                            .controllerAppliedFrameId = controllerAppliedFrameId,
                            .controllerObservedTimestampNs =
                                controllerTelemetry->controllerObservedTimestampNs.value_or(0),
                            .controllerRequestTimestampNs =
                                controllerTelemetry->controllerRequestTimestampNs.value(),
                            .controllerLatchTimestampNs =
                                controllerTelemetry->controllerLatchTimestampNs.value(),
                            .deadlineFrameId = frameId + kResponseProbeTimeoutFrames,
                            .baselineFacingDirection = lastFacingDirection_,
                            .baselineMovementDirection = lastMovementDirection_,
                            .baselineFloatState = lastFloatState_,
                            .baselineGameInputMask = lastGameInputMask_,
                            .expectedGameInputMask =
                                smbButtonsMaskFromRuntimeMask(SMOLNES_RUNTIME_BUTTON_DOWN),
                            .baselineState = lastState_.value(),
                            .baselineDucking = lastDucking_,
                        };
                }
                if (!hasConflictingHorizontalInput(controllerMask)
                    && (addedMask & SMOLNES_RUNTIME_BUTTON_LEFT) != 0) {
                    const auto context = classifyMoveLeftContext(
                        lastState_.value(), lastFacingDirection_, lastMovementDirection_);
                    if (context.has_value()) {
                        pendingProbes_[responseKindIndex(NesSuperMarioBrosResponseKind::MoveLeft)] =
                            PendingProbe{
                                .kind = NesSuperMarioBrosResponseKind::MoveLeft,
                                .context = context.value(),
                                .controllerSequenceId = controllerSequenceId,
                                .controllerAppliedFrameId = controllerAppliedFrameId,
                                .controllerObservedTimestampNs =
                                    controllerTelemetry->controllerObservedTimestampNs.value_or(0),
                                .controllerRequestTimestampNs =
                                    controllerTelemetry->controllerRequestTimestampNs.value(),
                                .controllerLatchTimestampNs =
                                    controllerTelemetry->controllerLatchTimestampNs.value(),
                                .deadlineFrameId = frameId + kResponseProbeTimeoutFrames,
                                .baselineFacingDirection = lastFacingDirection_,
                                .baselineMovementDirection = lastMovementDirection_,
                                .baselineFloatState = lastFloatState_,
                                .baselineGameInputMask = lastGameInputMask_,
                                .expectedGameInputMask =
                                    smbButtonsMaskFromRuntimeMask(SMOLNES_RUNTIME_BUTTON_LEFT),
                                .baselineState = lastState_.value(),
                                .baselineDucking = lastDucking_,
                            };
                    }
                }
                if (!hasConflictingHorizontalInput(controllerMask)
                    && (addedMask & SMOLNES_RUNTIME_BUTTON_RIGHT) != 0) {
                    const auto context = classifyMoveRightContext(
                        lastState_.value(), lastFacingDirection_, lastMovementDirection_);
                    if (context.has_value()) {
                        pendingProbes_[responseKindIndex(
                            NesSuperMarioBrosResponseKind::MoveRight)] = PendingProbe{
                            .kind = NesSuperMarioBrosResponseKind::MoveRight,
                            .context = context.value(),
                            .controllerSequenceId = controllerSequenceId,
                            .controllerAppliedFrameId = controllerAppliedFrameId,
                            .controllerObservedTimestampNs =
                                controllerTelemetry->controllerObservedTimestampNs.value_or(0),
                            .controllerRequestTimestampNs =
                                controllerTelemetry->controllerRequestTimestampNs.value(),
                            .controllerLatchTimestampNs =
                                controllerTelemetry->controllerLatchTimestampNs.value(),
                            .deadlineFrameId = frameId + kResponseProbeTimeoutFrames,
                            .baselineFacingDirection = lastFacingDirection_,
                            .baselineMovementDirection = lastMovementDirection_,
                            .baselineFloatState = lastFloatState_,
                            .baselineGameInputMask = lastGameInputMask_,
                            .expectedGameInputMask =
                                smbButtonsMaskFromRuntimeMask(SMOLNES_RUNTIME_BUTTON_RIGHT),
                            .baselineState = lastState_.value(),
                            .baselineDucking = lastDucking_,
                        };
                    }
                }
            }

            lastControllerSequenceId_ = controllerSequenceId;
        }
        lastControllerMask_ = controllerMask;
    }

    if (isGameplayStateEligible(currentState)) {
        for (auto& pendingProbe : pendingProbes_) {
            if (!pendingProbe.has_value()) {
                continue;
            }

            if (!pendingProbe->gameInputCopied
                && isExpectedGameInputCopied(
                    pendingProbe->baselineGameInputMask,
                    pendingProbe->expectedGameInputMask,
                    currentGameInputMask)) {
                pendingProbe->gameInputCopied = true;
                pendingProbe->gameInputCopiedFrameId = frameId;
                pendingProbe->gameInputCopiedTimestampNs = steadyClockNowNs();
            }

            switch (pendingProbe->kind) {
                case NesSuperMarioBrosResponseKind::Jump:
                    if (!pendingProbe->acknowledgeEmitted
                        && isJumpResponse(
                            pendingProbe->baselineFloatState,
                            pendingProbe->baselineState,
                            currentFloatState,
                            currentState)) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Acknowledge);
                        pendingProbe->acknowledgeEmitted = true;
                    }
                    break;
                case NesSuperMarioBrosResponseKind::MoveLeft: {
                    const MoveMilestones milestones = detectMoveLeftMilestones(
                        pendingProbe->context,
                        pendingProbe->baselineFacingDirection,
                        pendingProbe->baselineMovementDirection,
                        pendingProbe->baselineState,
                        currentFacingDirection,
                        currentMovementDirection,
                        currentState);
                    if (!pendingProbe->acknowledgeEmitted && milestones.acknowledge) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Acknowledge);
                        pendingProbe->acknowledgeEmitted = true;
                    }
                    if (pendingProbe->context
                        != NesSuperMarioBrosResponseContext::GroundedTurnaround) {
                        if (!pendingProbe->commitEmitted && !pendingProbe->motionEmitted
                            && milestones.commit) {
                            enqueueTelemetry(
                                pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Commit);
                            pendingProbe->commitEmitted = true;
                        }
                        else if (!pendingProbe->motionEmitted && milestones.motion) {
                            enqueueTelemetry(
                                pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Motion);
                            pendingProbe->motionEmitted = true;
                        }
                        break;
                    }
                    if (!pendingProbe->commitEmitted && milestones.commit) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Commit);
                        pendingProbe->commitEmitted = true;
                    }
                    if (!pendingProbe->motionEmitted && milestones.motion) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Motion);
                        pendingProbe->motionEmitted = true;
                    }
                    break;
                }
                case NesSuperMarioBrosResponseKind::MoveRight: {
                    const MoveMilestones milestones = detectMoveRightMilestones(
                        pendingProbe->context,
                        pendingProbe->baselineFacingDirection,
                        pendingProbe->baselineMovementDirection,
                        pendingProbe->baselineState,
                        currentFacingDirection,
                        currentMovementDirection,
                        currentState);
                    if (!pendingProbe->acknowledgeEmitted && milestones.acknowledge) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Acknowledge);
                        pendingProbe->acknowledgeEmitted = true;
                    }
                    if (pendingProbe->context
                        != NesSuperMarioBrosResponseContext::GroundedTurnaround) {
                        if (!pendingProbe->commitEmitted && !pendingProbe->motionEmitted
                            && milestones.commit) {
                            enqueueTelemetry(
                                pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Commit);
                            pendingProbe->commitEmitted = true;
                        }
                        else if (!pendingProbe->motionEmitted && milestones.motion) {
                            enqueueTelemetry(
                                pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Motion);
                            pendingProbe->motionEmitted = true;
                        }
                        break;
                    }
                    if (!pendingProbe->commitEmitted && milestones.commit) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Commit);
                        pendingProbe->commitEmitted = true;
                    }
                    if (!pendingProbe->motionEmitted && milestones.motion) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Motion);
                        pendingProbe->motionEmitted = true;
                    }
                    break;
                }
                case NesSuperMarioBrosResponseKind::Duck:
                    if (!pendingProbe->acknowledgeEmitted
                        && isDuckResponse(pendingProbe->baselineDucking, snapshot)) {
                        enqueueTelemetry(
                            pendingProbe.value(), NesSuperMarioBrosResponseMilestone::Acknowledge);
                        pendingProbe->acknowledgeEmitted = true;
                    }
                    break;
            }

            if (isProbeComplete(pendingProbe.value())) {
                pendingProbe.reset();
            }
        }
    }

    lastState_ = currentState;
    lastFacingDirection_ = currentFacingDirection;
    lastFloatState_ = currentFloatState;
    lastMovementDirection_ = currentMovementDirection;
    lastGameInputMask_ = currentGameInputMask;
    lastDucking_ = isDucking(snapshot);
    if (pendingTelemetry_.empty()) {
        return std::nullopt;
    }

    const NesSuperMarioBrosResponseTelemetry telemetry = pendingTelemetry_.front();
    pendingTelemetry_.pop_front();
    return telemetry;
}

size_t NesSuperMarioBrosResponseProbe::responseKindIndex(NesSuperMarioBrosResponseKind kind)
{
    switch (kind) {
        case NesSuperMarioBrosResponseKind::Jump:
            return 0u;
        case NesSuperMarioBrosResponseKind::MoveLeft:
            return 1u;
        case NesSuperMarioBrosResponseKind::MoveRight:
            return 2u;
        case NesSuperMarioBrosResponseKind::Duck:
            return 3u;
    }

    return 0u;
}

} // namespace DirtSim
