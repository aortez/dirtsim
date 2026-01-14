#include "PhysicsControlHelpers.h"
#include "core/ColorNames.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/PhysicsSettingsGet.h"
#include "server/api/PhysicsSettingsSet.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <atomic>
#include <spdlog/spdlog.h>

namespace {

// Sun color presets.
struct ColorPreset {
    const char* name;
    uint32_t color;
};

const ColorPreset sunColorPresets[] = {
    { "Warm Sunlight", 0 }, // Filled in at runtime from ColorNames.
    { "Cool Moonlight", 0 }, { "Torch Orange", 0 }, { "Candle Yellow", 0 }, { "White", 0 },
};
const int sunColorPresetCount = 5;

const ColorPreset ambientColorPresets[] = {
    { "Day", 0 },
    { "Dusk", 0 },
    { "Night", 0 },
    { "Cave", 0 },
};
const int ambientColorPresetCount = 4;

// Initialize color values from ColorNames.
uint32_t getSunColorByIndex(int index)
{
    switch (index) {
        case 0:
            return ColorNames::warmSunlight();
        case 1:
            return ColorNames::coolMoonlight();
        case 2:
            return ColorNames::torchOrange();
        case 3:
            return ColorNames::candleYellow();
        case 4:
            return ColorNames::white();
        default:
            return ColorNames::warmSunlight();
    }
}

uint32_t getAmbientColorByIndex(int index)
{
    switch (index) {
        case 0:
            return ColorNames::dayAmbient();
        case 1:
            return ColorNames::duskAmbient();
        case 2:
            return ColorNames::nightAmbient();
        case 3:
            return ColorNames::caveAmbient();
        default:
            return ColorNames::dayAmbient();
    }
}

int getSunColorIndex(uint32_t color)
{
    for (int i = 0; i < sunColorPresetCount; i++) {
        if (getSunColorByIndex(i) == color) return i;
    }
    return 0; // Default to first if not found.
}

int getAmbientColorIndex(uint32_t color)
{
    for (int i = 0; i < ambientColorPresetCount; i++) {
        if (getAmbientColorByIndex(i) == color) return i;
    }
    return 0; // Default to first if not found.
}

} // anonymous namespace

namespace DirtSim {
namespace Ui {
namespace PhysicsControlHelpers {

AllColumnConfigs createAllColumnConfigs()
{
    AllColumnConfigs configs;

    configs.generalPhysics = {
        .title = "General Physics",
        .controls = { { .label = "Timescale",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = -500,
                        .rangeMax = 1000,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2fx",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.timescale = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.timescale; } },
                      { .label = "Gravity",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = -5000,
                        .rangeMax = 50000,
                        .defaultValue = 981,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 50,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.gravity = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.gravity; } },
                      { .label = "Elasticity",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 80,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.elasticity = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.elasticity; } },
                      { .label = "Air Resistance",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 10,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.air_resistance = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.air_resistance; } },
                      { .label = "Enable Swap",
                        .type = ControlType::SWITCH_ONLY,
                        .enableSetter = [](PhysicsSettings& s, bool e) { s.swap_enabled = e; },
                        .enableGetter = [](const PhysicsSettings& s) { return s.swap_enabled; } } }
    };

    configs.pressure = {
        .title = "Pressure",
        .controls = { { .label = "Hydrostatic",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 300,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.pressure_hydrostatic_strength = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return s.pressure_hydrostatic_strength;
                            } },
                      { .label = "Dynamic",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 300,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.pressure_dynamic_strength = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.pressure_dynamic_strength; } },
                      { .label = "Diffusion",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 50000,
                        .defaultValue = 500,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 100,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.pressure_diffusion_strength = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return s.pressure_diffusion_strength;
                            } },
                      { .label = "Diffusion Iters",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 1,
                        .rangeMax = 5,
                        .defaultValue = 1,
                        .valueScale = 1.0,
                        .valueFormat = "%.0f",
                        .step = 1,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.pressure_diffusion_iterations = static_cast<int>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.pressure_diffusion_iterations);
                            } },
                      { .label = "Scale",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 500,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.pressure_scale = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.pressure_scale; } } }
    };

    configs.forces = { .title = "Forces",
                       .controls = {
                           { .label = "Cohesion",
                             .type = ControlType::ACTION_STEPPER,
                             .rangeMin = 0,
                             .rangeMax = 2000,
                             .defaultValue = 0,
                             .valueScale = 0.01,
                             .valueFormat = "%.0f",
                             .step = 100,
                             .valueSetter = [](PhysicsSettings& s,
                                               double v) { s.cohesion_strength = v; },
                             .valueGetter =
                                 [](const PhysicsSettings& s) { return s.cohesion_strength; } },
                           { .label = "Adhesion",
                             .type = ControlType::ACTION_STEPPER,
                             .rangeMin = 0,
                             .rangeMax = 1000,
                             .defaultValue = 500,
                             .valueScale = 0.01,
                             .valueFormat = "%.1f",
                             .step = 10,
                             .valueSetter = [](PhysicsSettings& s,
                                               double v) { s.adhesion_strength = v; },
                             .valueGetter =
                                 [](const PhysicsSettings& s) { return s.adhesion_strength; } },
                           { .label = "Viscosity",
                             .type = ControlType::ACTION_STEPPER,
                             .rangeMin = 0,
                             .rangeMax = 1000,
                             .defaultValue = 100,
                             .valueScale = 0.01,
                             .valueFormat = "%.2f",
                             .step = 10,
                             .valueSetter = [](PhysicsSettings& s,
                                               double v) { s.viscosity_strength = v; },
                             .valueGetter =
                                 [](const PhysicsSettings& s) { return s.viscosity_strength; } },
                           { .label = "Friction",
                             .type = ControlType::ACTION_STEPPER,
                             .rangeMin = 0,
                             .rangeMax = 200,
                             .defaultValue = 100,
                             .valueScale = 0.01,
                             .valueFormat = "%.2f",
                             .step = 5,
                             .valueSetter = [](PhysicsSettings& s,
                                               double v) { s.friction_strength = v; },
                             .valueGetter =
                                 [](const PhysicsSettings& s) { return s.friction_strength; } },
                           { .label = "Cohesion Resist",
                             .type = ControlType::ACTION_STEPPER,
                             .rangeMin = 0,
                             .rangeMax = 100,
                             .defaultValue = 10,
                             .valueScale = 1.0,
                             .valueFormat = "%.0f",
                             .step = 1,
                             .valueSetter = [](PhysicsSettings& s,
                                               double v) { s.cohesion_resistance_factor = v; },
                             .valueGetter =
                                 [](const PhysicsSettings& s) {
                                     return s.cohesion_resistance_factor;
                                 } } } };

    configs.light = {
        .title = "Light",
        .controls = { { .label = "Sun On",
                        .type = ControlType::SWITCH_ONLY,
                        .enableSetter = [](PhysicsSettings& s, bool e) { s.light.sun_enabled = e; },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.light.sun_enabled; } },
                      { .label = "Sun",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 1000,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.light.sun_intensity = static_cast<float>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.light.sun_intensity);
                            } },
                      { .label = "SunC",
                        .type = ControlType::DROPDOWN,
                        .dropdownOptions =
                            "Warm Sunlight\nCool Moonlight\nTorch Orange\nCandle Yellow\nWhite",
                        .indexSetter = [](PhysicsSettings& s,
                                          int idx) { s.light.sun_color = getSunColorByIndex(idx); },
                        .indexGetter =
                            [](const PhysicsSettings& s) {
                                return getSunColorIndex(s.light.sun_color);
                            } },
                      { .label = "Ambient",
                        .type = ControlType::DROPDOWN,
                        .dropdownOptions = "Day\nDusk\nNight\nCave",
                        .indexSetter =
                            [](PhysicsSettings& s, int idx) {
                                s.light.ambient_color = getAmbientColorByIndex(idx);
                            },
                        .indexGetter =
                            [](const PhysicsSettings& s) {
                                return getAmbientColorIndex(s.light.ambient_color);
                            } },
                      { .label = "Ambient",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 1000,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.light.ambient_intensity = static_cast<float>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.light.ambient_intensity);
                            } },
                      { .label = "Sky Falloff",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 200,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.light.sky_access_falloff = static_cast<float>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.light.sky_access_falloff);
                            } },
                      { .label = "D Iters",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 10,
                        .defaultValue = 2,
                        .valueScale = 1.0,
                        .valueFormat = "%.0f",
                        .step = 1,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.light.diffusion_iterations = static_cast<int>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.light.diffusion_iterations);
                            } },
                      { .label = "Diffusion",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 30,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.light.diffusion_rate = static_cast<float>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.light.diffusion_rate);
                            } },
                      { .label = "Air Scatter",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 15,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.light.air_scatter_rate = static_cast<float>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.light.air_scatter_rate);
                            } } }
    };

    configs.swapTuning = {
        .title = "Swap Tuning",
        .controls = { { .label = "Buoyancy Energy",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 2000,
                        .defaultValue = 500,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .step = 10,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.buoyancy_energy_scale = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.buoyancy_energy_scale; } },
                      { .label = "Cohesion Bonds",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 5000,
                        .defaultValue = 2000,
                        .valueScale = 0.01,
                        .valueFormat = "%.0f",
                        .step = 100,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.cohesion_resistance_factor = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.cohesion_resistance_factor; } },
                      { .label = "Horizontal Flow Resist",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 2000,
                        .defaultValue = 50,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .step = 10,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.horizontal_flow_resistance_factor = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return s.horizontal_flow_resistance_factor;
                            } },
                      { .label = "Fluid Lubrication",
                        .type = ControlType::ACTION_STEPPER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 50,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .step = 5,
                        .valueSetter = [](PhysicsSettings& s,
                                          double v) { s.fluid_lubrication_factor = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.fluid_lubrication_factor; } } }
    };

    configs.swap2 = { .title = "Swap2",
                      .controls = {
                          { .label = "Horizontal Non-Fluid Penalty",
                            .type = ControlType::ACTION_STEPPER,
                            .rangeMin = 0,
                            .rangeMax = 100,
                            .defaultValue = 10,
                            .valueScale = 0.01,
                            .valueFormat = "%.2f",
                            .step = 5,
                            .valueSetter = [](PhysicsSettings& s,
                                              double v) { s.horizontal_non_fluid_penalty = v; },
                            .valueGetter =
                                [](const PhysicsSettings& s) {
                                    return s.horizontal_non_fluid_penalty;
                                } },
                          { .label = "Horizontal Target Resist",
                            .type = ControlType::ACTION_STEPPER,
                            .rangeMin = 0,
                            .rangeMax = 1000,
                            .defaultValue = 200,
                            .valueScale = 0.01,
                            .valueFormat = "%.1f",
                            .step = 10,
                            .valueSetter =
                                [](PhysicsSettings& s, double v) {
                                    s.horizontal_non_fluid_target_resistance = v;
                                },
                            .valueGetter =
                                [](const PhysicsSettings& s) {
                                    return s.horizontal_non_fluid_target_resistance;
                                } },
                          { .label = "Horiz Non-Fluid Energy",
                            .type = ControlType::ACTION_STEPPER,
                            .rangeMin = 0,
                            .rangeMax = 10000,
                            .defaultValue = 400,
                            .valueScale = 0.01,
                            .valueFormat = "%.1f",
                            .step = 100,
                            .valueSetter =
                                [](PhysicsSettings& s, double v) {
                                    s.horizontal_non_fluid_energy_multiplier = v;
                                },
                            .valueGetter =
                                [](const PhysicsSettings& s) {
                                    return s.horizontal_non_fluid_energy_multiplier;
                                } } } };

    configs.frag = { .title = "Frag",
                     .controls = {
                         { .label = "Enabled",
                           .type = ControlType::SWITCH_ONLY,
                           .enableSetter = [](PhysicsSettings& s,
                                              bool e) { s.fragmentation_enabled = e; },
                           .enableGetter =
                               [](const PhysicsSettings& s) { return s.fragmentation_enabled; } },
                         { .label = "Threshold",
                           .type = ControlType::ACTION_STEPPER,
                           .rangeMin = 0,
                           .rangeMax = 500,
                           .defaultValue = 50,
                           .valueScale = 0.1,
                           .valueFormat = "%.1f",
                           .step = 1,
                           .valueSetter = [](PhysicsSettings& s,
                                             double v) { s.fragmentation_threshold = v; },
                           .valueGetter =
                               [](const PhysicsSettings& s) { return s.fragmentation_threshold; } },
                         { .label = "Full Threshold",
                           .type = ControlType::ACTION_STEPPER,
                           .rangeMin = 0,
                           .rangeMax = 1000,
                           .defaultValue = 100,
                           .valueScale = 0.1,
                           .valueFormat = "%.1f",
                           .step = 1,
                           .valueSetter = [](PhysicsSettings& s,
                                             double v) { s.fragmentation_full_threshold = v; },
                           .valueGetter =
                               [](const PhysicsSettings& s) {
                                   return s.fragmentation_full_threshold;
                               } },
                         { .label = "Spray Fraction",
                           .type = ControlType::ACTION_STEPPER,
                           .rangeMin = 0,
                           .rangeMax = 100,
                           .defaultValue = 40,
                           .valueScale = 0.01,
                           .valueFormat = "%.2f",
                           .step = 5,
                           .valueSetter = [](PhysicsSettings& s,
                                             double v) { s.fragmentation_spray_fraction = v; },
                           .valueGetter =
                               [](const PhysicsSettings& s) {
                                   return s.fragmentation_spray_fraction;
                               } } } };

    return configs;
}

size_t createControlsFromColumn(
    lv_obj_t* parent,
    const ColumnConfig& config,
    Control* controlsArray,
    size_t startIndex,
    std::unordered_map<lv_obj_t*, Control*>& widgetToControl,
    lv_event_cb_t toggleCallback,
    lv_event_cb_t sliderCallback,
    void* callbackUserData)
{
    size_t index = startIndex;
    for (const auto& controlConfig : config.controls) {
        Control& control = controlsArray[index];
        control.config = controlConfig;

        if (controlConfig.type == ControlType::SWITCH_ONLY) {
            control.widget = LVGLBuilder::labeledSwitch(parent)
                                 .label(controlConfig.label)
                                 .initialState(controlConfig.initiallyEnabled)
                                 .callback(toggleCallback, callbackUserData)
                                 .buildOrLog();

            if (control.widget) {
                control.switchWidget = control.widget;
                widgetToControl[control.switchWidget] = &control;
            }
        }
        else if (controlConfig.type == ControlType::ACTION_STEPPER) {
            control.widget = LVGLBuilder::actionStepper(parent)
                                 .label(controlConfig.label)
                                 .range(controlConfig.rangeMin, controlConfig.rangeMax)
                                 .step(controlConfig.step)
                                 .value(controlConfig.defaultValue)
                                 .valueFormat(controlConfig.valueFormat)
                                 .valueScale(controlConfig.valueScale)
                                 .width(LV_PCT(95))
                                 .callback(sliderCallback, callbackUserData)
                                 .buildOrLog();

            if (control.widget) {
                control.stepperWidget = control.widget;
                widgetToControl[control.widget] = &control;
            }
        }
        else if (controlConfig.type == ControlType::DROPDOWN) {
            control.widget = LVGLBuilder::actionDropdown(parent)
                                 .label(controlConfig.label)
                                 .options(controlConfig.dropdownOptions)
                                 .selected(0)
                                 .width(LV_PCT(95))
                                 .callback(sliderCallback, callbackUserData)
                                 .buildOrLog();

            if (control.widget) {
                control.dropdownWidget = lv_obj_get_child(control.widget, 1);
                widgetToControl[control.widget] = &control;
                if (control.dropdownWidget) {
                    widgetToControl[control.dropdownWidget] = &control;
                }
            }
        }

        index++;
    }

    return index - startIndex;
}

Control* findControl(lv_obj_t* widget, std::unordered_map<lv_obj_t*, Control*>& widgetToControl)
{
    auto it = widgetToControl.find(widget);
    if (it != widgetToControl.end()) {
        return it->second;
    }

    lv_obj_t* parent = lv_obj_get_parent(widget);
    if (parent) {
        it = widgetToControl.find(parent);
        if (it != widgetToControl.end()) {
            return it->second;
        }

        lv_obj_t* grandparent = lv_obj_get_parent(parent);
        if (grandparent) {
            it = widgetToControl.find(grandparent);
            if (it != widgetToControl.end()) {
                return it->second;
            }
        }
    }

    return nullptr;
}

void updateControlsFromSettings(
    Control* controlsArray, size_t controlCount, const PhysicsSettings& settings)
{
    for (size_t i = 0; i < controlCount; i++) {
        Control& control = controlsArray[i];

        // Handle ACTION_STEPPER controls.
        if (control.config.type == ControlType::ACTION_STEPPER) {
            if (control.config.valueGetter && control.stepperWidget) {
                double value = control.config.valueGetter(settings);
                int32_t stepperValue = static_cast<int32_t>(value / control.config.valueScale);
                LVGLBuilder::ActionStepperBuilder::setValue(control.stepperWidget, stepperValue);
            }
            continue;
        }

        // Handle DROPDOWN controls.
        if (control.config.type == ControlType::DROPDOWN) {
            if (control.config.indexGetter && control.dropdownWidget) {
                int selectedIndex = control.config.indexGetter(settings);
                lv_dropdown_set_selected(
                    control.dropdownWidget, static_cast<uint16_t>(selectedIndex));
            }
            continue;
        }

        // Handle SWITCH_ONLY controls.
        if (control.config.type == ControlType::SWITCH_ONLY) {
            if (control.config.enableGetter && control.switchWidget) {
                bool enabled = control.config.enableGetter(settings);
                if (enabled) {
                    lv_obj_add_state(control.switchWidget, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_remove_state(control.switchWidget, LV_STATE_CHECKED);
                }
            }
        }
    }
}

void syncSettingsToServer(
    Network::WebSocketServiceInterface* wsService, const PhysicsSettings& settings)
{
    if (!wsService || !wsService->isConnected()) {
        LOG_WARN(Controls, "Cannot sync settings - not connected");
        return;
    }

    LOG_DEBUG(Controls, "Syncing physics settings to server");

    static std::atomic<uint64_t> nextId{ 1 };
    const Api::PhysicsSettingsSet::Command cmd{ .settings = settings };
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);

    auto result = wsService->sendBinary(Network::serialize_envelope(envelope));
    if (result.isError()) {
        LOG_ERROR(Controls, "Failed to send PhysicsSettingsSet: {}", result.errorValue());
    }
}

PhysicsSettings fetchSettingsFromServer(Network::WebSocketServiceInterface* wsService)
{
    if (!wsService || !wsService->isConnected()) {
        LOG_WARN(Controls, "Cannot fetch settings - not connected");
        return getDefaultPhysicsSettings();
    }

    LOG_INFO(Controls, "Fetching physics settings from server");

    static std::atomic<uint64_t> nextId{ 1 };
    const Api::PhysicsSettingsGet::Command cmd{};
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);

    auto envelopeResult = wsService->sendBinaryAndReceive(envelope, 1000);
    if (envelopeResult.isError()) {
        LOG_ERROR(Controls, "Failed to send command: {}", envelopeResult.errorValue());
        return getDefaultPhysicsSettings();
    }

    auto response =
        Network::extract_result<Api::PhysicsSettingsGet::Okay, ApiError>(envelopeResult.value());

    if (response.isValue()) {
        const auto& settings = response.value().settings;
        LOG_INFO(Controls, "Received settings from server (gravity={:.2f})", settings.gravity);
        return settings;
    }
    else {
        LOG_ERROR(Controls, "Server error: {}", response.errorValue().message);
        return getDefaultPhysicsSettings();
    }
}

} // namespace PhysicsControlHelpers
} // namespace Ui
} // namespace DirtSim
