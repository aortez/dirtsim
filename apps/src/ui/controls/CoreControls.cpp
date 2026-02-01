#include "CoreControls.h"
#include "core/Assert.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/Reset.h"
#include "server/api/WorldResize.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/DuckStopButton.h"
#include "ui/controls/IconRail.h"
#include "ui/rendering/CellRenderer.h"
#include "ui/rendering/FractalAnimator.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/api/DrawDebugToggle.h"
#include "ui/state-machine/api/PixelRendererToggle.h"
#include "ui/state-machine/api/RenderModeSelect.h"
#include "ui/state-machine/api/SimStop.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <atomic>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

CoreControls::CoreControls(
    lv_obj_t* container,
    Network::WebSocketServiceInterface* wsService,
    EventSink& eventSink,
    CoreControlsState& sharedState,
    UiComponentManager* uiManager,
    FractalAnimator* fractalAnimator)
    : container_(container),
      wsService_(wsService),
      eventSink_(eventSink),
      state_(sharedState),
      uiManager_(uiManager),
      fractalAnimator_(fractalAnimator)
{
    // Create view controller.
    viewController_ = std::make_unique<PanelViewController>(container_);

    // Create main view.
    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

    // Create interaction mode modal view.
    lv_obj_t* interactionModeView = viewController_->createView("interaction_mode");
    createInteractionModeView(interactionModeView);

    // Create draw material modal view (nested under interaction mode).
    lv_obj_t* drawMaterialView = viewController_->createView("draw_material");
    createDrawMaterialView(drawMaterialView);

    // Create render mode modal view.
    lv_obj_t* renderModeView = viewController_->createView("render_mode");
    createRenderModeView(renderModeView);

    // Show main view initially.
    viewController_->showView("main");

    spdlog::info("CoreControls: Initialized with modal navigation");
}

void CoreControls::createMainView(lv_obj_t* view)
{
    // Top row: Reset and Quit buttons (evenly spaced).
    lv_obj_t* topRow = lv_obj_create(view);
    lv_obj_set_size(topRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        topRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(topRow, 4, 0);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);

    // Reset button - orange with refresh icon (push).
    resetButton_ = LVGLBuilder::actionButton(topRow)
                       .text("Reset")
                       .icon(LV_SYMBOL_REFRESH)
                       .mode(LVGLBuilder::ActionMode::Push)
                       .size(80)
                       .backgroundColor(0xFF8800)
                       .callback(onResetClicked, this)
                       .buildOrLog();

    // Stop button - fractal background with duck (push). Returns to start menu.
    DIRTSIM_ASSERT(fractalAnimator_, "CoreControls requires FractalAnimator for Stop button");
    stopButton_ = std::make_unique<DuckStopButton>(topRow, *fractalAnimator_, 108, 108, "Stop");
    if (stopButton_ && stopButton_->getButton()) {
        lv_obj_add_event_cb(stopButton_->getButton(), onStopClicked, LV_EVENT_CLICKED, this);
    }
    else {
        spdlog::error("CoreControls: Failed to create Stop button");
    }

    // Debug toggle.
    debugSwitch_ = LVGLBuilder::actionButton(view)
                       .text("Debug Draw")
                       .mode(LVGLBuilder::ActionMode::Toggle)
                       .size(80)
                       .checked(state_.debugDrawEnabled)
                       .glowColor(0x00CC00)
                       .callback(onDebugToggled, this)
                       .buildOrLog();

    // Interaction mode button - navigates to modal for selection.
    std::string interactionModeText =
        "Interaction: " + interactionModeToString(state_.interactionMode);
    interactionModeButton_ = LVGLBuilder::actionButton(view)
                                 .text(interactionModeText.c_str())
                                 .icon(LV_SYMBOL_RIGHT)
                                 .width(LV_PCT(95))
                                 .height(LVGLBuilder::Style::ACTION_SIZE)
                                 .layoutRow()
                                 .alignLeft()
                                 .callback(onInteractionModeButtonClicked, this)
                                 .buildOrLog();

    // Stats display.
    statsLabel_ = lv_label_create(view);
    lv_label_set_text(statsLabel_, "Server: -- FPS");
    lv_obj_set_style_text_font(statsLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(statsLabel_, lv_color_white(), 0);

    statsLabelUI_ = lv_label_create(view);
    lv_label_set_text(statsLabelUI_, "UI: -- FPS");
    lv_obj_set_style_text_font(statsLabelUI_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(statsLabelUI_, lv_color_white(), 0);

    // Render Mode button - uses ActionButton with horizontal layout.
    std::string renderModeText = "Render Mode: " + renderModeToString(state_.renderMode);
    renderModeButton_ = LVGLBuilder::actionButton(view)
                            .text(renderModeText.c_str())
                            .icon(LV_SYMBOL_RIGHT)
                            .width(LV_PCT(95))
                            .height(LVGLBuilder::Style::ACTION_SIZE)
                            .layoutRow()
                            .alignLeft()
                            .callback(onRenderModeButtonClicked, this)
                            .buildOrLog();

    // Scale Factor stepper.
    scaleFactorStepper_ = LVGLBuilder::actionStepper(view)
                              .label("Render Scale")
                              .range(1, 200)
                              .step(5)
                              .value(static_cast<int32_t>(state_.scaleFactor * 100))
                              .valueFormat("%.2f")
                              .valueScale(0.01)
                              .width(LV_PCT(95))
                              .callback(onScaleFactorChanged, this)
                              .buildOrLog();

    // World Size stepper.
    worldSizeStepper_ = LVGLBuilder::actionStepper(view)
                            .label("World Size")
                            .range(1, 400)
                            .step(1)
                            .value(state_.worldSize)
                            .valueFormat("%.0f")
                            .valueScale(1.0)
                            .width(LV_PCT(95))
                            .callback(onWorldSizeChanged, this)
                            .buildOrLog();
}

void CoreControls::createRenderModeView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onRenderModeBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Render Mode");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Render mode option buttons.
    buttonToRenderMode_.clear();
    const char* modes[] = { "Adaptive", "Sharp", "Smooth", "Pixel Perfect", "LVGL Debug" };
    for (int i = 0; i < 5; i++) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(modes[i])
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            // Get the inner button (first child of container).
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                // Store button->mode mapping (don't touch ActionButton's user_data!).
                buttonToRenderMode_[button] = i;
                lv_obj_add_event_cb(button, onRenderModeSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void CoreControls::createInteractionModeView(lv_obj_t* view)
{
    // Back button.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onInteractionModeBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Interaction Mode");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Interaction mode option buttons.
    // "None" and "Erase" are direct selections.
    // "Draw" navigates to material selection submenu.
    buttonToInteractionMode_.clear();

    // None button - direct selection.
    lv_obj_t* noneContainer = LVGLBuilder::actionButton(view)
                                  .text("None")
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
    if (noneContainer) {
        lv_obj_t* button = lv_obj_get_child(noneContainer, 0);
        if (button) {
            buttonToInteractionMode_[button] = 0; // NONE.
            lv_obj_add_event_cb(button, onInteractionModeSelected, LV_EVENT_CLICKED, this);
        }
    }

    // Draw button - navigates to material selection.
    lv_obj_t* drawContainer = LVGLBuilder::actionButton(view)
                                  .text("Draw...")
                                  .icon(LV_SYMBOL_RIGHT)
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutRow()
                                  .alignLeft()
                                  .buildOrLog();
    if (drawContainer) {
        lv_obj_t* button = lv_obj_get_child(drawContainer, 0);
        if (button) {
            buttonToInteractionMode_[button] = 1; // DRAW - but navigates to submenu.
            lv_obj_add_event_cb(button, onInteractionModeSelected, LV_EVENT_CLICKED, this);
        }
    }

    // Erase button - direct selection.
    lv_obj_t* eraseContainer = LVGLBuilder::actionButton(view)
                                   .text("Erase")
                                   .width(LV_PCT(95))
                                   .height(LVGLBuilder::Style::ACTION_SIZE)
                                   .layoutColumn()
                                   .buildOrLog();
    if (eraseContainer) {
        lv_obj_t* button = lv_obj_get_child(eraseContainer, 0);
        if (button) {
            buttonToInteractionMode_[button] = 2; // ERASE.
            lv_obj_add_event_cb(button, onInteractionModeSelected, LV_EVENT_CLICKED, this);
        }
    }
}

void CoreControls::createDrawMaterialView(lv_obj_t* view)
{
    // Back button - returns to interaction mode view.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onDrawMaterialBackClicked, this)
        .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Draw Material");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Material option buttons (excluding AIR since that's for erasing).
    buttonToDrawMaterial_.clear();
    const std::vector<Material::EnumType> drawableMaterials = {
        Material::EnumType::Dirt, Material::EnumType::Leaf,  Material::EnumType::Metal,
        Material::EnumType::Root, Material::EnumType::Sand,  Material::EnumType::Seed,
        Material::EnumType::Wall, Material::EnumType::Water, Material::EnumType::Wood
    };

    for (Material::EnumType material : drawableMaterials) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(toString(material).c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();

        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonToDrawMaterial_[button] = material;
                lv_obj_add_event_cb(button, onDrawMaterialSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

CoreControls::~CoreControls()
{
    // No manual cleanup needed - LVGL automatically destroys callbacks when widgets are destroyed.
    spdlog::info("CoreControls: Destroyed");
}

void CoreControls::updateStats(double serverFPS, double uiFPS)
{
    if (statsLabel_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Server: %.1f FPS", serverFPS);
        lv_label_set_text(statsLabel_, buf);
    }

    if (statsLabelUI_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "UI: %.1f FPS", uiFPS);
        lv_label_set_text(statsLabelUI_, buf);
    }
}

void CoreControls::updateFromState()
{
    if (debugSwitch_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(debugSwitch_, state_.debugDrawEnabled);
    }

    if (interactionModeButton_) {
        lv_obj_t* button = lv_obj_get_child(interactionModeButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                std::string text;
                if (state_.interactionMode == InteractionMode::DRAW) {
                    // Show material name when in draw mode.
                    text = std::string("Draw: ") + toString(state_.drawMaterial);
                }
                else {
                    text = "Interaction: " + interactionModeToString(state_.interactionMode);
                }
                lv_label_set_text(label, text.c_str());
            }
        }
    }

    if (renderModeButton_) {
        lv_obj_t* button = lv_obj_get_child(renderModeButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                std::string text = "Render Mode: " + renderModeToString(state_.renderMode);
                lv_label_set_text(label, text.c_str());
            }
        }
    }

    if (scaleFactorStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            scaleFactorStepper_, static_cast<int32_t>(state_.scaleFactor * 100));
    }

    if (worldSizeStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(worldSizeStepper_, state_.worldSize);
    }
}

void CoreControls::onStopClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("CoreControls: Stop button clicked");

    // Queue SimStop event to return to start menu.
    UiApi::SimStop::Cwc cwc;
    cwc.callback = [](auto&&) {}; // No response needed.
    self->eventSink_.queueEvent(cwc);
}

void CoreControls::onResetClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("CoreControls: Reset button clicked");

    // Send binary reset command to server.
    static std::atomic<uint64_t> nextId{ 1 };
    const Api::Reset::Command cmd{};
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
    auto result = self->wsService_->sendBinary(Network::serialize_envelope(envelope));
    if (result.isError()) {
        spdlog::error("CoreControls: Failed to send Reset: {}", result.errorValue());
    }
}

void CoreControls::onDebugToggled(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    // Get current state from ActionButton.
    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->debugSwitch_);

    spdlog::info("CoreControls: Debug draw toggled to {}", enabled ? "ON" : "OFF");

    // Queue UI-local debug toggle event.
    UiApi::DrawDebugToggle::Cwc cwc;
    cwc.command.enabled = enabled;
    cwc.callback = [](auto&&) {}; // No response needed.
    self->eventSink_.queueEvent(cwc);
}

void CoreControls::onInteractionModeButtonClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    spdlog::debug("CoreControls: Interaction mode button clicked");
    self->viewController_->showView("interaction_mode");
}

void CoreControls::onInteractionModeSelected(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up mode index from button mapping.
    auto it = self->buttonToInteractionMode_.find(btn);
    if (it == self->buttonToInteractionMode_.end()) {
        spdlog::error("CoreControls: Unknown interaction mode button clicked");
        return;
    }

    int modeIndex = it->second;

    // Handle "Draw" specially - navigate to material selection submenu.
    if (modeIndex == 1) {
        spdlog::debug("CoreControls: Draw selected, showing material menu");
        self->viewController_->showView("draw_material");
        return;
    }

    // Map index to InteractionMode.
    // Order: "None" (0), "Erase" (2).
    InteractionMode mode;
    switch (modeIndex) {
        case 0:
            mode = InteractionMode::NONE;
            break;
        case 2:
            mode = InteractionMode::ERASE;
            break;
        default:
            mode = InteractionMode::NONE;
            break;
    }

    spdlog::info("CoreControls: Interaction mode changed to {}", interactionModeToString(mode));

    self->state_.interactionMode = mode;

    // Update button text and go back to main view.
    self->updateFromState();
    self->viewController_->showView("main");
}

void CoreControls::onInteractionModeBackClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    self->viewController_->showView("main");
}

void CoreControls::onDrawMaterialSelected(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up material from button mapping.
    auto it = self->buttonToDrawMaterial_.find(btn);
    if (it == self->buttonToDrawMaterial_.end()) {
        spdlog::error("CoreControls: Unknown draw material button clicked");
        return;
    }

    Material::EnumType material = it->second;

    spdlog::info("CoreControls: Draw mode enabled with material {}", toString(material));

    // Set both the interaction mode and the draw material.
    self->state_.interactionMode = InteractionMode::DRAW;
    self->state_.drawMaterial = material;

    // Update button text and go back to main view.
    self->updateFromState();
    self->viewController_->showView("main");
}

void CoreControls::onDrawMaterialBackClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    // Go back to interaction mode view (not main).
    self->viewController_->showView("interaction_mode");
}

void CoreControls::onRenderModeButtonClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    spdlog::debug("CoreControls: Render mode button clicked");
    self->viewController_->showView("render_mode");
}

void CoreControls::onRenderModeSelected(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up mode index from button mapping.
    auto it = self->buttonToRenderMode_.find(btn);
    if (it == self->buttonToRenderMode_.end()) {
        spdlog::error("CoreControls: Unknown render mode button clicked");
        return;
    }

    int modeIndex = it->second;

    // Map index to RenderMode.
    // Order: "Adaptive", "Sharp", "Smooth", "Pixel Perfect", "LVGL Debug".
    RenderMode mode;
    switch (modeIndex) {
        case 0:
            mode = RenderMode::ADAPTIVE;
            break;
        case 1:
            mode = RenderMode::SHARP;
            break;
        case 2:
            mode = RenderMode::SMOOTH;
            break;
        case 3:
            mode = RenderMode::PIXEL_PERFECT;
            break;
        case 4:
            mode = RenderMode::LVGL_DEBUG;
            break;
        default:
            mode = RenderMode::ADAPTIVE;
            break;
    }

    spdlog::info("CoreControls: Render mode changed to {}", renderModeToString(mode));

    // Update local state and button text.
    self->state_.renderMode = mode;
    if (self->renderModeButton_) {
        lv_obj_t* button = lv_obj_get_child(self->renderModeButton_, 0);
        if (button) {
            lv_obj_t* label = lv_obj_get_child(button, 1);
            if (label) {
                std::string text = "Render Mode: " + renderModeToString(mode);
                lv_label_set_text(label, text.c_str());
            }
        }
    }

    // Return to main view.
    if (self->viewController_) {
        self->viewController_->showView("main");
    }

    // Queue UI-local render mode select event.
    UiApi::RenderModeSelect::Cwc cwc;
    cwc.command.mode = mode;
    cwc.callback = [](auto&&) {}; // No response needed.
    self->eventSink_.queueEvent(cwc);
}

void CoreControls::onRenderModeBackClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->viewController_) return;

    spdlog::debug("CoreControls: Render mode back button clicked");
    self->viewController_->showView("main");
}

void CoreControls::onWorldSizeChanged(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->worldSizeStepper_) {
        spdlog::error("CoreControls: onWorldSizeChanged called with null self or stepper");
        return;
    }

    // Get value from stepper.
    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->worldSizeStepper_);

    spdlog::info("CoreControls: World size changed to {}", value);

    // Send binary WorldResize API command.
    static std::atomic<uint64_t> nextId{ 1 };
    const Api::WorldResize::Command cmd{ .width = static_cast<int16_t>(value),
                                         .height = static_cast<int16_t>(value) };
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
    auto result = self->wsService_->sendBinary(Network::serialize_envelope(envelope));
    if (result.isError()) {
        spdlog::error("CoreControls: Failed to send WorldResize: {}", result.errorValue());
    }
}

void CoreControls::onScaleFactorChanged(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self || !self->scaleFactorStepper_) {
        spdlog::error("CoreControls: onScaleFactorChanged called with null self or stepper");
        return;
    }

    // Get value from stepper.
    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->scaleFactorStepper_);

    // Convert from integer (1-200) to double (0.01-2.0).
    double scaleFactor = value / 100.0;

    spdlog::info("CoreControls: Scale factor changed to {:.2f}", scaleFactor);

    // Update the global scale factor.
    setSharpScaleFactor(scaleFactor);

    // Trigger renderer reinitialization by sending RenderModeSelect event.
    UiApi::RenderModeSelect::Cwc cwc;
    cwc.command.mode = self->state_.renderMode;
    cwc.callback = [](auto&&) {};
    self->eventSink_.queueEvent(cwc);
}

} // namespace Ui
} // namespace DirtSim
