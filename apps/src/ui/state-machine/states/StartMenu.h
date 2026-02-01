#pragma once

#include "StateForward.h"
#include "ui/controls/SparklingDuckButton.h"
#include "ui/controls/StartMenuCorePanel.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {

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
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StartButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const NextFractalClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimRun::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);

    // Update background animations (fractal).
    void updateAnimations();

    static constexpr const char* name() { return "StartMenu"; }

private:
    static void onDisplayResized(lv_event_t* e);
    static void onTouchEvent(lv_event_t* e);
    void updateInfoPanelVisibility(RailMode mode);

    StateMachine* sm_ = nullptr;                       // State machine reference for callbacks.
    std::unique_ptr<SparklingDuckButton> startButton_; // Animated start button.
    std::unique_ptr<StartMenuCorePanel> corePanel_;    // Core controls panel (quit, etc.).
    lv_obj_t* touchDebugLabel_ = nullptr;              // Touch coordinate debug display.
    lv_obj_t* infoPanel_ = nullptr;                    // Bottom-left info panel container.
    lv_obj_t* infoLabel_ = nullptr;                    // Fractal info label.
    int updateFrameCount_ = 0;                         // Frame counter for periodic logging.
    int labelUpdateCounter_ = 0;                       // Frame counter for label updates (~1/sec).
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
