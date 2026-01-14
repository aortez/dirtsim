#pragma once

#include "lvgl/lvgl.h"

namespace DirtSim {
namespace Ui {

// Forward declaration.
class EventSink;

/**
 * @brief Core controls panel for the StartMenu.
 *
 * Contains the Quit button and any other core settings for the start menu.
 * Shown when the CORE icon is selected in the IconRail.
 */
class StartMenuCorePanel {
public:
    /**
     * @brief Construct the core panel.
     * @param container Parent LVGL container to build UI in.
     * @param eventSink Event sink for queueing events.
     */
    StartMenuCorePanel(lv_obj_t* container, EventSink& eventSink);
    ~StartMenuCorePanel();

private:
    lv_obj_t* container_;
    EventSink& eventSink_;
    lv_obj_t* quitButton_ = nullptr;

    void createUI();

    static void onQuitClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
