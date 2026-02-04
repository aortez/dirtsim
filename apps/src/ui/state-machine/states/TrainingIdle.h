#pragma once

#include "StateForward.h"
#include "ui/TrainingIdleView.h"
#include "ui/state-machine/Event.h"
#include <memory>

namespace DirtSim {
namespace Ui {

namespace State {

struct TrainingIdle {
    TrainingIdle() = default;
    TrainingIdle(TrainingSpec lastTrainingSpec, bool hasTrainingSpec);
    ~TrainingIdle();
    TrainingIdle(const TrainingIdle&) = delete;
    TrainingIdle& operator=(const TrainingIdle&) = delete;
    TrainingIdle(TrainingIdle&&) = default;
    TrainingIdle& operator=(TrainingIdle&&) = default;

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);
    void updateAnimations();

    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingBestSnapshotReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StartEvolutionButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const StopTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const QuitTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::GenomeBrowserOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::GenomeDetailLoad::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::GenomeDetailOpen::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingConfigShowEvolution::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingQuit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& sm);
    Any onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm);
    Any onEvent(const GenomeLoadClickedEvent& evt, StateMachine& sm);
    Any onEvent(const OpenTrainingGenomeBrowserClickedEvent& evt, StateMachine& sm);
    Any onEvent(const GenomeAddToTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const ViewBestButtonClickedEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "TrainingIdle"; }

    std::unique_ptr<TrainingIdleView> view_;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
