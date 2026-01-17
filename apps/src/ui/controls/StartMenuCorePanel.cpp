#include "StartMenuCorePanel.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {

StartMenuCorePanel::StartMenuCorePanel(lv_obj_t* container, EventSink& eventSink)
    : container_(container), eventSink_(eventSink)
{
    createUI();
    LOG_INFO(Controls, "StartMenuCorePanel created");
}

StartMenuCorePanel::~StartMenuCorePanel()
{
    LOG_INFO(Controls, "StartMenuCorePanel destroyed");
}

void StartMenuCorePanel::createUI()
{
    auto createRow = [this]() -> lv_obj_t* {
        lv_obj_t* row = lv_obj_create(container_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        return row;
    };

    lv_obj_t* quitRow = createRow();
    quitButton_ = LVGLBuilder::actionButton(quitRow)
                      .text("Quit")
                      .icon(LV_SYMBOL_CLOSE)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onQuitClicked, this)
                      .buildOrLog();

    if (!quitButton_) {
        LOG_ERROR(Controls, "Failed to create Quit button");
    }

    lv_obj_t* nextFractalRow = createRow();
    nextFractalButton_ = LVGLBuilder::actionButton(nextFractalRow)
                             .text("Next Fractal")
                             .mode(LVGLBuilder::ActionMode::Push)
                             .size(80)
                             .callback(onNextFractalClicked, this)
                             .buildOrLog();

    if (!nextFractalButton_) {
        LOG_ERROR(Controls, "Failed to create Next Fractal button");
    }
}

void StartMenuCorePanel::onNextFractalClicked(lv_event_t* e)
{
    StartMenuCorePanel* self = static_cast<StartMenuCorePanel*>(lv_event_get_user_data(e));
    if (!self) return;

    LOG_INFO(Controls, "Next Fractal button clicked in StartMenuCorePanel");

    self->eventSink_.queueEvent(NextFractalClickedEvent{});
}

void StartMenuCorePanel::onQuitClicked(lv_event_t* e)
{
    StartMenuCorePanel* self = static_cast<StartMenuCorePanel*>(lv_event_get_user_data(e));
    if (!self) return;

    LOG_INFO(Controls, "Quit button clicked in StartMenuCorePanel");

    // Queue Exit event to shut down the application.
    UiApi::Exit::Cwc cwc;
    cwc.callback = [](auto&&) {}; // No response needed.
    self->eventSink_.queueEvent(cwc);
}

} // namespace Ui
} // namespace DirtSim
