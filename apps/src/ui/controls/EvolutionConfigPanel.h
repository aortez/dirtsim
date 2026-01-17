#pragma once

#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>

namespace DirtSim {

struct EvolutionConfig;
struct MutationConfig;
struct TrainingSpec;

namespace Ui {

class EventSink;

/**
 * @brief Evolution configuration panel.
 *
 * Provides controls for configuring evolution parameters.
 * Opens from the EVOLUTION icon in the Training state.
 */
class EvolutionConfigPanel {
public:
    EvolutionConfigPanel(
        lv_obj_t* container,
        EventSink& eventSink,
        bool evolutionStarted,
        EvolutionConfig& evolutionConfig,
        MutationConfig& mutationConfig,
        TrainingSpec& trainingSpec);
    ~EvolutionConfigPanel();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted();

private:
    lv_obj_t* container_;
    EventSink& eventSink_;

    std::unique_ptr<PanelViewController> viewController_;

    bool evolutionStarted_ = false;

    // Shared configs (owned by TrainingView).
    EvolutionConfig& evolutionConfig_;
    MutationConfig& mutationConfig_;
    TrainingSpec& trainingSpec_;

    // UI elements.
    lv_obj_t* startButton_ = nullptr;
    lv_obj_t* stopButton_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;

    // Config steppers.
    lv_obj_t* populationStepper_ = nullptr;
    lv_obj_t* generationsStepper_ = nullptr;
    lv_obj_t* mutationRateStepper_ = nullptr;
    lv_obj_t* tournamentSizeStepper_ = nullptr;
    lv_obj_t* maxSimTimeStepper_ = nullptr;

    void createMainView(lv_obj_t* view);
    void updateButtonVisibility();
    void updateControlsEnabled();

    static void onPopulationChanged(lv_event_t* e);
    static void onGenerationsChanged(lv_event_t* e);
    static void onMutationRateChanged(lv_event_t* e);
    static void onTournamentSizeChanged(lv_event_t* e);
    static void onMaxSimTimeChanged(lv_event_t* e);
    static void onStartClicked(lv_event_t* e);
    static void onStopClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
