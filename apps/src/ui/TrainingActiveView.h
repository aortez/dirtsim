#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "ui/UserSettings.h"
#include "ui/rendering/Starfield.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;

typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

struct WorldData;

namespace Api {
struct EvolutionProgress;
struct FitnessBreakdownReport;
} // namespace Api

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class CellRenderer;
class EventSink;
class ScenarioControlsBase;
class Starfield;
class TimeSeriesPlotWidget;
class UiComponentManager;

class TrainingActiveView {
public:
    TrainingActiveView(
        UiComponentManager* uiManager,
        EventSink& eventSink,
        Network::WebSocketServiceInterface* wsService,
        UserSettings& userSettings,
        const Starfield::Snapshot* starfieldSnapshot = nullptr);
    ~TrainingActiveView();

    void updateProgress(const Api::EvolutionProgress& progress);
    void updateFitnessPlots(
        const std::vector<float>& robustFitnessSeries,
        const std::vector<float>& averageFitnessSeries,
        const std::vector<uint8_t>& robustHighMask);
    void clearFitnessPlots();
    void updateAnimations();

    void renderWorld(const WorldData& worldData);
    void updateBestSnapshot(
        const WorldData& worldData,
        double fitness,
        int generation,
        int commandsAccepted,
        int commandsRejected,
        const std::vector<std::pair<std::string, int>>& topCommandSignatures,
        const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures,
        const std::optional<Api::FitnessBreakdownReport>& fitnessBreakdown);
    void updateBestPlaybackFrame(const WorldData& worldData, double fitness, int generation);

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted(GenomeId bestGenomeId);
    void setTrainingPaused(bool paused);
    void setStreamIntervalMs(int value);
    void setBestPlaybackEnabled(bool enabled);
    void setBestPlaybackIntervalMs(int value);
    void updateScenarioConfig(Scenario::EnumType scenarioId, const ScenarioConfig& config);
    void showScenarioControlsOverlay();

    bool isTrainingResultModalVisible() const;
    Starfield::Snapshot captureStarfieldSnapshot() const;

private:
    static std::vector<float> buildCpuCoreSeries(const Api::EvolutionProgress& progress);
    static std::vector<float> buildDistributionSeries(const Api::EvolutionProgress& progress);

    void createUI();
    void destroyUI();
    void createActiveUI(int displayWidth, int displayHeight);
    void renderBestWorld();
    void scheduleBestRender();
    static void renderBestWorldAsync(void* data);
    void createStreamPanel(lv_obj_t* parent);
    void createScenarioControlsOverlay();
    void hideScenarioControlsOverlay();
    void refreshScenarioControlsOverlay();
    void updateScenarioButtonState();

    static void onStreamIntervalChanged(lv_event_t* e);
    static void onBestPlaybackToggled(lv_event_t* e);
    static void onBestPlaybackIntervalChanged(lv_event_t* e);
    static void onStopTrainingClicked(lv_event_t* e);
    static void onPauseResumeClicked(lv_event_t* e);
    static void onScenarioControlsClicked(lv_event_t* e);

    bool evolutionStarted_ = false;
    UiComponentManager* uiManager_ = nullptr;
    EventSink& eventSink_;
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    UserSettings& userSettings_;

    lv_obj_t* bestAllTimeLabel_ = nullptr;
    lv_obj_t* bestThisGenLabel_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* contentRow_ = nullptr;
    lv_obj_t* cpuLabel_ = nullptr;
    lv_obj_t* evalLabel_ = nullptr;
    lv_obj_t* evaluationBar_ = nullptr;
    lv_obj_t* genLabel_ = nullptr;
    lv_obj_t* genomeCountLabel_ = nullptr;
    lv_obj_t* generationBar_ = nullptr;
    lv_obj_t* statsPanel_ = nullptr;
    lv_obj_t* etaLabel_ = nullptr;
    lv_obj_t* simTimeLabel_ = nullptr;
    lv_obj_t* speedupLabel_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;
    lv_obj_t* totalTimeLabel_ = nullptr;
    lv_obj_t* worldContainer_ = nullptr;
    lv_obj_t* mainLayout_ = nullptr;
    lv_obj_t* longTermPanel_ = nullptr;
    lv_obj_t* parallelismLabel_ = nullptr;
    lv_obj_t* bottomRow_ = nullptr;
    lv_obj_t* fitnessPlotsPanel_ = nullptr;
    lv_obj_t* fitnessPlotsRow_ = nullptr;
    lv_obj_t* streamPanel_ = nullptr;
    int progressUiUpdateCount_ = 0;
    std::chrono::steady_clock::time_point lastLabelStateLog_{};
    std::chrono::steady_clock::time_point lastProgressUiLog_{};
    std::chrono::steady_clock::time_point lastStatsInvalidate_{};
    lv_obj_t* streamIntervalStepper_ = nullptr;
    lv_obj_t* bestPlaybackToggle_ = nullptr;
    lv_obj_t* bestPlaybackIntervalStepper_ = nullptr;
    lv_obj_t* pauseResumeButton_ = nullptr;
    lv_obj_t* pauseResumeLabel_ = nullptr;
    lv_obj_t* scenarioControlsButton_ = nullptr;
    lv_obj_t* scenarioControlsOverlay_ = nullptr;
    lv_obj_t* scenarioControlsOverlayTitle_ = nullptr;
    lv_obj_t* scenarioControlsOverlayContent_ = nullptr;
    lv_obj_t* stopTrainingButton_ = nullptr;

    lv_obj_t* bestWorldContainer_ = nullptr;
    lv_obj_t* bestFitnessLabel_ = nullptr;
    lv_obj_t* bestCommandSummaryLabel_ = nullptr;
    lv_obj_t* bestFitnessBreakdownLabel_ = nullptr;

    std::unique_ptr<CellRenderer> renderer_;
    std::unique_ptr<CellRenderer> bestRenderer_;
    std::unique_ptr<ScenarioControlsBase> scenarioControls_;
    std::unique_ptr<TimeSeriesPlotWidget> cpuCorePlot_;
    std::unique_ptr<Starfield> starfield_;
    std::unique_ptr<TimeSeriesPlotWidget> bestFitnessPlot_;
    std::unique_ptr<TimeSeriesPlotWidget> lastGenerationDistributionPlot_;
    const Starfield::Snapshot* starfieldSnapshot_ = nullptr;

    std::unique_ptr<WorldData> bestWorldData_;
    std::unique_ptr<WorldData> bestSnapshotWorldData_;
    ScenarioConfig currentScenarioConfig_ = Config::Empty{};
    Scenario::EnumType currentScenarioId_ = Scenario::EnumType::Empty;
    Scenario::EnumType scenarioControlsScenarioId_ = Scenario::EnumType::Empty;
    bool hasScenarioState_ = false;
    bool scenarioControlsOverlayVisible_ = false;
    double bestFitness_ = 0.0;
    int bestGeneration_ = 0;
    double bestSnapshotFitness_ = 0.0;
    int bestSnapshotGeneration_ = 0;
    bool hasBestSnapshot_ = false;
    bool hasShownBestSnapshot_ = false;
    std::shared_ptr<std::atomic<bool>> alive_;
};

} // namespace Ui
} // namespace DirtSim
