#pragma once

#include <cstdint>
#include <zpp_bits.h>

namespace DirtSim {

enum class NesSuperMarioBrosResponseKind : uint8_t {
    Jump = 0,
    MoveLeft = 1,
    MoveRight = 2,
    Duck = 3,
};

enum class NesSuperMarioBrosResponseContext : uint8_t {
    StandingJump = 0,
    RunningJump = 1,
    GroundedStart = 2,
    GroundedTurnaround = 3,
    GroundedDuck = 4,
};

enum class NesSuperMarioBrosResponseMilestone : uint8_t {
    Acknowledge = 0,
    Commit = 1,
    Motion = 2,
};

const char* toString(NesSuperMarioBrosResponseKind kind);
const char* toString(NesSuperMarioBrosResponseContext context);
const char* toString(NesSuperMarioBrosResponseMilestone milestone);

struct NesSuperMarioBrosResponseTelemetry {
    NesSuperMarioBrosResponseKind kind = NesSuperMarioBrosResponseKind::Jump;
    NesSuperMarioBrosResponseContext context = NesSuperMarioBrosResponseContext::StandingJump;
    NesSuperMarioBrosResponseMilestone milestone = NesSuperMarioBrosResponseMilestone::Acknowledge;
    uint64_t responseEventId = 0;
    uint64_t controllerSequenceId = 0;
    uint64_t controllerAppliedFrameId = 0;
    uint64_t responseFrameId = 0;
    uint64_t controllerObservedTimestampNs = 0;
    uint64_t controllerRequestTimestampNs = 0;
    uint64_t controllerLatchTimestampNs = 0;
    uint64_t gameInputCopiedFrameId = 0;
    uint64_t gameInputCopiedTimestampNs = 0;
    uint64_t responseDetectedTimestampNs = 0;

    using serialize = zpp::bits::members<13>;
};

} // namespace DirtSim
