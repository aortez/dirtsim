#pragma once

#include "lvgl/lvgl.h"

namespace DirtSim {
namespace Ui {

class EventSink;

/**
 * @brief Simple home panel with a Stop button.
 */
class StopPanel {
public:
    StopPanel(lv_obj_t* container, EventSink& eventSink);
    ~StopPanel();

private:
    lv_obj_t* container_;
    EventSink& eventSink_;
    lv_obj_t* stopButton_ = nullptr;

    void createUI();

    static void onStopClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
