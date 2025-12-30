#include "CoreControls.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/Reset.h"
#include "server/api/WorldResize.h"
#include "ui/rendering/CellRenderer.h"
#include "ui/state-machine/EventSink.h"
#include "ui/state-machine/api/DrawDebugToggle.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/state-machine/api/PixelRendererToggle.h"
#include "ui/state-machine/api/RenderModeSelect.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <atomic>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

CoreControls::CoreControls(
    lv_obj_t* container,
    Network::WebSocketService* wsService,
    EventSink& eventSink,
    RenderMode initialMode)
    : container_(container),
      wsService_(wsService),
      eventSink_(eventSink),
      currentRenderMode_(initialMode)
{
    // Create view controller.
    viewController_ = std::make_unique<PanelViewController>(container_);

    // Create main view.
    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

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
    lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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

    // Quit button - red with power icon (push).
    quitButton_ = LVGLBuilder::actionButton(topRow)
                      .text("Quit")
                      .icon(LV_SYMBOL_POWER)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onQuitClicked, this)
                      .buildOrLog();

    // Debug toggle.
    debugSwitch_ = LVGLBuilder::actionButton(view)
                       .text("Debug Draw")
                       .mode(LVGLBuilder::ActionMode::Toggle)
                       .size(80)
                       .checked(false)
                       .glowColor(0x00CC00)
                       .callback(onDebugToggled, this)
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
    std::string renderModeText = "Render Mode: " + renderModeToString(currentRenderMode_);
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
    scaleFactorStepper_ =
        LVGLBuilder::actionStepper(view)
            .label("Render Scale")
            .range(1, 200)
            .step(5)
            .value(40)
            .valueFormat("%.2f")
            .valueScale(0.01)
            .width(LV_PCT(95))
            .callback(onScaleFactorChanged, this)
            .buildOrLog();

    // World Size stepper.
    worldSizeStepper_ =
        LVGLBuilder::actionStepper(view)
            .label("World Size")
            .range(1, 400)
            .step(1)
            .value(28)
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
    const char* modes[] = {"Adaptive", "Sharp", "Smooth", "Pixel Perfect", "LVGL Debug"};
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

void CoreControls::setRenderMode(RenderMode mode)
{
    currentRenderMode_ = mode;
    if (!renderModeButton_) return;

    // ActionButton stores the container - need to get the inner button and then its label.
    // The button is the first child of the container.
    lv_obj_t* button = lv_obj_get_child(renderModeButton_, 0);
    if (!button) return;

    // In row layout: first child is icon, second is text label.
    lv_obj_t* label = lv_obj_get_child(button, 1);
    if (label) {
        std::string renderModeText = "Render Mode: " + renderModeToString(mode);
        lv_label_set_text(label, renderModeText.c_str());
    }
}

void CoreControls::onQuitClicked(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("CoreControls: Quit button clicked");

    // Queue UI-local exit event (works in all states, including Disconnected).
    UiApi::Exit::Cwc cwc;
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

    // Update button text and track current mode.
    self->currentRenderMode_ = mode;
    self->setRenderMode(mode);

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
    const Api::WorldResize::Command cmd{ .width = static_cast<uint32_t>(value),
                                         .height = static_cast<uint32_t>(value) };
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
    // Preserve the current mode (don't force SHARP).
    UiApi::RenderModeSelect::Cwc cwc;
    cwc.command.mode = self->currentRenderMode_;
    cwc.callback = [](auto&&) {}; // No response needed.
    self->eventSink_.queueEvent(cwc);
}

} // namespace Ui
} // namespace DirtSim
