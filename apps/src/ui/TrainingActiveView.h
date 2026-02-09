#pragma once

#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "ui/UserSettings.h"
#include "ui/rendering/Starfield.h"
#include <atomic>
#include <chrono>
#include <memory>
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
class ExpandablePanel;
class Starfield;
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
    void updateAnimations();

    void renderWorld(const WorldData& worldData);
    void updateBestSnapshot(const WorldData& worldData, double fitness, int generation);

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted(GenomeId bestGenomeId);
    void setTrainingPaused(bool paused);
    void setStreamIntervalMs(int value);

    void clearPanelContent();
    void createCorePanel();
    bool isTrainingResultModalVisible() const;
    Starfield::Snapshot captureStarfieldSnapshot() const;

private:
    void createUI();
    void destroyUI();
    void createActiveUI(int displayWidth, int displayHeight);
    void renderBestWorld();
    void scheduleBestRender();
    static void renderBestWorldAsync(void* data);
    void createStreamPanel(lv_obj_t* parent);

    static void onStreamIntervalChanged(lv_event_t* e);
    static void onStopTrainingClicked(lv_event_t* e);
    static void onPauseResumeClicked(lv_event_t* e);

    bool evolutionStarted_ = false;
    UiComponentManager* uiManager_ = nullptr;
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

    lv_obj_t* bestWorldContainer_ = nullptr;
    lv_obj_t* bestFitnessLabel_ = nullptr;

    std::unique_ptr<CellRenderer> renderer_;
    std::unique_ptr<CellRenderer> bestRenderer_;
    std::unique_ptr<Starfield> starfield_;
    const Starfield::Snapshot* starfieldSnapshot_ = nullptr;

    std::unique_ptr<WorldData> bestWorldData_;
    double bestFitness_ = 0.0;
    int bestGeneration_ = 0;
    bool hasShownBestSnapshot_ = false;
    std::shared_ptr<std::atomic<bool>> alive_;

    std::unique_ptr<EvolutionControls> evolutionControls_;
};

} // namespace Ui
} // namespace DirtSim
