#pragma once

#include "ui/UserSettings.h"
#include <lvgl/lvgl.h>

namespace DirtSim::Ui {

class UserSettingsManager;

class NesSettingsPanel {
public:
    NesSettingsPanel(lv_obj_t* container, UserSettingsManager& userSettingsManager);
    ~NesSettingsPanel();

    void updateFromSettings(const DirtSim::UserSettings& settings);

private:
    void syncSettings();
    void updateFrameDelayControl();
    void updateFrameDelayToggleText();

    static void onFrameDelayToggleClicked(lv_event_t* e);
    static void onFrameDelayValueChanged(lv_event_t* e);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* frameDelayStepper_ = nullptr;
    lv_obj_t* frameDelayToggle_ = nullptr;
    UserSettingsManager& userSettingsManager_;
    DirtSim::UserSettings settings_{};
    bool updatingUi_ = false;
};

} // namespace DirtSim::Ui
