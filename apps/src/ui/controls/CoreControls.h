#pragma once

#include "core/WorldData.h"
#include "lvgl/lvgl.h"
#include "ui/rendering/RenderMode.h"
#include "ui/PanelViewController.h"
#include <memory>

namespace DirtSim {

// Forward declaration.
namespace Network {
class WebSocketService;
}

namespace Ui {

// Forward declaration.
class EventSink;

/**
 * @brief Core controls always present in simulation view.
 *
 * Includes: Quit button, Reset button, FPS stats display, Debug Draw toggle, World Size slider.
 */
class CoreControls {
public:
    CoreControls(
        lv_obj_t* container,
        Network::WebSocketService* wsService,
        EventSink& eventSink,
        RenderMode initialMode = RenderMode::ADAPTIVE);
    ~CoreControls();

    /**
     * @brief Update stats display with FPS values.
     */
    void updateStats(double serverFPS, double uiFPS);

    /**
     * @brief Set render mode dropdown to match current mode.
     * Used to sync UI after mode changes or scenario switches.
     */
    void setRenderMode(RenderMode mode);

private:
    lv_obj_t* container_;
    Network::WebSocketService* wsService_;
    EventSink& eventSink_;
    RenderMode currentRenderMode_; // Track current mode to preserve it.

    // View controller for modal navigation.
    std::unique_ptr<PanelViewController> viewController_;

    // Widgets.
    lv_obj_t* quitButton_ = nullptr;
    lv_obj_t* resetButton_ = nullptr;
    lv_obj_t* statsLabel_ = nullptr;
    lv_obj_t* statsLabelUI_ = nullptr;
    lv_obj_t* debugSwitch_ = nullptr;
    lv_obj_t* renderModeButton_ = nullptr; // Button to open render mode modal.
    lv_obj_t* worldSizeStepper_ = nullptr;
    lv_obj_t* scaleFactorStepper_ = nullptr;

    // Render mode button to index mapping.
    std::unordered_map<lv_obj_t*, int> buttonToRenderMode_;

    // View creation helpers.
    void createMainView(lv_obj_t* view);
    void createRenderModeView(lv_obj_t* view);

    // Event handlers.
    static void onQuitClicked(lv_event_t* e);
    static void onResetClicked(lv_event_t* e);
    static void onDebugToggled(lv_event_t* e);
    static void onRenderModeButtonClicked(lv_event_t* e);
    static void onRenderModeSelected(lv_event_t* e);
    static void onRenderModeBackClicked(lv_event_t* e);
    static void onWorldSizeChanged(lv_event_t* e);
    static void onScaleFactorChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
