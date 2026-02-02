#pragma once

#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>

namespace DirtSim {

namespace Ui {

class EventSink;
class FractalAnimator;
class DuckStopButton;

/**
 * @brief Home panel for Training state.
 *
 * Provides View Best and Quit buttons for the training view.
 * This is the "home" panel for the Training state.
 */
class EvolutionControls {
public:
    EvolutionControls(
        lv_obj_t* container,
        EventSink& eventSink,
        bool evolutionStarted,
        TrainingSpec& trainingSpec,
        FractalAnimator* fractalAnimator);
    ~EvolutionControls();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted(GenomeId bestGenomeId);

private:
    lv_obj_t* container_;
    EventSink& eventSink_;
    FractalAnimator* fractalAnimator_ = nullptr;

    std::unique_ptr<PanelViewController> viewController_;

    bool evolutionStarted_ = false;
    bool evolutionCompleted_ = false;
    GenomeId bestGenomeId_;

    // Shared configs (owned by TrainingView).
    TrainingSpec& trainingSpec_;

    lv_obj_t* viewBestButton_ = nullptr;
    std::unique_ptr<DuckStopButton> quitButton_;

    void createMainView(lv_obj_t* view);
    void updateButtonVisibility();

    static void onViewBestClicked(lv_event_t* e);
    static void onQuitClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
