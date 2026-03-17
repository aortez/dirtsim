#pragma once

#include "StateForward.h"
#include "ui/SimPlayground.h"
#include "ui/state-machine/Event.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace DirtSim {

struct WorldData;

namespace Ui {
namespace State {

struct LatencyAccumulator {
    double max = 0.0;
    double min = 1e9;
    double total = 0.0;
    double totalSq = 0.0;

    double average(uint32_t sampleCount) const;
    void record(double sample);
    void reset();
    double stddev(uint32_t sampleCount) const;
};

struct DisplayFrameStagingStats {
    uint32_t handledAfterRenderCount = 0;
    uint32_t handledBeforeRenderCount = 0;
    uint32_t handledOtherPhaseCount = 0;
    uint32_t sampleCount = 0;
    LatencyAccumulator receiveToUiApplyMs;
    LatencyAccumulator timerStartToFlushMs;
    LatencyAccumulator uiApplyToTimerStartMs;

    void reset();
};

struct LiveInputLatencyStats {
    uint32_t coalescedCount = 0;
    uint32_t inputLatencyCount = 0;
    uint32_t observedLatencyCount = 0;
    LatencyAccumulator latchToDisplayMs;
    LatencyAccumulator observedToDisplayMs;
    LatencyAccumulator observedToLatchMs;
    LatencyAccumulator observedToRequestMs;
    LatencyAccumulator requestToDisplayMs;
    LatencyAccumulator requestToLatchMs;

    void reset();
};

struct SmbResponseLatencyStats {
    std::array<uint32_t, 11> bucketCounts{};
    uint32_t coalescedCount = 0;
    uint32_t gameInputLatencyCount = 0;
    uint32_t gameInputObservedLatencyCount = 0;
    uint32_t latencyCount = 0;
    uint32_t observedLatencyCount = 0;
    LatencyAccumulator detectedToDisplayMs;
    LatencyAccumulator framesAfterLatch;
    LatencyAccumulator framesToGameInput;
    LatencyAccumulator gameInputToDetectedMs;
    LatencyAccumulator latchToDetectedMs;
    LatencyAccumulator latchToDisplayMs;
    LatencyAccumulator latchToGameInputMs;
    LatencyAccumulator observedToDetectedMs;
    LatencyAccumulator observedToDisplayMs;
    LatencyAccumulator observedToGameInputMs;
    LatencyAccumulator requestToDisplayMs;

    void reset();
};

/**
 * @brief Simulation running state - active display and interaction.
 */
struct SimRunning {
    std::unique_ptr<WorldData> worldData;       // Local copy of world data for rendering.
    std::unique_ptr<SimPlayground> playground_; // Coordinates all UI components.
    Scenario::EnumType scenarioId = Scenario::EnumType::Empty;

    bool debugDrawEnabled = false;
    std::optional<UiApi::MouseButton> activeMouseButton;

    // UI FPS tracking.
    std::chrono::steady_clock::time_point lastFrameTime;
    double measuredUiFps = 0.0;
    double smoothedUiFps = 0.0;
    uint64_t skippedFrames = 0;

    // Round-trip timing (state_get request → UiUpdateEvent received).
    std::chrono::steady_clock::time_point lastStateGetSentTime;
    double lastRoundTripMs = 0.0;
    double smoothedRoundTripMs = 0.0;
    uint64_t updateCount = 0;
    bool stateGetPending = false;

    // Frame interval jitter tracking (reset every 1000 updates).
    LatencyAccumulator frameIntervalMs;
    uint32_t frameIntervalCount = 0;

    // Queue delay tracking (reset every 1000 frames).
    LatencyAccumulator queueDelayMs;
    uint32_t queueDelayCount = 0;

    // Server send -> UI receive timing (reset every 1000 frames).
    LatencyAccumulator transportDelayMs;
    uint32_t transportDelayCount = 0;

    // Server send -> display flush timing (reset every 1000 displayed frames).
    std::chrono::steady_clock::time_point pendingDisplayReceiveTime;
    std::chrono::steady_clock::time_point pendingDisplayUiApplyTime;
    std::chrono::steady_clock::time_point pendingDisplayTimerHandlerStartTime;
    uint64_t pendingDisplayServerSendTimestampNs = 0;
    uint64_t pendingDisplayControllerObservedTimestampNs = 0;
    uint64_t pendingDisplayControllerLatchTimestampNs = 0;
    uint64_t pendingDisplayControllerRequestTimestampNs = 0;
    uint64_t pendingDisplayControllerSequenceId = 0;
    uint8_t pendingDisplayHandlePhase = 0;
    bool pendingDisplayMeasurement = false;
    LatencyAccumulator displayLatencyMs;
    uint32_t displayLatencyCount = 0;
    uint32_t displayCoalescedCount = 0;
    uint64_t lastDisplayedControllerSequenceId = 0;
    DisplayFrameStagingStats displayFrameStagingStats;

    // Live input timing for newly displayed controller changes (reset every 20 changes).
    LiveInputLatencyStats liveInputLatencyStats;
    uint64_t pendingDisplaySmbResponseAppliedFrameId = 0;
    uint64_t pendingDisplaySmbResponseDetectedTimestampNs = 0;
    uint64_t pendingDisplaySmbResponseEventId = 0;
    uint64_t pendingDisplaySmbResponseFrameId = 0;
    uint64_t pendingDisplaySmbResponseGameInputCopiedFrameId = 0;
    uint64_t pendingDisplaySmbResponseGameInputCopiedTimestampNs = 0;
    uint64_t pendingDisplaySmbResponseObservedTimestampNs = 0;
    uint64_t pendingDisplaySmbResponseLatchTimestampNs = 0;
    uint64_t pendingDisplaySmbResponseRequestTimestampNs = 0;
    uint64_t pendingDisplaySmbResponseSequenceId = 0;
    bool pendingDisplaySmbResponseMeasurement = false;
    SmbResponseLatencyStats smbResponseLatencyStats;
    uint64_t lastDisplayedSmbResponseEventId = 0;

    // Performance stats interval tracking (reset every 1000 updates).
    double lastParseTotal = 0.0;
    uint32_t lastParseCount = 0;
    double lastRenderTotal = 0.0;
    uint32_t lastRenderCount = 0;
    double lastCopyTotal = 0.0;
    uint32_t lastCopyCount = 0;
    double lastUpdateTotal = 0.0;
    uint32_t lastUpdateCount = 0;

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PhysicsSettingsReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const UserSettingsUpdatedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::DrawDebugToggle::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PlantSeed::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PixelRendererToggle::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::RenderModeSelect::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimPause::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    void onDisplayTimerHandlerStart(std::chrono::steady_clock::time_point startTime);
    void onDisplayFlush(std::chrono::steady_clock::time_point flushTime);

    static constexpr const char* name() { return "SimRunning"; }
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
