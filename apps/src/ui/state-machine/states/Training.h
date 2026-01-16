#pragma once

#include "StateForward.h"
#include "core/api/UiUpdateEvent.h"
#include "server/api/EvolutionProgress.h"
#include "ui/TrainingView.h"
#include "ui/state-machine/Event.h"
#include <memory>

typedef struct _lv_event_t lv_event_t;

namespace DirtSim {
namespace Ui {

namespace State {

/**
 * Training state - displays evolution progress and controls.
 *
 * Shows progress bars for generation and evaluation, fitness statistics,
 * and provides controls to pause, resume, stop, or view the best genome.
 */
struct Training {
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StartEvolutionButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const ViewBestButtonClickedEvent& evt, StateMachine& sm);

    static constexpr const char* name() { return "Training"; }

    Api::EvolutionProgress progress;
    std::unique_ptr<TrainingView> view_;
    StateMachine* sm_ = nullptr;
    bool evolutionStarted_ = false;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
