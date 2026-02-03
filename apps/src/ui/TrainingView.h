#pragma once

#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/TrainingResult.h"
#include "ui/UserSettings.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <variant>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

struct WorldData;

namespace Api {
struct EvolutionProgress;
}

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class CellRenderer;
class EvolutionControls;
class EventSink;
class GenomeBrowserPanel;
class Starfield;
class TrainingConfigPanel;
class TrainingResultBrowserPanel;
class UiComponentManager;
class ExpandablePanel;

/**
 * Coordinates the training view display.
 *
 * TrainingView encapsulates all LVGL widget management for the evolution
 * training UI. It creates its own dedicated display container and manages
 * progress bars, statistics labels, and control buttons.
 *
 * Similar to SimPlayground, this separates UI implementation details from
 * the state machine logic.
 */
class TrainingView {
public:
    enum class Layout {
        Idle,
        Active,
        UnsavedResult,
    };

    enum class TrainingConfigView {
        None,
        Evolution,
        Population,
    };

    explicit TrainingView(
        Layout layout,
        UiComponentManager* uiManager,
        EventSink& eventSink,
        Network::WebSocketServiceInterface* wsService,
        UserSettings& userSettings);
    ~TrainingView();

    void updateProgress(const Api::EvolutionProgress& progress);
    void updateAnimations();

    void renderWorld(const WorldData& worldData);
    void updateBestSnapshot(const WorldData& worldData, double fitness, int generation);

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted(GenomeId bestGenomeId);
    void showTrainingResultModal(
        const Api::TrainingResult::Summary& summary,
        const std::vector<Api::TrainingResult::Candidate>& candidates);
    void hideTrainingResultModal();
    bool isTrainingResultModalVisible() const;
    std::vector<GenomeId> getTrainingResultSaveIds() const;
    std::vector<GenomeId> getTrainingResultSaveIdsForCount(int count) const;

    void clearPanelContent();
    void createCorePanel();
    void createGenomeBrowserPanel();
    void createTrainingConfigPanel();
    void createTrainingResultBrowserPanel();
    Result<std::monostate, std::string> showTrainingConfigView(TrainingConfigView view);
    void setStreamIntervalMs(int value);
    void setTrainingPaused(bool paused);
    void updateIconRailOffset();
    Result<GenomeId, std::string> openGenomeDetailByIndex(int index);
    Result<GenomeId, std::string> openGenomeDetailById(const GenomeId& genomeId);
    Result<std::monostate, std::string> loadGenomeDetail(const GenomeId& genomeId);
    void addGenomeToTraining(const GenomeId& genomeId, Scenario::EnumType scenarioId);

private:
    Layout layout_ = Layout::Idle;
    bool evolutionStarted_ = false;
    UiComponentManager* uiManager_;
    EventSink& eventSink_;
    Network::WebSocketServiceInterface* wsService_ = nullptr;

    UserSettings& userSettings_;

    lv_obj_t* averageLabel_ = nullptr;
    lv_obj_t* bestAllTimeLabel_ = nullptr;
    lv_obj_t* bestThisGenLabel_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* contentRow_ = nullptr;
    lv_obj_t* evalLabel_ = nullptr;
    lv_obj_t* evaluationBar_ = nullptr;
    lv_obj_t* genLabel_ = nullptr;
    lv_obj_t* generationBar_ = nullptr;
    lv_obj_t* statsPanel_ = nullptr;
    lv_obj_t* etaLabel_ = nullptr;
    lv_obj_t* simTimeLabel_ = nullptr;
    lv_obj_t* speedupLabel_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;
    lv_obj_t* totalTimeLabel_ = nullptr;
    lv_obj_t* worldContainer_ = nullptr;
    lv_obj_t* mainLayout_ = nullptr;
    lv_obj_t* bottomRow_ = nullptr;
    lv_obj_t* streamPanel_ = nullptr;
    lv_obj_t* idleSpacer_ = nullptr;
    int progressUiUpdateCount_ = 0;
    std::chrono::steady_clock::time_point lastLabelStateLog_{};
    std::chrono::steady_clock::time_point lastProgressUiLog_{};
    std::chrono::steady_clock::time_point lastStatsInvalidate_{};
    lv_obj_t* streamIntervalStepper_ = nullptr;
    lv_obj_t* pauseResumeButton_ = nullptr;
    lv_obj_t* pauseResumeLabel_ = nullptr;
    lv_obj_t* stopTrainingButton_ = nullptr;
    std::unique_ptr<ExpandablePanel> panel_;
    lv_obj_t* panelContent_ = nullptr;

    // Best snapshot display.
    lv_obj_t* bestWorldContainer_ = nullptr;
    lv_obj_t* bestFitnessLabel_ = nullptr;

    // World renderer for live feed.
    std::unique_ptr<CellRenderer> renderer_;

    // Renderer for best snapshot.
    std::unique_ptr<CellRenderer> bestRenderer_;
    std::unique_ptr<Starfield> starfield_;

    // Best snapshot data.
    std::unique_ptr<WorldData> bestWorldData_;
    double bestFitness_ = 0.0;
    int bestGeneration_ = 0;
    bool hasShownBestSnapshot_ = false;
    std::shared_ptr<std::atomic<bool>> alive_;

    // Panel content (created lazily).
    std::unique_ptr<EvolutionControls> evolutionControls_;
    std::unique_ptr<GenomeBrowserPanel> genomeBrowserPanel_;
    std::unique_ptr<TrainingConfigPanel> trainingConfigPanel_;
    std::unique_ptr<TrainingResultBrowserPanel> trainingResultBrowserPanel_;

    // Training result modal.
    Api::TrainingResult::Summary trainingResultSummary_;
    std::vector<Api::TrainingResult::Candidate> primaryCandidates_;
    lv_obj_t* trainingResultOverlay_ = nullptr;
    lv_obj_t* trainingResultCountLabel_ = nullptr;
    lv_obj_t* trainingResultSaveStepper_ = nullptr;
    lv_obj_t* trainingResultSaveButton_ = nullptr;
    lv_obj_t* trainingResultSaveAndRestartButton_ = nullptr;

    void createUI();
    void destroyUI();
    void createActiveUI(int displayWidth, int displayHeight);
    void createIdleUI();
    void createUnsavedResultUI();
    void renderBestWorld();
    void scheduleBestRender();
    static void renderBestWorldAsync(void* data);
    void updateTrainingResultSaveButton();
    void createGenomeBrowserPanelInternal();
    void createStreamPanel(lv_obj_t* parent);

    static void onTrainingResultSaveClicked(lv_event_t* e);
    static void onTrainingResultSaveAndRestartClicked(lv_event_t* e);
    static void onTrainingResultDiscardClicked(lv_event_t* e);
    static void onTrainingResultCountChanged(lv_event_t* e);
    static void onStreamIntervalChanged(lv_event_t* e);
    static void onStopTrainingClicked(lv_event_t* e);
    static void onPauseResumeClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
