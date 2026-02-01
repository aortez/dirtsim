#pragma once

#include "lvgl/lvgl.h"
#include <memory>

namespace DirtSim {
namespace Ui {

class EventSink;
class FractalAnimator;
class DuckStopButton;

/**
 * @brief Simple home panel with a Stop button.
 */
class StopPanel {
public:
    StopPanel(lv_obj_t* container, EventSink& eventSink, FractalAnimator& fractalAnimator);
    ~StopPanel();

private:
    lv_obj_t* container_;
    EventSink& eventSink_;
    FractalAnimator& fractalAnimator_;
    std::unique_ptr<DuckStopButton> stopButton_;

    void createUI();

    static void onStopClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
