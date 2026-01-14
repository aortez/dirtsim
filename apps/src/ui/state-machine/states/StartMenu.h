#pragma once

#include "StateForward.h"
#include "ui/controls/NetworkDiagnosticsPanel.h"
#include "ui/controls/SparklingDuckButton.h"
#include "ui/controls/StartMenuCorePanel.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {

// Forward declaration.
class JuliaFractal;

namespace State {

/**
 * @brief Start menu state - connected to server, ready to start simulation.
 * Shows simulation controls (start, scenario selection, etc.).
 */
struct StartMenu {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StartButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimRun::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);

    // Update background animations (fractal).
    void updateAnimations();

    static constexpr const char* name() { return "StartMenu"; }

private:
    static void onDisplayResized(lv_event_t* e);
    static void onNextFractalClicked(lv_event_t* e);
    static void onTouchEvent(lv_event_t* e);

    StateMachine* sm_ = nullptr;                       // State machine reference for callbacks.
    JuliaFractal* fractal_ = nullptr;                  // Fractal background animation.
    std::unique_ptr<SparklingDuckButton> startButton_; // Animated start button.
    std::unique_ptr<NetworkDiagnosticsPanel> networkPanel_; // Network diagnostics panel.
    std::unique_ptr<StartMenuCorePanel> corePanel_;         // Core controls panel (quit, etc.).
    lv_obj_t* touchDebugLabel_ = nullptr;                   // Touch coordinate debug display.
    lv_obj_t* infoPanel_ = nullptr;                         // Bottom-left info panel container.
    lv_obj_t* infoLabel_ = nullptr;                         // Fractal info label.
    lv_obj_t* nextFractalButton_ = nullptr;                 // Button to advance fractal.
    int updateFrameCount_ = 0;                              // Frame counter for periodic logging.
    int labelUpdateCounter_ = 0; // Frame counter for label updates (~1/sec).
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
