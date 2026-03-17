#pragma once

#include "core/scenarios/nes/NesControllerTelemetry.h"
#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "core/scenarios/nes/NesSuperMarioBrosResponseTelemetry.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace DirtSim {

class NesSuperMarioBrosResponseProbe {
public:
    enum class FacingDirection : uint8_t {
        Unknown = 0,
        Right = 1,
        Left = 2,
    };

    void reset();
    std::optional<NesSuperMarioBrosResponseTelemetry> observeFrame(
        uint64_t frameId,
        const SmolnesRuntime::MemorySnapshot& snapshot,
        const std::optional<NesControllerTelemetry>& controllerTelemetry);

private:
    struct PendingProbe {
        NesSuperMarioBrosResponseKind kind = NesSuperMarioBrosResponseKind::Jump;
        NesSuperMarioBrosResponseContext context = NesSuperMarioBrosResponseContext::StandingJump;
        uint64_t controllerSequenceId = 0;
        uint64_t controllerAppliedFrameId = 0;
        uint64_t controllerObservedTimestampNs = 0;
        uint64_t controllerRequestTimestampNs = 0;
        uint64_t controllerLatchTimestampNs = 0;
        uint64_t deadlineFrameId = 0;
        FacingDirection baselineFacingDirection = FacingDirection::Unknown;
        FacingDirection baselineMovementDirection = FacingDirection::Unknown;
        uint8_t baselineFloatState = 0;
        uint8_t baselineGameInputMask = 0;
        uint8_t expectedGameInputMask = 0;
        NesSuperMarioBrosState baselineState;
        bool baselineDucking = false;
        bool acknowledgeEmitted = false;
        bool commitEmitted = false;
        uint64_t gameInputCopiedFrameId = 0;
        uint64_t gameInputCopiedTimestampNs = 0;
        bool gameInputCopied = false;
        bool motionEmitted = false;
    };

    static size_t responseKindIndex(NesSuperMarioBrosResponseKind kind);

    std::array<std::optional<PendingProbe>, 4> pendingProbes_{};
    std::deque<NesSuperMarioBrosResponseTelemetry> pendingTelemetry_{};
    std::optional<NesSuperMarioBrosState> lastState_;
    FacingDirection lastFacingDirection_ = FacingDirection::Unknown;
    uint8_t lastFloatState_ = 0;
    FacingDirection lastMovementDirection_ = FacingDirection::Unknown;
    uint8_t lastGameInputMask_ = 0;
    bool lastDucking_ = false;
    uint8_t lastControllerMask_ = 0;
    uint64_t lastControllerSequenceId_ = 0;
    uint64_t nextResponseEventId_ = 1;
    NesSuperMarioBrosRamExtractor extractor_;
};

} // namespace DirtSim
