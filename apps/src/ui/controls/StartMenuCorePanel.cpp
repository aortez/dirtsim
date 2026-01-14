#include "StartMenuCorePanel.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

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
    // Quit button - red, same style as the old floating quit button.
    quitButton_ = LVGLBuilder::actionButton(container_)
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
