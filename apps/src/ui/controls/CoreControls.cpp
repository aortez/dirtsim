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
    // Top row: Reset and Quit buttons (evenly spaced).
    lv_obj_t* topRow = lv_obj_create(container_);
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

    // Debug toggle - second row.
    debugSwitch_ = LVGLBuilder::actionButton(container_)
                       .text("Debug Draw")
                       .mode(LVGLBuilder::ActionMode::Toggle)
                       .size(80)
                       .checked(false)
                       .glowColor(0x00CC00)
                       .callback(onDebugToggled, this)
                       .buildOrLog();

    // Stats display (below debug button).
    statsLabel_ = lv_label_create(container_);
    lv_label_set_text(statsLabel_, "Server: -- FPS");
    lv_obj_set_style_text_font(statsLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(statsLabel_, lv_color_white(), 0);

    statsLabelUI_ = lv_label_create(container_);
    lv_label_set_text(statsLabelUI_, "UI: -- FPS");
    lv_obj_set_style_text_font(statsLabelUI_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(statsLabelUI_, lv_color_white(), 0);

    // Render Mode dropdown with ActionDropdown styling.
    renderModeContainer_ = LVGLBuilder::actionDropdown(container_)
                               .label("Render:")
                               .options("Adaptive\nSharp\nSmooth\nPixel Perfect\nLVGL Debug")
                               .selected(0)
                               .width(LV_PCT(95))
                               .callback(onRenderModeChanged, this)
                               .buildOrLog();

    // Scale Factor stepper (affects SHARP, SMOOTH, LVGL_DEBUG, and ADAPTIVE modes).
    scaleFactorStepper_ =
        LVGLBuilder::actionStepper(container_)
            .label("Render Scale")
            .range(1, 200)   // 0.01 to 2.0, scaled by 100.
            .step(5)         // 0.05 increments.
            .value(40)       // Default 0.40.
            .valueFormat("%.2f")
            .valueScale(0.01)
            .width(LV_PCT(95))
            .callback(onScaleFactorChanged, this)
            .buildOrLog();

    // World Size stepper.
    worldSizeStepper_ =
        LVGLBuilder::actionStepper(container_)
            .label("World Size")
            .range(1, 400)
            .step(1)
            .value(28)
            .valueFormat("%.0f")
            .valueScale(1.0)
            .width(LV_PCT(95))
            .callback(onWorldSizeChanged, this)
            .buildOrLog();

    // Set initial render mode in dropdown.
    setRenderMode(initialMode);

    spdlog::info("CoreControls: Initialized");
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
    currentRenderMode_ = mode; // Track the current mode.
    if (!renderModeContainer_) return;

    // Map RenderMode to dropdown index.
    // Order: "Adaptive\nSharp\nSmooth\nPixel Perfect\nLVGL Debug".
    uint16_t index = 0;
    switch (mode) {
        case RenderMode::ADAPTIVE:
            index = 0;
            break;
        case RenderMode::SHARP:
            index = 1;
            break;
        case RenderMode::SMOOTH:
            index = 2;
            break;
        case RenderMode::PIXEL_PERFECT:
            index = 3;
            break;
        case RenderMode::LVGL_DEBUG:
            index = 4;
            break;
    }

    LVGLBuilder::ActionDropdownBuilder::setSelected(renderModeContainer_, index);
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

void CoreControls::onRenderModeChanged(lv_event_t* e)
{
    CoreControls* self = static_cast<CoreControls*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!dropdown) return;

    uint16_t selected = lv_dropdown_get_selected(dropdown);

    // Map dropdown index to RenderMode.
    // Order matches dropdown options: "Adaptive\nSharp\nSmooth\nPixel Perfect\nLVGL Debug".
    RenderMode mode;
    switch (selected) {
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

    // Track the current mode.
    self->currentRenderMode_ = mode;

    // Queue UI-local render mode select event.
    UiApi::RenderModeSelect::Cwc cwc;
    cwc.command.mode = mode;
    cwc.callback = [](auto&&) {}; // No response needed.
    self->eventSink_.queueEvent(cwc);
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
