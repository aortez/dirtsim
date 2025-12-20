#include "ClockControls.h"
#include "server/scenarios/scenarios/ClockScenario.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

ClockControls::ClockControls(
    lv_obj_t* container,
    Network::WebSocketService* wsService,
    const ClockConfig& config,
    DisplayDimensionsGetter dimensionsGetter)
    : ScenarioControlsBase(container, wsService, "clock"), dimensionsGetter_(std::move(dimensionsGetter))
{
    // Create widgets.
    createWidgets();

    // Initialize widget states from config.
    updateFromConfig(config);

    // Finish initialization - allow callbacks to send updates now.
    finishInitialization();

    spdlog::info("ClockControls: Initialized");
}

ClockControls::~ClockControls()
{
    // Base class handles container deletion.
    spdlog::info("ClockControls: Destroyed");
}

void ClockControls::createWidgets()
{
    // Create font dropdown.
    // Order matches ClockFont enum: DotMatrix=0, Segment7=1, Segment7Large=2, Segment7Tall=3.
    fontDropdown_ = LVGLBuilder::dropdown(controlsContainer_)
                        .options("Dot Matrix\n7-Segment\n7-Segment Large\n7-Segment Tall")
                        .selected(0)
                        .size(LV_PCT(95), LVGLBuilder::Style::CONTROL_HEIGHT)
                        .buildOrLog();

    if (fontDropdown_) {
        lv_obj_set_user_data(fontDropdown_, this);
        lv_obj_add_event_cb(fontDropdown_, onFontChanged, LV_EVENT_VALUE_CHANGED, this);
    }

    // Build dropdown options string from timezone array.
    std::string options;
    for (size_t i = 0; i < ClockScenario::TIMEZONES.size(); ++i) {
        if (i > 0) {
            options += "\n";
        }
        options += ClockScenario::TIMEZONES[i].label;
    }

    // Create timezone dropdown.
    timezoneDropdown_ = LVGLBuilder::dropdown(controlsContainer_)
                            .options(options.c_str())
                            .selected(0)
                            .size(LV_PCT(95), LVGLBuilder::Style::CONTROL_HEIGHT)
                            .buildOrLog();

    if (timezoneDropdown_) {
        lv_obj_set_user_data(timezoneDropdown_, this);
        lv_obj_add_event_cb(timezoneDropdown_, onTimezoneChanged, LV_EVENT_VALUE_CHANGED, this);
    }

    // Create show seconds toggle.
    secondsSwitch_ = LVGLBuilder::labeledSwitch(controlsContainer_)
                         .label("Show Seconds")
                         .initialState(true)
                         .callback(onSecondsToggled, this)
                         .buildOrLog();
}

void ClockControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    // Extract ClockConfig from variant.
    if (!std::holds_alternative<ClockConfig>(configVariant)) {
        spdlog::error("ClockControls: Invalid config type (expected ClockConfig)");
        return;
    }

    const ClockConfig& config = std::get<ClockConfig>(configVariant);
    spdlog::debug(
        "ClockControls: updateFromConfig called - font={}, timezone_index={}",
        static_cast<int>(config.font),
        config.timezone_index);

    // Prevent sending updates back to server during UI sync.
    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    // Update font dropdown.
    if (fontDropdown_) {
        lv_dropdown_set_selected(fontDropdown_, static_cast<uint16_t>(config.font));
        spdlog::debug("ClockControls: Updated font dropdown to index {}", static_cast<int>(config.font));
    }

    // Update timezone dropdown.
    if (timezoneDropdown_) {
        lv_dropdown_set_selected(timezoneDropdown_, config.timezone_index);
        spdlog::debug("ClockControls: Updated timezone dropdown to index {}", config.timezone_index);
    }

    // Update seconds switch.
    if (secondsSwitch_) {
        if (config.show_seconds) {
            lv_obj_add_state(secondsSwitch_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_remove_state(secondsSwitch_, LV_STATE_CHECKED);
        }
        spdlog::debug("ClockControls: Updated seconds switch to {}", config.show_seconds);
    }

    // Restore initializing state.
    if (!wasInitializing) {
        initializing_ = false;
    }
}

ClockConfig ClockControls::getCurrentConfig() const
{
    // Start with current config (preserves auto-scale settings).
    ClockConfig config = currentConfig_;

    // Get font from dropdown.
    if (fontDropdown_) {
        config.font = static_cast<ClockFont>(lv_dropdown_get_selected(fontDropdown_));
    }

    // Get timezone index from dropdown.
    if (timezoneDropdown_) {
        config.timezone_index = static_cast<uint8_t>(lv_dropdown_get_selected(timezoneDropdown_));
    }

    // Get show_seconds from switch.
    if (secondsSwitch_) {
        config.show_seconds = lv_obj_has_state(secondsSwitch_, LV_STATE_CHECKED);
    }

    // Populate display dimensions from getter for auto-scaling.
    if (dimensionsGetter_) {
        DisplayDimensions dims = dimensionsGetter_();
        config.target_display_width = dims.width;
        config.target_display_height = dims.height;
        config.auto_scale = true;
        spdlog::debug(
            "ClockControls: Setting display dimensions {}x{} for auto-scale",
            dims.width,
            dims.height);
    }

    return config;
}

void ClockControls::onFontChanged(lv_event_t* e)
{
    auto* controls =
        static_cast<ClockControls*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!controls)
        return;

    // Don't send updates during initialization.
    if (controls->isInitializing()) {
        spdlog::debug("ClockControls: Ignoring font change during initialization");
        return;
    }

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint16_t selectedIdx = lv_dropdown_get_selected(dropdown);

    // Order matches ClockFont enum: DotMatrix=0, Segment7=1, Segment7Large=2, Segment7Tall=3.
    static const char* fontNames[] = {"Dot Matrix", "7-Segment", "7-Segment Large", "7-Segment Tall"};
    spdlog::info("ClockControls: Font changed to index {} ({})", selectedIdx, fontNames[selectedIdx]);

    // Get complete current config and send update.
    ClockConfig config = controls->getCurrentConfig();
    controls->sendConfigUpdate(config);
}

void ClockControls::onTimezoneChanged(lv_event_t* e)
{
    auto* controls =
        static_cast<ClockControls*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!controls)
        return;

    // Don't send updates during initialization.
    if (controls->isInitializing()) {
        spdlog::debug("ClockControls: Ignoring timezone change during initialization");
        return;
    }

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint16_t selectedIdx = lv_dropdown_get_selected(dropdown);

    spdlog::info(
        "ClockControls: Timezone changed to index {} ({})",
        selectedIdx,
        ClockScenario::TIMEZONES[selectedIdx].label);

    // Get complete current config and send update.
    ClockConfig config = controls->getCurrentConfig();
    controls->sendConfigUpdate(config);
}

void ClockControls::onSecondsToggled(lv_event_t* e)
{
    ClockControls* self = static_cast<ClockControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("ClockControls: onSecondsToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->initializing_) {
        spdlog::debug("ClockControls: Ignoring seconds toggle during initialization");
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);

    spdlog::info("ClockControls: Show seconds toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    ClockConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

} // namespace Ui
} // namespace DirtSim
