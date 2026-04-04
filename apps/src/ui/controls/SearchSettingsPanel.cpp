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
        .beamWidth = clampStepperValue(
            static_cast<int32_t>(settings.beamWidth),
            SearchSettings::BeamWidthMin,
            SearchSettings::BeamWidthMax),
        .maxSegments = clampStepperValue(
            static_cast<int32_t>(settings.maxSegments),
            SearchSettings::MaxSegmentsMin,
            SearchSettings::MaxSegmentsMax),
        .segmentFrameBudget = clampStepperValue(
            static_cast<int32_t>(settings.segmentFrameBudget),
            SearchSettings::SegmentFrameBudgetMin,
            SearchSettings::SegmentFrameBudgetMax),
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

    if (beamWidthStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            beamWidthStepper_, static_cast<int32_t>(settings_.searchSettings.beamWidth));
    }
    if (maxSegmentsStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            maxSegmentsStepper_, static_cast<int32_t>(settings_.searchSettings.maxSegments));
    }
    if (segmentFrameBudgetStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            segmentFrameBudgetStepper_,
            static_cast<int32_t>(settings_.searchSettings.segmentFrameBudget));
    }

    updatingUi_ = false;
}

void SearchSettingsPanel::createControls()
{
    DIRTSIM_ASSERT(container_, "SearchSettingsPanel requires a container");

    lv_obj_t* header = lv_label_create(container_);
    DIRTSIM_ASSERT(header, "SearchSettingsPanel failed to create header");
    lv_label_set_text(header, "Search Settings");
    lv_obj_set_style_text_color(header, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);
    lv_obj_set_width(header, LV_PCT(95));
    lv_obj_set_style_text_align(header, LV_TEXT_ALIGN_LEFT, 0);

    beamWidthStepper_ = LVGLBuilder::actionStepper(container_)
                            .label("Search Depth")
                            .range(
                                static_cast<int32_t>(SearchSettings::BeamWidthMin),
                                static_cast<int32_t>(SearchSettings::BeamWidthMax))
                            .step(1)
                            .value(static_cast<int32_t>(settings_.searchSettings.beamWidth))
                            .width(kPanelControlWidth)
                            .callback(onBeamWidthChanged, this)
                            .buildOrLog();

    maxSegmentsStepper_ = LVGLBuilder::actionStepper(container_)
                              .label("Max Segments (0 = unlimited)")
                              .range(
                                  static_cast<int32_t>(SearchSettings::MaxSegmentsMin),
                                  static_cast<int32_t>(SearchSettings::MaxSegmentsMax))
                              .step(1)
                              .value(static_cast<int32_t>(settings_.searchSettings.maxSegments))
                              .width(kPanelControlWidth)
                              .callback(onMaxSegmentsChanged, this)
                              .buildOrLog();

    segmentFrameBudgetStepper_ =
        LVGLBuilder::actionStepper(container_)
            .label("Frames / Segment")
            .range(
                static_cast<int32_t>(SearchSettings::SegmentFrameBudgetMin),
                static_cast<int32_t>(SearchSettings::SegmentFrameBudgetMax))
            .step(1)
            .value(static_cast<int32_t>(settings_.searchSettings.segmentFrameBudget))
            .width(kPanelControlWidth)
            .callback(onSegmentFrameBudgetChanged, this)
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

void SearchSettingsPanel::onBeamWidthChanged(lv_event_t* e)
{
    auto* self = static_cast<SearchSettingsPanel*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SearchSettingsPanel beam-width callback requires panel");
    if (self->updatingUi_) {
        return;
    }

    self->settings_.searchSettings.beamWidth = clampStepperValue(
        LVGLBuilder::ActionStepperBuilder::getValue(self->beamWidthStepper_),
        SearchSettings::BeamWidthMin,
        SearchSettings::BeamWidthMax);
    self->patchCurrentSettings();
}

void SearchSettingsPanel::onMaxSegmentsChanged(lv_event_t* e)
{
    auto* self = static_cast<SearchSettingsPanel*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SearchSettingsPanel max-segments callback requires panel");
    if (self->updatingUi_) {
        return;
    }

    self->settings_.searchSettings.maxSegments = clampStepperValue(
        LVGLBuilder::ActionStepperBuilder::getValue(self->maxSegmentsStepper_),
        SearchSettings::MaxSegmentsMin,
        SearchSettings::MaxSegmentsMax);
    self->patchCurrentSettings();
}

void SearchSettingsPanel::onSegmentFrameBudgetChanged(lv_event_t* e)
{
    auto* self = static_cast<SearchSettingsPanel*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(self, "SearchSettingsPanel frame-budget callback requires panel");
    if (self->updatingUi_) {
        return;
    }

    self->settings_.searchSettings.segmentFrameBudget = clampStepperValue(
        LVGLBuilder::ActionStepperBuilder::getValue(self->segmentFrameBudgetStepper_),
        SearchSettings::SegmentFrameBudgetMin,
        SearchSettings::SegmentFrameBudgetMax);
    self->patchCurrentSettings();
}

} // namespace Ui
} // namespace DirtSim
