#pragma once

#include "StateForward.h"
#include "ui/SimPlayground.h"
#include "ui/state-machine/Event.h"
#include <memory>

namespace DirtSim {

struct WorldData;

namespace Ui {

class SimPlayground;

namespace State {

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

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const PhysicsSettingsReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::DrawDebugToggle::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::PixelRendererToggle::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::RenderModeSelect::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimPause::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);

    static constexpr const char* name() { return "SimRunning"; }
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
