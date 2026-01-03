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

struct CoreControlsState {
    bool debugDrawEnabled = false;
    RenderMode renderMode = RenderMode::ADAPTIVE;
    double scaleFactor = 0.4;
    int worldSize = 28;
};

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
        const CoreControlsState& initialState);
    ~CoreControls();

    void updateFromState(const CoreControlsState& state);
    void updateStats(double serverFPS, double uiFPS);

private:
    lv_obj_t* container_;
    Network::WebSocketService* wsService_;
    EventSink& eventSink_;
    CoreControlsState state_;

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
