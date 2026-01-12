#pragma once

#include "lvgl/lvgl.h"

namespace DirtSim {
namespace Ui {

// Forward declaration.
class EventSink;

/**
 * @brief Evolution training controls panel.
 *
 * Provides controls for the evolution training process including a toggle
 * to show/hide the live simulation display.
 */
class EvolutionControls {
public:
    EvolutionControls(lv_obj_t* container, EventSink& eventSink);
    ~EvolutionControls();

    void setLiveDisplayEnabled(bool enabled);
    bool isLiveDisplayEnabled() const { return liveDisplayEnabled_; }

private:
    lv_obj_t* container_;
    EventSink& eventSink_;

    bool liveDisplayEnabled_ = false;

    // Widgets.
    lv_obj_t* liveDisplayToggle_ = nullptr;
    lv_obj_t* stopButton_ = nullptr;

    void createMainView(lv_obj_t* view);

    static void onLiveDisplayToggled(lv_event_t* e);
    static void onStopClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
