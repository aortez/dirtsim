#include "StopPanel.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {

StopPanel::StopPanel(lv_obj_t* container, EventSink& eventSink)
    : container_(container), eventSink_(eventSink)
{
    createUI();
    LOG_INFO(Controls, "StopPanel created");
}

StopPanel::~StopPanel()
{
    LOG_INFO(Controls, "StopPanel destroyed");
}

void StopPanel::createUI()
{
    lv_obj_t* row = lv_obj_create(container_);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    stopButton_ = LVGLBuilder::actionButton(row)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xFF8800)
                      .callback(onStopClicked, this)
                      .buildOrLog();

    if (!stopButton_) {
        LOG_ERROR(Controls, "Failed to create Stop button");
    }
}

void StopPanel::onStopClicked(lv_event_t* e)
{
    auto* self = static_cast<StopPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    LOG_INFO(Controls, "Stop button clicked in StopPanel");

    self->eventSink_.queueEvent(StopButtonClickedEvent{});
}

} // namespace Ui
} // namespace DirtSim
