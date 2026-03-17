#include "NesSettingsPanel.h"

#include "core/LoggingChannels.h"
#include "server/api/UserSettingsPatch.h"
#include "ui/UserSettingsManager.h"
#include "ui/ui_builders/LVGLBuilder.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace DirtSim::Ui {

namespace {

constexpr double kFrameDelayTickMs = 0.1;
constexpr double kNtscNesFramePeriodMs = 1000.0 / 60.0988;
constexpr int kMaxFrameDelayTicks =
    static_cast<int>((kNtscNesFramePeriodMs - 0.001) / kFrameDelayTickMs);

int frameDelayMsToTicks(double frameDelayMs)
{
    return std::clamp(
        static_cast<int>(std::lround(frameDelayMs / kFrameDelayTickMs)), 0, kMaxFrameDelayTicks);
}

double frameDelayTicksToMs(int frameDelayTicks)
{
    return std::clamp(frameDelayTicks, 0, kMaxFrameDelayTicks) * kFrameDelayTickMs;
}

void setActionButtonText(lv_obj_t* buttonContainer, const std::string& text)
{
    if (!buttonContainer) {
        return;
    }

    lv_obj_t* button = lv_obj_get_child(buttonContainer, 0);
    if (!button) {
        return;
    }

    const uint32_t childCount = lv_obj_get_child_cnt(button);
    if (childCount == 0) {
        return;
    }

    const uint32_t textIndex = childCount > 1 ? 1 : 0;
    lv_obj_t* label = lv_obj_get_child(button, textIndex);
    if (!label) {
        return;
    }

    lv_label_set_text(label, text.c_str());
}

void setControlEnabledRecursive(lv_obj_t* control, bool enabled)
{
    if (!control) {
        return;
    }

    if (enabled) {
        lv_obj_clear_state(control, LV_STATE_DISABLED);
    }
    else {
        lv_obj_add_state(control, LV_STATE_DISABLED);
    }

    const uint32_t childCount = lv_obj_get_child_cnt(control);
    for (uint32_t i = 0; i < childCount; i++) {
        setControlEnabledRecursive(lv_obj_get_child(control, i), enabled);
    }
}

void setControlEnabled(lv_obj_t* control, bool enabled)
{
    if (!control) {
        return;
    }

    setControlEnabledRecursive(control, enabled);
    lv_obj_set_style_opa(control, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
}

} // namespace

NesSettingsPanel::NesSettingsPanel(lv_obj_t* container, UserSettingsManager& userSettingsManager)
    : container_(container),
      userSettingsManager_(userSettingsManager),
      settings_(userSettingsManager.get())
{
    frameDelayToggle_ = LVGLBuilder::actionButton(container_)
                            .text("Frame Delay")
                            .mode(LVGLBuilder::ActionMode::Toggle)
                            .width(LV_PCT(95))
                            .height(LVGLBuilder::Style::ACTION_SIZE)
                            .layoutRow()
                            .alignLeft()
                            .glowColor(0x3399FF)
                            .callback(onFrameDelayToggleClicked, this)
                            .buildOrLog();

    frameDelayStepper_ = LVGLBuilder::actionStepper(container_)
                             .label("Delay (ms)")
                             .range(0, kMaxFrameDelayTicks)
                             .step(1)
                             .value(frameDelayMsToTicks(settings_.nesSessionSettings.frameDelayMs))
                             .valueFormat("%.1f")
                             .valueScale(kFrameDelayTickMs)
                             .width(LV_PCT(95))
                             .callback(onFrameDelayValueChanged, this)
                             .buildOrLog();

    updateFromSettings(userSettingsManager_.get());

    LOG_INFO(Controls, "NesSettingsPanel created");
}

NesSettingsPanel::~NesSettingsPanel()
{
    LOG_INFO(Controls, "NesSettingsPanel destroyed");
}

void NesSettingsPanel::updateFromSettings(const DirtSim::UserSettings& settings)
{
    updatingUi_ = true;
    settings_ = settings;

    updateFrameDelayControl();

    updatingUi_ = false;
}

void NesSettingsPanel::syncSettings()
{
    const Api::UserSettingsPatch::Command patchCmd{
        .nesSessionSettings = settings_.nesSessionSettings,
    };
    userSettingsManager_.patchOrAssert(patchCmd, 500);
    updateFromSettings(userSettingsManager_.get());
}

void NesSettingsPanel::updateFrameDelayControl()
{
    const bool enabled = settings_.nesSessionSettings.frameDelayEnabled;
    updateFrameDelayToggleText();

    LVGLBuilder::ActionButtonBuilder::setChecked(frameDelayToggle_, enabled);
    LVGLBuilder::ActionStepperBuilder::setValue(
        frameDelayStepper_, frameDelayMsToTicks(settings_.nesSessionSettings.frameDelayMs));
    setControlEnabled(frameDelayStepper_, enabled);
}

void NesSettingsPanel::updateFrameDelayToggleText()
{
    const std::string text = std::string("Frame Delay: ")
        + (settings_.nesSessionSettings.frameDelayEnabled ? "Enabled" : "Disabled");
    setActionButtonText(frameDelayToggle_, text);
}

void NesSettingsPanel::onFrameDelayToggleClicked(lv_event_t* e)
{
    auto* self = static_cast<NesSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || self->updatingUi_) {
        return;
    }

    self->settings_.nesSessionSettings.frameDelayEnabled =
        LVGLBuilder::ActionButtonBuilder::isChecked(self->frameDelayToggle_);
    self->syncSettings();
}

void NesSettingsPanel::onFrameDelayValueChanged(lv_event_t* e)
{
    auto* self = static_cast<NesSettingsPanel*>(lv_event_get_user_data(e));
    if (!self || self->updatingUi_) {
        return;
    }

    self->settings_.nesSessionSettings.frameDelayMs =
        frameDelayTicksToMs(LVGLBuilder::ActionStepperBuilder::getValue(self->frameDelayStepper_));
    self->syncSettings();
}

} // namespace DirtSim::Ui
