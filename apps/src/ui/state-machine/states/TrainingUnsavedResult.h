#pragma once

#include "StateForward.h"
#include "ui/TrainingUnsavedResultView.h"
#include "ui/state-machine/Event.h"
#include <memory>
#include <vector>

namespace DirtSim {
namespace Ui {

namespace State {

struct TrainingUnsavedResult {
    TrainingUnsavedResult() = default;
    TrainingUnsavedResult(
        TrainingSpec lastTrainingSpec,
        bool hasTrainingSpec,
        Api::TrainingResult::Summary summary,
        std::vector<Api::TrainingResult::Candidate> candidates);
    ~TrainingUnsavedResult();
    TrainingUnsavedResult(const TrainingUnsavedResult&) = delete;
    TrainingUnsavedResult& operator=(const TrainingUnsavedResult&) = delete;
    TrainingUnsavedResult(TrainingUnsavedResult&&) = default;
    TrainingUnsavedResult& operator=(TrainingUnsavedResult&&) = default;

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);
    void updateAnimations();

    Any onEvent(const TrainingResultSaveClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingResultDiscardClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::TrainingResultSave::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::TrainingResultDiscard::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingBestSnapshotReceivedEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "TrainingUnsavedResult"; }

    std::unique_ptr<TrainingUnsavedResultView> view_;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
    Api::TrainingResult::Summary summary_{};
    std::vector<Api::TrainingResult::Candidate> candidates_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
