#pragma once

#include "StateForward.h"
#include "core/ScenarioId.h"
#include "ui/controls/SparklingDuckButton.h"
#include "ui/controls/StartMenuCorePanel.h"
#include "ui/controls/StartMenuSettingsPanel.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>
#include <memory>
#include <optional>

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
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const StartButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const StartMenuIdleTimeoutEvent& evt, StateMachine& sm);
    Any onEvent(const TrainButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UserSettingsUpdatedEvent& evt, StateMachine& sm);
    Any onEvent(const NextFractalClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::SimRun::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm);

    // Update background animations (fractal).
    void updateAnimations();

    static constexpr const char* name() { return "StartMenu"; }

private:
    static void onDisplayResized(lv_event_t* e);
    static void onTouchEvent(lv_event_t* e);
    void updateInfoPanelVisibility(RailMode mode);
    Any startSimulation(StateMachine& sm, std::optional<Scenario::EnumType> scenarioId);

    StateMachine* sm_ = nullptr;                       // State machine reference for callbacks.
    std::unique_ptr<SparklingDuckButton> startButton_; // Animated start button.
    std::unique_ptr<StartMenuCorePanel> corePanel_;    // Core controls panel (quit, etc.).
    std::unique_ptr<StartMenuSettingsPanel> settingsPanel_; // Settings controls panel.
    lv_obj_t* touchDebugLabel_ = nullptr;                   // Touch coordinate debug display.
    lv_obj_t* infoPanel_ = nullptr;                         // Bottom-left info panel container.
    lv_obj_t* infoLabel_ = nullptr;                         // Fractal info label.
    int updateFrameCount_ = 0;                              // Frame counter for periodic logging.
    int labelUpdateCounter_ = 0; // Frame counter for label updates (~1/sec).
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
