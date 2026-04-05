#include "SearchSettingsPanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "server/api/UserSettingsPatch.h"
#include "ui/UiServices.h"
#include "ui/UserSettingsManager.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {
namespace {

constexpr int kPanelControlWidth = LV_PCT(95);

uint32_t clampStepperValue(int32_t value, uint32_t minValue, uint32_t maxValue)
{
    return static_cast<uint32_t>(
        std::clamp<int32_t>(value, static_cast<int32_t>(minValue), static_cast<int32_t>(maxValue)));
}

SearchSettings clampSearchSettings(const SearchSettings& settings)
{
    return SearchSettings{
        .maxSearchedNodeCount = clampStepperValue(
            static_cast<int32_t>(settings.maxSearchedNodeCount),
            SearchSettings::MaxSearchedNodeCountMin,
            SearchSettings::MaxSearchedNodeCountMax),
        .stallFrameLimit = clampStepperValue(
            static_cast<int32_t>(settings.stallFrameLimit),
            SearchSettings::StallFrameLimitMin,
            SearchSettings::StallFrameLimitMax),
        .velocityPruningEnabled = settings.velocityPruningEnabled,
    };
}

} // namespace

SearchSettingsPanel::SearchSettingsPanel(lv_obj_t* container, UiServices& uiServices)
    : container_(container), uiServices_(uiServices)
{
    createControls();
    LOG_INFO(Controls, "SearchSettingsPanel created");
}

SearchSettingsPanel::~SearchSettingsPanel()
{
    LOG_INFO(Controls, "SearchSettingsPanel destroyed");
}

void SearchSettingsPanel::applySettings(const DirtSim::UserSettings& settings)
{
    settings_ = settings;
    updatingUi_ = true;

    if (maxSearchedNodeCountStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            maxSearchedNodeCountStepper_,
            static_cast<int32_t>(settings_.searchSettings.maxSearchedNodeCount));
    }
    if (stallFrameLimitStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            stallFrameLimitStepper_,
            static_cast<int32_t>(settings_.searchSettings.stallFrameLimit));
    }
    if (velocityPruningSwitch_) {
        if (settings_.searchSettings.velocityPruningEnabled) {
            lv_obj_add_state(velocityPruningSwitch_, LV_STATE_CHECKED);
        }
        else {
            lv_obj_clear_state(velocityPruningSwitch_, LV_STATE_CHECKED);
        }
    }

    updatingUi_ = false;
}

void SearchSettingsPanel::createControls()
{
    DIRTSIM_ASSERT(container_, "SearchSettingsPanel requires a container");

    lv_obj_t* header = lv_label_create(container_);
    DIRTSIM_ASSERT(header, "SearchSettingsPanel failed to create header");
    lv_label_set_text(header, "Search Settings");
    lv_obj_set_width(header, LV_PCT(95));
    lv_obj_set_style_text_align(header, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);

    maxSearchedNodeCountStepper_ =
        LVGLBuilder::actionStepper(container_)
            .label("Node Budget")
            .range(
                static_cast<int32_t>(SearchSettings::MaxSearchedNodeCountMin),
                static_cast<int32_t>(SearchSettings::MaxSearchedNodeCountMax))
            .step(1000)
            .value(static_cast<int32_t>(settings_.searchSettings.maxSearchedNodeCount))
            .width(kPanelControlWidth)
            .callback(onMaxSearchedNodeCountChanged, this)
            .buildOrLog();

    stallFrameLimitStepper_ =
        LVGLBuilder::actionStepper(container_)
            .label("Stall Limit (frames)")
            .range(
                static_cast<int32_t>(SearchSettings::StallFrameLimitMin),
                static_cast<int32_t>(SearchSettings::StallFrameLimitMax))
            .step(5)
            .value(static_cast<int32_t>(settings_.searchSettings.stallFrameLimit))
            .width(kPanelControlWidth)
            .callback(onStallFrameLimitChanged, this)
            .buildOrLog();

    velocityPruningSwitch_ = LVGLBuilder::labeledSwitch(container_)
                                 .label("Velocity Pruning")
                                 .initialState(settings_.searchSettings.velocityPruningEnabled)
                                 .width(kPanelControlWidth)
                                 .callback(onVelocityPruningChanged, this)
                                 .buildOrLog();
}

void SearchSettingsPanel::patchCurrentSettings()
{
    const Api::UserSettingsPatch::Command patchCmd{
        .searchSettings = settings_.searchSettings,
    };
    uiServices_.userSettingsManager().patchOrAssert(patchCmd);
    applySettings(uiServices_.userSettingsManager().get());
}

void SearchSettingsPanel::setSearchSettingsAndPersist(const SearchSettings& settings)
{
    settings_.searchSettings = clampSearchSettings(settings);
    patchCurrentSettings();
}

void SearchSettingsPanel::onMaxSearchedNodeCountChanged(lv_event_t* e)
{
    auto* self = static_cast<SearchSettingsPanel*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SearchSettingsPanel node-budget callback requires panel");
    if (self->updatingUi_) {
        return;
    }

    self->settings_.searchSettings.maxSearchedNodeCount = clampStepperValue(
        LVGLBuilder::ActionStepperBuilder::getValue(self->maxSearchedNodeCountStepper_),
        SearchSettings::MaxSearchedNodeCountMin,
        SearchSettings::MaxSearchedNodeCountMax);
    self->patchCurrentSettings();
}

void SearchSettingsPanel::onStallFrameLimitChanged(lv_event_t* e)
{
    auto* self = static_cast<SearchSettingsPanel*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SearchSettingsPanel stall-limit callback requires panel");
    if (self->updatingUi_) {
        return;
    }

    self->settings_.searchSettings.stallFrameLimit = clampStepperValue(
        LVGLBuilder::ActionStepperBuilder::getValue(self->stallFrameLimitStepper_),
        SearchSettings::StallFrameLimitMin,
        SearchSettings::StallFrameLimitMax);
    self->patchCurrentSettings();
}

void SearchSettingsPanel::onVelocityPruningChanged(lv_event_t* e)
{
    auto* self = static_cast<SearchSettingsPanel*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SearchSettingsPanel velocity-pruning callback requires panel");
    if (self->updatingUi_) {
        return;
    }

    self->settings_.searchSettings.velocityPruningEnabled =
        lv_obj_has_state(self->velocityPruningSwitch_, LV_STATE_CHECKED);
    self->patchCurrentSettings();
}

} // namespace Ui
} // namespace DirtSim
