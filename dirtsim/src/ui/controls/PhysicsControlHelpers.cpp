#include "PhysicsControlHelpers.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/PhysicsSettingsGet.h"
#include "server/api/PhysicsSettingsSet.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <atomic>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {
namespace PhysicsControlHelpers {

AllColumnConfigs createAllColumnConfigs()
{
    AllColumnConfigs configs;

    configs.generalPhysics = { .title = "General Physics",
                               .controls = { { .label = "Timescale",
                                               .type = ControlType::TOGGLE_SLIDER,
                                               .rangeMin = -500,
                                               .rangeMax = 1000,
                                               .defaultValue = 100,
                                               .valueScale = 0.01,
                                               .valueFormat = "%.2fx",
                                               .initiallyEnabled = true,
                                               .valueSetter =
                                                   [](PhysicsSettings& s, double v) { s.timescale = v; },
                                               .valueGetter =
                                                   [](const PhysicsSettings& s) { return s.timescale; },
                                               .enableSetter =
                                                   [](PhysicsSettings& s, bool e) {
                                                       if (!e) s.timescale = 0.0;
                                                   },
                                               .enableGetter =
                                                   [](const PhysicsSettings& s) {
                                                       return s.timescale > 0.0;
                                                   } },
                                             { .label = "Gravity",
                                               .type = ControlType::TOGGLE_SLIDER,
                                               .rangeMin = -5000,
                                               .rangeMax = 50000,
                                               .defaultValue = 981,
                                               .valueScale = 0.01,
                                               .valueFormat = "%.2f",
                                               .initiallyEnabled = true,
                                               .valueSetter =
                                                   [](PhysicsSettings& s, double v) { s.gravity = v; },
                                               .valueGetter =
                                                   [](const PhysicsSettings& s) { return s.gravity; },
                                               .enableSetter =
                                                   [](PhysicsSettings& s, bool e) {
                                                       if (!e) s.gravity = 0.0;
                                                   },
                                               .enableGetter =
                                                   [](const PhysicsSettings& s) {
                                                       return s.gravity != 0.0;
                                                   } },
                                             { .label = "Elasticity",
                                               .type = ControlType::TOGGLE_SLIDER,
                                               .rangeMin = 0,
                                               .rangeMax = 100,
                                               .defaultValue = 80,
                                               .valueScale = 0.01,
                                               .valueFormat = "%.2f",
                                               .initiallyEnabled = true,
                                               .valueSetter =
                                                   [](PhysicsSettings& s, double v) { s.elasticity = v; },
                                               .valueGetter =
                                                   [](const PhysicsSettings& s) {
                                                       return s.elasticity;
                                                   },
                                               .enableSetter = []([[maybe_unused]] PhysicsSettings& s,
                                                                  [[maybe_unused]] bool e) {},
                                               .enableGetter =
                                                   []([[maybe_unused]] const PhysicsSettings& s) {
                                                       return true;
                                                   } },
                                             { .label = "Air Resistance",
                                               .type = ControlType::TOGGLE_SLIDER,
                                               .rangeMin = 0,
                                               .rangeMax = 100,
                                               .defaultValue = 10,
                                               .valueScale = 0.01,
                                               .valueFormat = "%.2f",
                                               .initiallyEnabled = true,
                                               .valueSetter = [](PhysicsSettings& s, double v) {
                                                   s.air_resistance = v;
                                               },
                                               .valueGetter =
                                                   [](const PhysicsSettings& s) {
                                                       return s.air_resistance;
                                                   },
                                               .enableSetter = []([[maybe_unused]] PhysicsSettings& s,
                                                                  [[maybe_unused]] bool e) {},
                                               .enableGetter =
                                                   []([[maybe_unused]] const PhysicsSettings& s) {
                                                       return true;
                                                   } },
                                             { .label = "Enable Swap",
                                               .type = ControlType::SWITCH_ONLY,
                                               .enableSetter =
                                                   [](PhysicsSettings& s, bool e) { s.swap_enabled = e; },
                                               .enableGetter = [](const PhysicsSettings& s) {
                                                   return s.swap_enabled;
                                               } } } };

    configs.pressure = {
        .title = "Pressure",
        .controls = { { .label = "Hydrostatic",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 300,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.pressure_hydrostatic_strength = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.pressure_hydrostatic_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                s.pressure_hydrostatic_enabled = e;
                                if (!e) s.pressure_hydrostatic_strength = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.pressure_hydrostatic_enabled; } },
                      { .label = "Dynamic",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 300,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.pressure_dynamic_strength = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.pressure_dynamic_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                s.pressure_dynamic_enabled = e;
                                if (!e) s.pressure_dynamic_strength = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.pressure_dynamic_enabled; } },
                      { .label = "Diffusion",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 50000,
                        .defaultValue = 500,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.pressure_diffusion_strength = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.pressure_diffusion_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                if (!e) s.pressure_diffusion_strength = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) {
                                return s.pressure_diffusion_strength > 0.0;
                            } },
                      { .label = "Diffusion Iters",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 1,
                        .rangeMax = 5,
                        .defaultValue = 1,
                        .valueScale = 1.0,
                        .valueFormat = "%.0f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.pressure_diffusion_iterations = static_cast<int>(v);
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return static_cast<double>(s.pressure_diffusion_iterations);
                            },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Scale",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 500,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.pressure_scale = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.pressure_scale; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                if (!e) s.pressure_scale = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.pressure_scale > 0.0; } } }
    };

    configs.forces = {
        .title = "Forces",
        .controls = { { .label = "Cohesion",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 2000,
                        .defaultValue = 0,
                        .valueScale = 0.01,
                        .valueFormat = "%.0f",
                        .initiallyEnabled = true,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.cohesion_strength = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.cohesion_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                s.cohesion_enabled = e;
                                if (!e) s.cohesion_strength = 0.0;
                            },
                        .enableGetter = [](const PhysicsSettings& s) { return s.cohesion_enabled; } },
                      { .label = "Adhesion",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 1000,
                        .defaultValue = 500,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.adhesion_strength = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.adhesion_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                s.adhesion_enabled = e;
                                if (!e) s.adhesion_strength = 0.0;
                            },
                        .enableGetter = [](const PhysicsSettings& s) { return s.adhesion_enabled; } },
                      { .label = "Viscosity",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 1000,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.viscosity_strength = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.viscosity_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                s.viscosity_enabled = e;
                                if (!e) s.viscosity_strength = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.viscosity_enabled; } },
                      { .label = "Friction",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 200,
                        .defaultValue = 100,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter = [](PhysicsSettings& s, double v) { s.friction_strength = v; },
                        .valueGetter = [](const PhysicsSettings& s) { return s.friction_strength; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                s.friction_enabled = e;
                                if (!e) s.friction_strength = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.friction_enabled; } },
                      { .label = "Cohesion Resist",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 10,
                        .valueScale = 1.0,
                        .valueFormat = "%.0f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.cohesion_resistance_factor = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.cohesion_resistance_factor; },
                        .enableSetter =
                            [](PhysicsSettings& s, bool e) {
                                if (!e) s.cohesion_resistance_factor = 0.0;
                            },
                        .enableGetter =
                            [](const PhysicsSettings& s) {
                                return s.cohesion_resistance_factor > 0.0;
                            } } }
    };

    configs.swapTuning = {
        .title = "Swap Tuning",
        .controls = { { .label = "Buoyancy Energy",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 2000,
                        .defaultValue = 500,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.buoyancy_energy_scale = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.buoyancy_energy_scale; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Cohesion Bonds",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 5000,
                        .defaultValue = 2000,
                        .valueScale = 0.01,
                        .valueFormat = "%.0f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.cohesion_resistance_factor = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.cohesion_resistance_factor; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Horizontal Flow Resist",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 2000,
                        .defaultValue = 50,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.horizontal_flow_resistance_factor = v;
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return s.horizontal_flow_resistance_factor;
                            },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Fluid Lubrication",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 50,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.fluid_lubrication_factor = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.fluid_lubrication_factor; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } } }
    };

    configs.swap2 = {
        .title = "Swap2",
        .controls = { { .label = "Horizontal Non-Fluid Penalty",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 10,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.horizontal_non_fluid_penalty = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.horizontal_non_fluid_penalty; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Horizontal Target Resist",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 1000,
                        .defaultValue = 200,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.horizontal_non_fluid_target_resistance = v;
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return s.horizontal_non_fluid_target_resistance;
                            },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Horiz Non-Fluid Energy",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 10000,
                        .defaultValue = 400,
                        .valueScale = 0.01,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) {
                                s.horizontal_non_fluid_energy_multiplier = v;
                            },
                        .valueGetter =
                            [](const PhysicsSettings& s) {
                                return s.horizontal_non_fluid_energy_multiplier;
                            },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } } }
    };

    configs.frag = {
        .title = "Frag",
        .controls = { { .label = "Enabled",
                        .type = ControlType::SWITCH_ONLY,
                        .enableSetter = [](PhysicsSettings& s, bool e) { s.fragmentation_enabled = e; },
                        .enableGetter =
                            [](const PhysicsSettings& s) { return s.fragmentation_enabled; } },
                      { .label = "Threshold",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 500,
                        .defaultValue = 50,
                        .valueScale = 0.1,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.fragmentation_threshold = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.fragmentation_threshold; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Full Threshold",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 1000,
                        .defaultValue = 100,
                        .valueScale = 0.1,
                        .valueFormat = "%.1f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.fragmentation_full_threshold = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.fragmentation_full_threshold; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } },
                      { .label = "Spray Fraction",
                        .type = ControlType::TOGGLE_SLIDER,
                        .rangeMin = 0,
                        .rangeMax = 100,
                        .defaultValue = 40,
                        .valueScale = 0.01,
                        .valueFormat = "%.2f",
                        .initiallyEnabled = true,
                        .valueSetter =
                            [](PhysicsSettings& s, double v) { s.fragmentation_spray_fraction = v; },
                        .valueGetter =
                            [](const PhysicsSettings& s) { return s.fragmentation_spray_fraction; },
                        .enableSetter =
                            []([[maybe_unused]] PhysicsSettings& s, [[maybe_unused]] bool e) {},
                        .enableGetter = []([[maybe_unused]] const PhysicsSettings& s) {
                            return true;
                        } } }
    };

    return configs;
}

size_t createControlsFromColumn(lv_obj_t* parent,
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

        if (controlConfig.type == ControlType::TOGGLE_SLIDER) {
            control.widget = LVGLBuilder::toggleSlider(parent)
                                 .label(controlConfig.label)
                                 .range(controlConfig.rangeMin, controlConfig.rangeMax)
                                 .value(controlConfig.defaultValue)
                                 .defaultValue(controlConfig.defaultValue)
                                 .valueScale(controlConfig.valueScale)
                                 .valueFormat(controlConfig.valueFormat)
                                 .initiallyEnabled(controlConfig.initiallyEnabled)
                                 .sliderWidth(180)
                                 .onToggle(toggleCallback, callbackUserData)
                                 .onSliderChange(sliderCallback, callbackUserData)
                                 .buildOrLog();

            if (control.widget) {
                control.switchWidget = lv_obj_get_child(control.widget, 0);
                control.sliderWidget = lv_obj_get_child(control.widget, 2);

                if (control.sliderWidget) {
                    lv_obj_add_event_cb(
                        control.sliderWidget, sliderCallback, LV_EVENT_RELEASED, callbackUserData);
                }

                widgetToControl[control.widget] = &control;
                if (control.switchWidget) {
                    widgetToControl[control.switchWidget] = &control;
                }
                if (control.sliderWidget) {
                    widgetToControl[control.sliderWidget] = &control;
                }
            }
        }
        else if (controlConfig.type == ControlType::SWITCH_ONLY) {
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

        index++;
    }

    return index - startIndex;
}

Control* findControl(lv_obj_t* widget,
                      std::unordered_map<lv_obj_t*, Control*>& widgetToControl)
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

void updateControlsFromSettings(Control* controlsArray,
                                 size_t controlCount,
                                 const PhysicsSettings& settings)
{
    auto updateToggleSlider = [](Control* control, double value, bool enabled) {
        if (!control || !control->widget) return;

        if (control->config.type == ControlType::TOGGLE_SLIDER) {
            lv_obj_t* toggle = lv_obj_get_child(control->widget, 0);
            lv_obj_t* slider = lv_obj_get_child(control->widget, 2);
            lv_obj_t* valueLabel = lv_obj_get_child(control->widget, 3);

            if (toggle) {
                if (enabled) {
                    lv_obj_add_state(toggle, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_remove_state(toggle, LV_STATE_CHECKED);
                }
            }

            if (slider) {
                int sliderValue = static_cast<int>(value / control->config.valueScale);
                lv_slider_set_value(slider, sliderValue, LV_ANIM_OFF);

                if (valueLabel) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), control->config.valueFormat, value);
                    lv_label_set_text(valueLabel, buf);
                }
            }
        }
        else if (control->config.type == ControlType::SWITCH_ONLY) {
            if (control->switchWidget) {
                if (enabled) {
                    lv_obj_add_state(control->switchWidget, LV_STATE_CHECKED);
                }
                else {
                    lv_obj_remove_state(control->switchWidget, LV_STATE_CHECKED);
                }
            }
        }
    };

    for (size_t i = 0; i < controlCount; i++) {
        Control& control = controlsArray[i];
        if (control.config.valueGetter && control.config.enableGetter) {
            double value = control.config.valueGetter(settings);
            bool enabled = control.config.enableGetter(settings);
            updateToggleSlider(&control, value, enabled);
        }
        else if (control.config.enableGetter) {
            bool enabled = control.config.enableGetter(settings);
            updateToggleSlider(&control, 0.0, enabled);
        }
    }
}

void syncSettingsToServer(Network::WebSocketService* wsService, const PhysicsSettings& settings)
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

PhysicsSettings fetchSettingsFromServer(Network::WebSocketService* wsService)
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
