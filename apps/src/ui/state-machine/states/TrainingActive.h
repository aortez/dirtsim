#pragma once

#include "StateForward.h"
#include "ui/TrainingActiveView.h"
#include "ui/state-machine/Event.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace DirtSim {
namespace Ui {

namespace State {

struct TrainingActive {
    TrainingActive() = default;
    TrainingActive(
        TrainingSpec lastTrainingSpec,
        bool hasTrainingSpec,
        std::optional<Starfield::Snapshot> starfieldSnapshot = std::nullopt);
    ~TrainingActive();
    TrainingActive(const TrainingActive&) = delete;
    TrainingActive& operator=(const TrainingActive&) = delete;
    TrainingActive(TrainingActive&&) = default;
    TrainingActive& operator=(TrainingActive&&) = default;

    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);
    void updateAnimations();

    Any onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingBestPlaybackFrameReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingBestSnapshotReceivedEvent& evt, StateMachine& sm);
    Any onEvent(const Api::TrainingResult::Cwc& cwc, StateMachine& sm);
    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const QuitTrainingClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingPauseResumeClickedEvent& evt, StateMachine& sm);
    Any onEvent(const TrainingStreamConfigChangedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::TrainingQuit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiUpdateEvent& evt, StateMachine& sm);

    bool isTrainingResultModalVisible() const;

    static constexpr const char* name() { return "TrainingActive"; }

    Api::EvolutionProgress progress;
    std::unique_ptr<TrainingActiveView> view_;
    TrainingSpec lastTrainingSpec_;
    bool hasTrainingSpec_ = false;
    std::optional<Starfield::Snapshot> starfieldSnapshot_;
    bool trainingPaused_ = false;
    bool hasPlottedRobustBestFitness_ = false;
    float plottedRobustBestFitness_ = 0.0f;
    std::vector<float> plotBestSeries_;
    std::vector<uint8_t> plotBestSeriesRobustHighMask_;
    uint64_t lastPlottedRobustEvaluationCount_ = 0;
    int lastPlottedCompletedGeneration_ = -1;
    uint64_t progressEventCount_ = 0;
    uint64_t renderMessageCount_ = 0;
    std::chrono::steady_clock::time_point lastRenderRateLog_;
    uint64_t uiLoopCount_ = 0;
    std::chrono::steady_clock::time_point lastUiLoopLog_;
    std::chrono::steady_clock::time_point lastProgressRateLog_;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
