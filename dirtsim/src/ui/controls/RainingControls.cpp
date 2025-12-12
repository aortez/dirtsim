#include "RainingControls.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

RainingControls::RainingControls(
    lv_obj_t* container, Network::WebSocketService* wsService, const RainingConfig& config)
    : ScenarioControlsBase(container, wsService, "raining")
{
    // Create widgets.
    createWidgets();

    // Initialize widget states from config.
    updateFromConfig(config);

    // Finish initialization - allow callbacks to send updates now.
    finishInitialization();

    spdlog::info("RainingControls: Initialized");
}

RainingControls::~RainingControls()
{
    // Base class handles container deletion.
    spdlog::info("RainingControls: Destroyed");
}

void RainingControls::createWidgets()
{
    // Scenario label.
    lv_obj_t* scenarioLabel = lv_label_create(controlsContainer_);
    lv_label_set_text(scenarioLabel, "--- Raining ---");

    // Rain rate toggle slider.
    rainControl_ = LVGLBuilder::toggleSlider(controlsContainer_)
                       .label("Rain Rate")
                       .range(0, 100)
                       .value(0)
                       .defaultValue(50)
                       .valueScale(1.0)
                       .valueFormat("%.0f")
                       .initiallyEnabled(false)
                       .sliderWidth(180)
                       .onToggle(onRainToggled, this)
                       .onSliderChange(onRainSliderChanged, this)
                       .buildOrLog();

    // Puddle floor toggle.
    puddleFloorSwitch_ = LVGLBuilder::labeledSwitch(controlsContainer_)
                             .label("Puddle Floor")
                             .initialState(false)
                             .callback(onPuddleFloorToggled, this)
                             .buildOrLog();
}

void RainingControls::updateFromConfig(const ScenarioConfig& configVariant)
{
    // Extract RainingConfig from variant.
    if (!std::holds_alternative<RainingConfig>(configVariant)) {
        spdlog::error("RainingControls: Invalid config type (expected RainingConfig)");
        return;
    }

    const RainingConfig& config = std::get<RainingConfig>(configVariant);

    // Prevent sending updates back to server during UI sync.
    bool wasInitializing = isInitializing();
    if (!wasInitializing) {
        initializing_ = true;
    }

    // Update rain control.
    if (rainControl_) {
        lv_obj_t* rainSwitch = lv_obj_get_child(rainControl_, 1);
        lv_obj_t* rainSlider = lv_obj_get_child(rainControl_, 2);

        if (rainSwitch && rainSlider) {
            // Update toggle state.
            bool shouldBeEnabled = config.rain_rate > 0.0;
            bool currentlyEnabled = lv_obj_has_state(rainSwitch, LV_STATE_CHECKED);

            if (shouldBeEnabled != currentlyEnabled) {
                if (shouldBeEnabled) {
                    lv_obj_add_state(rainSwitch, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_remove_state(rainSwitch, LV_STATE_CHECKED);
                }
                spdlog::debug("RainingControls: Updated rain toggle to {}", shouldBeEnabled);
            }

            // Update slider value if enabled.
            if (shouldBeEnabled) {
                int currentValue = lv_slider_get_value(rainSlider);
                int newValue = static_cast<int>(config.rain_rate);
                if (currentValue != newValue) {
                    lv_slider_set_value(rainSlider, newValue, LV_ANIM_OFF);
                    spdlog::debug("RainingControls: Updated rain slider to {}", config.rain_rate);
                }
            }
        }
    }

    // Update puddle floor switch.
    if (puddleFloorSwitch_) {
        bool currentState = lv_obj_has_state(puddleFloorSwitch_, LV_STATE_CHECKED);
        if (currentState != config.puddle_floor) {
            if (config.puddle_floor) {
                lv_obj_add_state(puddleFloorSwitch_, LV_STATE_CHECKED);
            }
            else {
                lv_obj_remove_state(puddleFloorSwitch_, LV_STATE_CHECKED);
            }
            spdlog::debug("RainingControls: Updated puddle floor to {}", config.puddle_floor);
        }
    }

    // Restore initializing state.
    if (!wasInitializing) {
        initializing_ = false;
    }
}

RainingConfig RainingControls::getCurrentConfig() const
{
    RainingConfig config;

    // Get rain rate from control.
    if (rainControl_) {
        lv_obj_t* rainSwitch = lv_obj_get_child(rainControl_, 1);
        lv_obj_t* rainSlider = lv_obj_get_child(rainControl_, 2);

        if (rainSwitch && rainSlider) {
            bool enabled = lv_obj_has_state(rainSwitch, LV_STATE_CHECKED);
            if (enabled) {
                int value = lv_slider_get_value(rainSlider);
                config.rain_rate = static_cast<double>(value);
            }
            else {
                config.rain_rate = 0.0;
            }
        }
    }

    // Get puddle floor state.
    if (puddleFloorSwitch_) {
        config.puddle_floor = lv_obj_has_state(puddleFloorSwitch_, LV_STATE_CHECKED);
    }

    return config;
}

void RainingControls::onRainToggled(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    RainingControls* self = static_cast<RainingControls*>(lv_obj_get_user_data(target));
    if (!self) return;

    // Don't send updates during initialization.
    if (self->isInitializing()) {
        spdlog::debug("RainingControls: Ignoring rain toggle during initialization");
        return;
    }

    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    spdlog::info("RainingControls: Rain toggled to {}", enabled ? "ON" : "OFF");

    // Get current config and send update.
    RainingConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void RainingControls::onRainSliderChanged(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    RainingControls* self = static_cast<RainingControls*>(lv_obj_get_user_data(target));
    if (!self) return;

    // Don't send updates during initialization.
    if (self->isInitializing()) {
        spdlog::debug("RainingControls: Ignoring rain slider during initialization");
        return;
    }

    int value = lv_slider_get_value(target);
    double rainRate = static_cast<double>(value);

    spdlog::info("RainingControls: Rain rate changed to {:.0f}", rainRate);

    // Get complete current config and send update.
    RainingConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

void RainingControls::onPuddleFloorToggled(lv_event_t* e)
{
    RainingControls* self = static_cast<RainingControls*>(lv_event_get_user_data(e));
    if (!self) {
        spdlog::error("RainingControls: onPuddleFloorToggled called with null self");
        return;
    }

    // Don't send updates during initialization.
    if (self->isInitializing()) {
        spdlog::debug("RainingControls: Ignoring puddle floor toggle during initialization");
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    spdlog::info("RainingControls: Puddle Floor toggled to {}", enabled ? "ON" : "OFF");

    // Get complete current config and send update.
    RainingConfig config = self->getCurrentConfig();
    self->sendConfigUpdate(config);
}

} // namespace Ui
} // namespace DirtSim
