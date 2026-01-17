#pragma once

#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/api/TrainingResultAvailable.h"
#include "ui/controls/IconRail.h"
#include <memory>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

struct WorldData;

namespace Api {
struct EvolutionProgress;
}

namespace Ui {

class CellRenderer;
class EvolutionConfigPanel;
class EvolutionControls;
class EventSink;
class TrainingPopulationPanel;
class UiComponentManager;

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
    explicit TrainingView(UiComponentManager* uiManager, EventSink& eventSink);
    ~TrainingView();

    void updateProgress(const Api::EvolutionProgress& progress);

    void renderWorld(const WorldData& worldData);

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted(GenomeId bestGenomeId);
    void showTrainingResultModal(
        const Api::TrainingResultAvailable::Summary& summary,
        const std::vector<Api::TrainingResultAvailable::Candidate>& candidates);
    void hideTrainingResultModal();

    void clearPanelContent();
    void createCorePanel();
    void createEvolutionConfigPanel();
    void createTrainingPopulationPanel();

private:
    bool evolutionStarted_ = false;
    UiComponentManager* uiManager_;
    EventSink& eventSink_;

    // Shared evolution configuration (owned here, referenced by panels).
    EvolutionConfig evolutionConfig_;
    MutationConfig mutationConfig_;
    TrainingSpec trainingSpec_;

    lv_obj_t* averageLabel_ = nullptr;
    lv_obj_t* bestAllTimeLabel_ = nullptr;
    lv_obj_t* bestThisGenLabel_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* evalLabel_ = nullptr;
    lv_obj_t* evaluationBar_ = nullptr;
    lv_obj_t* genLabel_ = nullptr;
    lv_obj_t* generationBar_ = nullptr;
    lv_obj_t* etaLabel_ = nullptr;
    lv_obj_t* simTimeLabel_ = nullptr;
    lv_obj_t* speedupLabel_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;
    lv_obj_t* totalTimeLabel_ = nullptr;
    lv_obj_t* worldContainer_ = nullptr;

    // Best snapshot display.
    lv_obj_t* bestWorldContainer_ = nullptr;
    lv_obj_t* bestFitnessLabel_ = nullptr;

    // World renderer for live feed.
    std::unique_ptr<CellRenderer> renderer_;

    // Renderer for best snapshot.
    std::unique_ptr<CellRenderer> bestRenderer_;

    // Tracking state for best snapshot capture.
    std::unique_ptr<WorldData> lastRenderedWorld_;
    bool hasRenderedWorld_ = false;
    int lastEval_ = -1;
    int lastGeneration_ = -1;
    double lastBestFitness_ = -1.0;

    // Best snapshot data.
    std::unique_ptr<WorldData> bestWorldData_;
    double bestFitness_ = 0.0;
    int bestGeneration_ = 0;

    // Panel content (created lazily).
    std::unique_ptr<EvolutionConfigPanel> evolutionConfigPanel_;
    std::unique_ptr<EvolutionControls> evolutionControls_;
    std::unique_ptr<TrainingPopulationPanel> trainingPopulationPanel_;

    // Training result modal.
    Api::TrainingResultAvailable::Summary trainingResultSummary_;
    std::vector<Api::TrainingResultAvailable::Candidate> primaryCandidates_;
    lv_obj_t* trainingResultOverlay_ = nullptr;
    lv_obj_t* trainingResultCountLabel_ = nullptr;
    lv_obj_t* trainingResultSaveStepper_ = nullptr;
    lv_obj_t* trainingResultSaveButton_ = nullptr;

    void createUI();
    void destroyUI();
    void renderBestWorld();
    void updateTrainingResultSaveButton();
    std::vector<GenomeId> getTrainingResultSaveIds() const;

    static void onTrainingResultSaveClicked(lv_event_t* e);
    static void onTrainingResultDiscardClicked(lv_event_t* e);
    static void onTrainingResultCountChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
