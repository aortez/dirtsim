#pragma once

#include "StateForward.h"
#include "server/api/EvolutionProgress.h"
#include "ui/state-machine/Event.h"
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Training state - displays evolution progress and controls.
 *
 * Shows progress bars for generation and evaluation, fitness statistics,
 * and provides controls to pause, resume, stop, or view the best genome.
 */
struct Training {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);

    static constexpr const char* name() { return "Training"; }

    // Current evolution progress (updated by EvolutionProgress events).
    Api::EvolutionProgress progress;

    // UI elements (created in onEnter, cleaned up in onExit).
    lv_obj_t* container_ = nullptr;
    lv_obj_t* generationBar_ = nullptr;
    lv_obj_t* evaluationBar_ = nullptr;
    lv_obj_t* genLabel_ = nullptr;
    lv_obj_t* evalLabel_ = nullptr;
    lv_obj_t* bestThisGenLabel_ = nullptr;
    lv_obj_t* bestAllTimeLabel_ = nullptr;
    lv_obj_t* averageLabel_ = nullptr;
    lv_obj_t* stopButton_ = nullptr;

    // Stored for callbacks.
    StateMachine* sm_ = nullptr;

private:
    static void onStopClicked(lv_event_t* e);
    void updateProgressDisplay();
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
