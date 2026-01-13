#pragma once

#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>

namespace DirtSim {
namespace Ui {

class EventSink;

/**
 * @brief Core controls panel for Training state.
 *
 * Provides the Stop and Quit buttons for the training view.
 * This is the "home" panel for the Training state.
 */
class EvolutionControls {
public:
    EvolutionControls(lv_obj_t* container, EventSink& eventSink, bool evolutionStarted);
    ~EvolutionControls();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted();

private:
    lv_obj_t* container_;
    EventSink& eventSink_;

    std::unique_ptr<PanelViewController> viewController_;

    bool evolutionStarted_ = false;

    lv_obj_t* stopButton_ = nullptr;
    lv_obj_t* quitButton_ = nullptr;

    void createMainView(lv_obj_t* view);
    void updateButtonVisibility();

    static void onStopClicked(lv_event_t* e);
    static void onQuitClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
