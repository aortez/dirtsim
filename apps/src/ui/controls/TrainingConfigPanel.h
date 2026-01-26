#pragma once

#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "lvgl/lvgl.h"
#include <memory>

namespace DirtSim {

struct TrainingSpec;
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class EventSink;
class ExpandablePanel;
class TrainingPopulationPanel;

class TrainingConfigPanel {
public:
    enum class View {
        None,
        Evolution,
        Population,
    };

    TrainingConfigPanel(
        lv_obj_t* container,
        EventSink& eventSink,
        ExpandablePanel* panel,
        Network::WebSocketServiceInterface* wsService,
        bool evolutionStarted,
        EvolutionConfig& evolutionConfig,
        MutationConfig& mutationConfig,
        TrainingSpec& trainingSpec,
        int& streamIntervalMs);
    ~TrainingConfigPanel();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted();
    void setStreamIntervalMs(int value);
    void showView(View view);
    void addSeedGenome(const GenomeId& genomeId, Scenario::EnumType scenarioId);

private:
    lv_obj_t* container_ = nullptr;
    EventSink& eventSink_;
    ExpandablePanel* panel_ = nullptr;
    Network::WebSocketServiceInterface* wsService_ = nullptr;

    bool evolutionStarted_ = false;
    bool evolutionCompleted_ = false;

    EvolutionConfig& evolutionConfig_;
    MutationConfig& mutationConfig_;
    TrainingSpec& trainingSpec_;
    int& streamIntervalMs_;

    int collapsedWidth_ = 0;
    int expandedWidth_ = 0;
    int leftColumnWidth_ = 0;

    View currentView_ = View::None;

    lv_obj_t* leftColumn_ = nullptr;
    lv_obj_t* rightColumn_ = nullptr;
    lv_obj_t* evolutionView_ = nullptr;
    lv_obj_t* populationView_ = nullptr;

    lv_obj_t* startButton_ = nullptr;
    lv_obj_t* evolutionButton_ = nullptr;
    lv_obj_t* populationButton_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;

    lv_obj_t* populationStepper_ = nullptr;
    lv_obj_t* generationsStepper_ = nullptr;
    lv_obj_t* mutationRateStepper_ = nullptr;
    lv_obj_t* tournamentSizeStepper_ = nullptr;
    lv_obj_t* maxSimTimeStepper_ = nullptr;
    lv_obj_t* streamIntervalStepper_ = nullptr;

    std::unique_ptr<TrainingPopulationPanel> trainingPopulationPanel_;

    void createLayout();
    void createLeftColumn(lv_obj_t* parent);
    void createRightColumn(lv_obj_t* parent);
    void createEvolutionView(lv_obj_t* parent);
    void createPopulationView(lv_obj_t* parent);
    void setRightColumnVisible(bool visible);
    void updateControlsEnabled();
    void updateGenerationsStep(int32_t value);
    void updateStatusLabel(const char* text, lv_color_t color);
    void updateToggleLabels();

    static void onStartClicked(lv_event_t* e);
    static void onEvolutionSelected(lv_event_t* e);
    static void onPopulationSelected(lv_event_t* e);
    static void onPopulationChanged(lv_event_t* e);
    static void onGenerationsChanged(lv_event_t* e);
    static void onMutationRateChanged(lv_event_t* e);
    static void onTournamentSizeChanged(lv_event_t* e);
    static void onMaxSimTimeChanged(lv_event_t* e);
    static void onStreamIntervalChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
