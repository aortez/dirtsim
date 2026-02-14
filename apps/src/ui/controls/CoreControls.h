#pragma once

#include "core/MaterialType.h"
#include "core/WorldData.h"
#include "lvgl/lvgl.h"
#include "ui/InteractionMode.h"
#include "ui/PanelViewController.h"
#include "ui/rendering/RenderMode.h"
#include <memory>

namespace DirtSim {

// Forward declaration.
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

// Forward declaration.
class EventSink;
class FractalAnimator;
class DuckStopButton;
class UiComponentManager;

struct CoreControlsState {
    bool debugDrawEnabled = false;
    Material::EnumType drawMaterial = Material::EnumType::Wall;
    InteractionMode interactionMode = InteractionMode::NONE;
    RenderMode renderMode = RenderMode::ADAPTIVE;
    double scaleFactor = 0.4;
    int worldSize = 28;
};

/**
 * @brief Core controls always present in simulation view.
 *
 * Includes: Stop button, Reset button, FPS stats display, Debug Draw toggle, World Size slider.
 */
class CoreControls {
public:
    CoreControls(
        lv_obj_t* container,
        Network::WebSocketServiceInterface* wsService,
        EventSink& eventSink,
        CoreControlsState& sharedState,
        UiComponentManager* uiManager,
        FractalAnimator* fractalAnimator);
    ~CoreControls();

    void updateFromState();
    void updateStats(double serverFPS, double uiFPS);

private:
    lv_obj_t* container_;
    Network::WebSocketServiceInterface* wsService_;
    EventSink& eventSink_;
    CoreControlsState& state_;
    UiComponentManager* uiManager_;
    FractalAnimator* fractalAnimator_ = nullptr;

    // View controller for modal navigation.
    std::unique_ptr<PanelViewController> viewController_;

    // Widgets.
    std::unique_ptr<DuckStopButton> stopButton_;
    lv_obj_t* resetButton_ = nullptr;
    lv_obj_t* resetConfirmCheckbox_ = nullptr;
    lv_obj_t* statsLabel_ = nullptr;
    lv_obj_t* statsLabelUI_ = nullptr;
    lv_obj_t* debugSwitch_ = nullptr;
    lv_obj_t* interactionModeButton_ = nullptr;
    lv_obj_t* renderModeButton_ = nullptr;
    lv_obj_t* worldSizeStepper_ = nullptr;
    lv_obj_t* scaleFactorStepper_ = nullptr;

    // Button to index mappings for modal selections.
    std::unordered_map<lv_obj_t*, int> buttonToRenderMode_;
    std::unordered_map<lv_obj_t*, int> buttonToInteractionMode_;
    std::unordered_map<lv_obj_t*, Material::EnumType> buttonToDrawMaterial_;

    // View creation helpers.
    void createMainView(lv_obj_t* view);
    void createRenderModeView(lv_obj_t* view);
    void createInteractionModeView(lv_obj_t* view);
    void createDrawMaterialView(lv_obj_t* view);

    // Event handlers.
    static void onStopClicked(lv_event_t* e);
    static void onResetClicked(lv_event_t* e);
    static void onResetConfirmToggled(lv_event_t* e);
    static void onDebugToggled(lv_event_t* e);
    static void onInteractionModeButtonClicked(lv_event_t* e);
    static void onInteractionModeSelected(lv_event_t* e);
    static void onInteractionModeBackClicked(lv_event_t* e);
    static void onDrawMaterialSelected(lv_event_t* e);
    static void onDrawMaterialBackClicked(lv_event_t* e);
    static void onRenderModeButtonClicked(lv_event_t* e);
    static void onRenderModeSelected(lv_event_t* e);
    static void onRenderModeBackClicked(lv_event_t* e);
    static void onWorldSizeChanged(lv_event_t* e);
    static void onScaleFactorChanged(lv_event_t* e);

    void updateResetButtonEnabled();
};

} // namespace Ui
} // namespace DirtSim
