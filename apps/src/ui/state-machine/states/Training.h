#pragma once

#include "StateForward.h"
#include "core/api/UiUpdateEvent.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/TrainingResult.h"
#include "ui/TrainingActiveView.h"
#include "ui/TrainingIdleView.h"
#include "ui/TrainingUnsavedResultView.h"
#include "ui/state-machine/Event.h"
#include <chrono>
#include <memory>
#include <vector>

typedef struct _lv_event_t lv_event_t;

namespace DirtSim {
namespace Ui {

namespace State {

/**
 * Training idle state - displays training config panels and waits for start.
 */
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
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const ViewBestButtonClickedEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "Training"; }

    std::unique_ptr<TrainingIdleView> view_;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
};

/**
 * Training active state - modal training UI with limited actions.
 */
struct TrainingActive {
    TrainingActive() = default;
    TrainingActive(TrainingSpec lastTrainingSpec, bool hasTrainingSpec);
    ~TrainingActive();
    TrainingActive(const TrainingActive&) = delete;
    TrainingActive& operator=(const TrainingActive&) = delete;
    TrainingActive(TrainingActive&&) = default;
    TrainingActive& operator=(TrainingActive&&) = default;

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);
    void updateAnimations();

    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingBestSnapshotReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const Api::TrainingResult::Cwc& cwc, StateMachine& sm);
    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const QuitTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingPauseResumeClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::TrainingQuit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "Training"; }

    Api::EvolutionProgress progress;
    std::unique_ptr<TrainingActiveView> view_;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
    bool trainingPaused_ = false;
    uint64_t progressEventCount_ = 0;
    uint64_t renderMessageCount_ = 0;
    std::chrono::steady_clock::time_point lastRenderRateLog_;
    uint64_t uiLoopCount_ = 0;
    std::chrono::steady_clock::time_point lastUiLoopLog_;
    std::chrono::steady_clock::time_point lastProgressRateLog_;
};

/**
 * Training unsaved-result state - modal result save/discard flow.
 */
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
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);
    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingBestSnapshotReceivedEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "Training"; }

    std::unique_ptr<TrainingUnsavedResultView> view_;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
    Api::TrainingResult::Summary summary_{};
    std::vector<Api::TrainingResult::Candidate> candidates_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
