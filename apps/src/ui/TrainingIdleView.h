#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "ui/UserSettings.h"
#include "ui/rendering/Starfield.h"
#include <memory>
#include <string>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;

typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class EvolutionControls;
class EventSink;
class ExpandablePanel;
class GenomeBrowserPanel;
class TrainingConfigPanel;
class TrainingResultBrowserPanel;
class UiComponentManager;

class TrainingIdleView {
public:
    enum class TrainingConfigView {
        None,
        Evolution,
        Population,
    };

    TrainingIdleView(
        UiComponentManager* uiManager,
        EventSink& eventSink,
        Network::WebSocketServiceInterface* wsService,
        UserSettings& userSettings,
        const Starfield::Snapshot* starfieldSnapshot = nullptr);
    ~TrainingIdleView();

    void updateAnimations();

    void clearPanelContent();
    void hidePanel();
    void showPanel();
    void createCorePanel();
    void createGenomeBrowserPanel();
    void createTrainingConfigPanel();
    void createTrainingResultBrowserPanel();
    Result<std::monostate, std::string> showTrainingConfigView(TrainingConfigView view);
    void setStreamIntervalMs(int value);
    void setBestPlaybackEnabled(bool enabled);
    void setBestPlaybackIntervalMs(int value);
    void setEvolutionStarted(bool started);
    Result<GenomeId, std::string> openGenomeDetailByIndex(int index);
    Result<GenomeId, std::string> openGenomeDetailById(const GenomeId& genomeId);
    Result<std::monostate, std::string> loadGenomeDetail(const GenomeId& genomeId);
    void addGenomeToTraining(const GenomeId& genomeId, Scenario::EnumType scenarioId);
    bool isTrainingResultModalVisible() const;
    Starfield::Snapshot captureStarfieldSnapshot() const;

private:
    void createUI();
    void destroyUI();
    void createIdleUI();
    void createGenomeBrowserPanelInternal();

    bool evolutionStarted_ = false;
    UiComponentManager* uiManager_ = nullptr;
    EventSink& eventSink_;
    Network::WebSocketServiceInterface* wsService_ = nullptr;
    UserSettings& userSettings_;

    lv_obj_t* container_ = nullptr;
    ExpandablePanel* panel_ = nullptr;
    lv_obj_t* panelContent_ = nullptr;
    std::unique_ptr<Starfield> starfield_;
    const Starfield::Snapshot* starfieldSnapshot_ = nullptr;

    std::unique_ptr<EvolutionControls> evolutionControls_;
    std::unique_ptr<GenomeBrowserPanel> genomeBrowserPanel_;
    std::unique_ptr<TrainingConfigPanel> trainingConfigPanel_;
    std::unique_ptr<TrainingResultBrowserPanel> trainingResultBrowserPanel_;
};

} // namespace Ui
} // namespace DirtSim
