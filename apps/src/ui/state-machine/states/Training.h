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
    void updateAnimations();

    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StartEvolutionButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const StopTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const QuitTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const Api::TrainingResult::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm);
    Any onEvent(const TrainingResultSaveClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingResultDiscardClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const ViewBestButtonClickedEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "Training"; }

    Api::EvolutionProgress progress;
    std::unique_ptr<TrainingView> view_;
    bool evolutionStarted_ = false;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
