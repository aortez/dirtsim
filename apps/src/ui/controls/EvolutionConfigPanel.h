#pragma once

#include "core/organisms/evolution/EvolutionConfig.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>

namespace DirtSim {
namespace Ui {

class EventSink;

/**
 * @brief Evolution configuration panel with Start button.
 *
 * Provides controls for configuring evolution parameters before starting
 * a training run. Opens from the EVOLUTION icon in the Training state.
 */
class EvolutionConfigPanel {
public:
    EvolutionConfigPanel(lv_obj_t* container, EventSink& eventSink, bool evolutionStarted);
    ~EvolutionConfigPanel();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted();

private:
    lv_obj_t* container_;
    EventSink& eventSink_;

    std::unique_ptr<PanelViewController> viewController_;

    bool evolutionStarted_ = false;

    // Config values (will be editable later).
    EvolutionConfig evolutionConfig_;
    MutationConfig mutationConfig_;

    // UI elements.
    lv_obj_t* startButton_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;

    // Config steppers.
    lv_obj_t* populationStepper_ = nullptr;
    lv_obj_t* generationsStepper_ = nullptr;
    lv_obj_t* mutationRateStepper_ = nullptr;
    lv_obj_t* tournamentSizeStepper_ = nullptr;
    lv_obj_t* maxSimTimeStepper_ = nullptr;

    void createMainView(lv_obj_t* view);
    void updateStartButtonVisibility();

    static void onStartClicked(lv_event_t* e);
    static void onPopulationChanged(lv_event_t* e);
    static void onGenerationsChanged(lv_event_t* e);
    static void onMutationRateChanged(lv_event_t* e);
    static void onTournamentSizeChanged(lv_event_t* e);
    static void onMaxSimTimeChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
