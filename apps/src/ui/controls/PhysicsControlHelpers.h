#pragma once

#include "core/PhysicsSettings.h"
#include "lvgl/lvgl.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {
namespace PhysicsControlHelpers {

/**
 * @brief Shared control types and configurations for physics control panels.
 *
 * This namespace provides reusable data structures and helper functions for
 * creating physics control panels. GeneralPhysicsPanel, PressurePanel, and
 * ForcesPanel all use these helpers to build their controls from column configs.
 */

enum class ControlType { TOGGLE_SLIDER, SWITCH_ONLY };

struct ControlConfig {
    const char* label;
    ControlType type;

    int rangeMin = 0;
    int rangeMax = 100;
    int defaultValue = 50;
    double valueScale = 1.0;
    const char* valueFormat = "%.1f";
    bool initiallyEnabled = false;

    std::function<void(PhysicsSettings&, double)> valueSetter = nullptr;
    std::function<double(const PhysicsSettings&)> valueGetter = nullptr;
    std::function<void(PhysicsSettings&, bool)> enableSetter = nullptr;
    std::function<bool(const PhysicsSettings&)> enableGetter = nullptr;
};

struct ColumnConfig {
    const char* title;
    std::vector<ControlConfig> controls;
};

struct Control {
    ControlConfig config;
    lv_obj_t* widget = nullptr;
    lv_obj_t* switchWidget = nullptr;
    lv_obj_t* sliderWidget = nullptr;
};

struct AllColumnConfigs {
    ColumnConfig generalPhysics;
    ColumnConfig pressure;
    ColumnConfig forces;
    ColumnConfig swapTuning;
    ColumnConfig swap2;
    ColumnConfig frag;
};

AllColumnConfigs createAllColumnConfigs();

size_t createControlsFromColumn(lv_obj_t* parent,
                                 const ColumnConfig& config,
                                 Control* controlsArray,
                                 size_t startIndex,
                                 std::unordered_map<lv_obj_t*, Control*>& widgetToControl,
                                 lv_event_cb_t toggleCallback,
                                 lv_event_cb_t sliderCallback,
                                 void* callbackUserData);

Control* findControl(lv_obj_t* widget,
                      std::unordered_map<lv_obj_t*, Control*>& widgetToControl);

void updateControlsFromSettings(Control* controlsArray,
                                 size_t controlCount,
                                 const PhysicsSettings& settings);

void syncSettingsToServer(Network::WebSocketService* wsService, const PhysicsSettings& settings);

PhysicsSettings fetchSettingsFromServer(Network::WebSocketService* wsService);

} // namespace PhysicsControlHelpers
} // namespace Ui
} // namespace DirtSim
