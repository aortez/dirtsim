#pragma once

#include "core/organisms/evolution/GenomeMetadata.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>

namespace DirtSim {

struct EvolutionConfig;
struct MutationConfig;

namespace Ui {

class EventSink;

/**
 * @brief Home panel for Training state.
 *
 * Provides Start, Stop, and Quit buttons for the training view.
 * This is the "home" panel for the Training state.
 */
class EvolutionControls {
public:
    EvolutionControls(
        lv_obj_t* container,
        EventSink& eventSink,
        bool evolutionStarted,
        EvolutionConfig& evolutionConfig,
        MutationConfig& mutationConfig);
    ~EvolutionControls();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted(GenomeId bestGenomeId);

private:
    lv_obj_t* container_;
    EventSink& eventSink_;

    std::unique_ptr<PanelViewController> viewController_;

    bool evolutionStarted_ = false;
    bool evolutionCompleted_ = false;
    GenomeId bestGenomeId_;

    // Shared configs (owned by TrainingView).
    EvolutionConfig& evolutionConfig_;
    MutationConfig& mutationConfig_;

    lv_obj_t* startButton_ = nullptr;
    lv_obj_t* stopButton_ = nullptr;
    lv_obj_t* viewBestButton_ = nullptr;
    lv_obj_t* quitButton_ = nullptr;

    void createMainView(lv_obj_t* view);
    void updateButtonVisibility();

    static void onStartClicked(lv_event_t* e);
    static void onStopClicked(lv_event_t* e);
    static void onViewBestClicked(lv_event_t* e);
    static void onQuitClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
