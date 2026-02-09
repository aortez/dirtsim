#include "TrainingUnsavedResultView.h"
#include "UiComponentManager.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/reflect.h"
#include "state-machine/Event.h"
#include "state-machine/EventSink.h"
#include "ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

TrainingUnsavedResultView::TrainingUnsavedResultView(
    UiComponentManager* uiManager,
    EventSink& eventSink,
    const Starfield::Snapshot* starfieldSnapshot)
    : uiManager_(uiManager), eventSink_(eventSink), starfieldSnapshot_(starfieldSnapshot)
{
    createUI();
}

TrainingUnsavedResultView::~TrainingUnsavedResultView()
{
    destroyUI();
}

void TrainingUnsavedResultView::createUI()
{
    DIRTSIM_ASSERT(uiManager_, "TrainingUnsavedResultView requires valid UiComponentManager");

    container_ = uiManager_->getWorldDisplayArea();
    DIRTSIM_ASSERT(container_, "Failed to get world display area");

    lv_obj_clean(container_);
    lv_obj_update_layout(container_);

    int displayWidth = lv_obj_get_width(container_);
    int displayHeight = lv_obj_get_height(container_);
    if (displayWidth <= 0 || displayHeight <= 0) {
        lv_disp_t* display = lv_disp_get_default();
        if (display) {
            displayWidth = lv_disp_get_hor_res(display);
            displayHeight = lv_disp_get_ver_res(display);
        }
    }

    starfield_ =
        std::make_unique<Starfield>(container_, displayWidth, displayHeight, starfieldSnapshot_);

    createUnsavedResultUI();
}

void TrainingUnsavedResultView::createUnsavedResultUI()
{
    contentRow_ = lv_obj_create(container_);
    lv_obj_set_size(contentRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(contentRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentRow_, 0, 0);
    lv_obj_clear_flag(contentRow_, LV_OBJ_FLAG_SCROLLABLE);

    LOG_INFO(Controls, "Training unsaved-result UI created");
}

void TrainingUnsavedResultView::destroyUI()
{
    hideTrainingResultModal();
    starfield_.reset();

    if (container_) {
        lv_obj_clean(container_);
    }

    container_ = nullptr;
    contentRow_ = nullptr;
}

void TrainingUnsavedResultView::updateAnimations()
{
    if (starfield_ && starfield_->isVisible()) {
        starfield_->update();
    }
}

Starfield::Snapshot TrainingUnsavedResultView::captureStarfieldSnapshot() const
{
    DIRTSIM_ASSERT(starfield_, "TrainingUnsavedResultView requires Starfield");
    return starfield_->capture();
}

void TrainingUnsavedResultView::showTrainingResultModal(
    const Api::TrainingResult::Summary& summary,
    const std::vector<Api::TrainingResult::Candidate>& candidates)
{
    hideTrainingResultModal();

    trainingResultSummary_ = summary;
    primaryCandidates_.clear();

    for (const auto& candidate : candidates) {
        if (!summary.primaryBrainKind.empty() && candidate.brainKind != summary.primaryBrainKind) {
            continue;
        }
        if (summary.primaryBrainVariant.has_value()
            && candidate.brainVariant != summary.primaryBrainVariant) {
            continue;
        }
        primaryCandidates_.push_back(candidate);
    }

    std::sort(
        primaryCandidates_.begin(), primaryCandidates_.end(), [](const auto& a, const auto& b) {
            return a.fitness > b.fitness;
        });

    lv_obj_t* overlayLayer = lv_layer_top();
    trainingResultOverlay_ = lv_obj_create(overlayLayer);
    lv_obj_set_size(trainingResultOverlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(trainingResultOverlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(trainingResultOverlay_, LV_OPA_60, 0);
    lv_obj_clear_flag(trainingResultOverlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(trainingResultOverlay_);

    lv_obj_t* modal = lv_obj_create(trainingResultOverlay_);
    lv_obj_set_size(modal, 380, 420);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_90, 0);
    lv_obj_set_style_radius(modal, 12, 0);
    lv_obj_set_style_pad_all(modal, 12, 0);
    lv_obj_set_style_pad_row(modal, 8, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(modal);
    lv_label_set_text(title, "Training Result");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    char buf[128];
    snprintf(buf, sizeof(buf), "Scenario: %s", Scenario::toString(summary.scenarioId).c_str());
    lv_obj_t* scenarioLabel = lv_label_create(modal);
    lv_label_set_text(scenarioLabel, buf);
    lv_obj_set_style_text_color(scenarioLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(scenarioLabel, &lv_font_montserrat_12, 0);

    const auto organismName = reflect::enum_name(summary.organismType);
    snprintf(
        buf,
        sizeof(buf),
        "Organism: %.*s",
        static_cast<int>(organismName.size()),
        organismName.data());
    lv_obj_t* organismLabel = lv_label_create(modal);
    lv_label_set_text(organismLabel, buf);
    lv_obj_set_style_text_color(organismLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(organismLabel, &lv_font_montserrat_12, 0);

    snprintf(buf, sizeof(buf), "Generations: %d", summary.completedGenerations);
    lv_obj_t* generationsLabel = lv_label_create(modal);
    lv_label_set_text(generationsLabel, buf);
    lv_obj_set_style_text_color(generationsLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(generationsLabel, &lv_font_montserrat_12, 0);

    snprintf(buf, sizeof(buf), "Population: %d", summary.populationSize);
    lv_obj_t* populationLabel = lv_label_create(modal);
    lv_label_set_text(populationLabel, buf);
    lv_obj_set_style_text_color(populationLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(populationLabel, &lv_font_montserrat_12, 0);

    snprintf(buf, sizeof(buf), "Best Fitness: %.2f", summary.bestFitness);
    lv_obj_t* bestLabel = lv_label_create(modal);
    lv_label_set_text(bestLabel, buf);
    lv_obj_set_style_text_color(bestLabel, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(bestLabel, &lv_font_montserrat_12, 0);

    snprintf(buf, sizeof(buf), "Avg Fitness: %.2f", summary.averageFitness);
    lv_obj_t* avgLabel = lv_label_create(modal);
    lv_label_set_text(avgLabel, buf);
    lv_obj_set_style_text_color(avgLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(avgLabel, &lv_font_montserrat_12, 0);

    snprintf(buf, sizeof(buf), "Total Time: %.1fs", summary.totalTrainingSeconds);
    lv_obj_t* timeLabel = lv_label_create(modal);
    lv_label_set_text(timeLabel, buf);
    lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_12, 0);

    std::string brainLabel =
        summary.primaryBrainKind.empty() ? "Unknown" : summary.primaryBrainKind;
    if (summary.primaryBrainVariant.has_value() && !summary.primaryBrainVariant->empty()) {
        brainLabel += " (" + summary.primaryBrainVariant.value() + ")";
    }
    snprintf(buf, sizeof(buf), "Brain A: %s", brainLabel.c_str());
    lv_obj_t* brainLabelObj = lv_label_create(modal);
    lv_label_set_text(brainLabelObj, buf);
    lv_obj_set_style_text_color(brainLabelObj, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(brainLabelObj, &lv_font_montserrat_12, 0);

    snprintf(buf, sizeof(buf), "Saveable Genomes: %zu", primaryCandidates_.size());
    trainingResultCountLabel_ = lv_label_create(modal);
    lv_label_set_text(trainingResultCountLabel_, buf);
    lv_obj_set_style_text_color(trainingResultCountLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(trainingResultCountLabel_, &lv_font_montserrat_12, 0);

    const int maxSaveCount = static_cast<int>(primaryCandidates_.size());
    trainingResultSaveStepper_ = LVGLBuilder::actionStepper(modal)
                                     .label("Save Top N")
                                     .range(0, maxSaveCount)
                                     .step(1)
                                     .value(maxSaveCount)
                                     .valueFormat("%.0f")
                                     .valueScale(1.0)
                                     .width(LV_PCT(95))
                                     .callback(onTrainingResultCountChanged, this)
                                     .buildOrLog();

    lv_obj_t* buttonRow = lv_obj_create(modal);
    lv_obj_set_size(buttonRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttonRow, 0, 0);
    lv_obj_set_style_pad_column(buttonRow, 10, 0);
    lv_obj_set_flex_flow(buttonRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        buttonRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttonRow, LV_OBJ_FLAG_SCROLLABLE);

    trainingResultSaveButton_ = LVGLBuilder::actionButton(buttonRow)
                                    .text("Save")
                                    .icon(LV_SYMBOL_OK)
                                    .mode(LVGLBuilder::ActionMode::Push)
                                    .size(80)
                                    .backgroundColor(0x00AA66)
                                    .callback(onTrainingResultSaveClicked, this)
                                    .buildOrLog();

    trainingResultSaveAndRestartButton_ = LVGLBuilder::actionButton(buttonRow)
                                              .text("Save+Run")
                                              .icon(LV_SYMBOL_PLAY)
                                              .mode(LVGLBuilder::ActionMode::Push)
                                              .size(80)
                                              .backgroundColor(0x0077CC)
                                              .callback(onTrainingResultSaveAndRestartClicked, this)
                                              .buildOrLog();

    LVGLBuilder::actionButton(buttonRow)
        .text("Discard")
        .icon(LV_SYMBOL_CLOSE)
        .mode(LVGLBuilder::ActionMode::Push)
        .size(80)
        .backgroundColor(0xCC0000)
        .callback(onTrainingResultDiscardClicked, this)
        .buildOrLog();

    updateTrainingResultSaveButton();
}

void TrainingUnsavedResultView::hideTrainingResultModal()
{
    if (trainingResultOverlay_) {
        lv_obj_del(trainingResultOverlay_);
        trainingResultOverlay_ = nullptr;
    }

    trainingResultCountLabel_ = nullptr;
    trainingResultSaveStepper_ = nullptr;
    trainingResultSaveButton_ = nullptr;
    trainingResultSaveAndRestartButton_ = nullptr;
    primaryCandidates_.clear();
    trainingResultSummary_ = Api::TrainingResult::Summary{};
}

bool TrainingUnsavedResultView::isTrainingResultModalVisible() const
{
    return trainingResultOverlay_ != nullptr;
}

void TrainingUnsavedResultView::updateTrainingResultSaveButton()
{
    if (!trainingResultSaveButton_ && !trainingResultSaveAndRestartButton_) {
        return;
    }
    int32_t value = 0;
    if (trainingResultSaveStepper_) {
        value = LVGLBuilder::ActionStepperBuilder::getValue(trainingResultSaveStepper_);
    }

    const bool enabled = (value > 0) && !primaryCandidates_.empty();
    auto updateButton = [&](lv_obj_t* button) {
        if (!button) {
            return;
        }
        if (enabled) {
            lv_obj_clear_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_opa(button, LV_OPA_COVER, 0);
        }
        else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
            lv_obj_set_style_opa(button, LV_OPA_50, 0);
        }
    };

    updateButton(trainingResultSaveButton_);
    updateButton(trainingResultSaveAndRestartButton_);
}

std::vector<GenomeId> TrainingUnsavedResultView::getTrainingResultSaveIds() const
{
    if (!trainingResultSaveStepper_) {
        return {};
    }

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(trainingResultSaveStepper_);
    return getTrainingResultSaveIdsForCount(value);
}

std::vector<GenomeId> TrainingUnsavedResultView::getTrainingResultSaveIdsForCount(int count) const
{
    std::vector<GenomeId> ids;
    const int clamped = std::max(0, count);
    const int limit = std::min(clamped, static_cast<int>(primaryCandidates_.size()));
    ids.reserve(limit);
    for (int i = 0; i < limit; ++i) {
        ids.push_back(primaryCandidates_[i].id);
    }
    return ids;
}

void TrainingUnsavedResultView::onTrainingResultSaveClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingUnsavedResultView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    TrainingResultSaveClickedEvent evt;
    evt.ids = self->getTrainingResultSaveIds();
    self->eventSink_.queueEvent(evt);
}

void TrainingUnsavedResultView::onTrainingResultSaveAndRestartClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingUnsavedResultView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    TrainingResultSaveClickedEvent evt;
    evt.ids = self->getTrainingResultSaveIds();
    evt.restart = true;
    self->eventSink_.queueEvent(evt);
}

void TrainingUnsavedResultView::onTrainingResultDiscardClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingUnsavedResultView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->eventSink_.queueEvent(TrainingResultDiscardClickedEvent{});
}

void TrainingUnsavedResultView::onTrainingResultCountChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingUnsavedResultView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->updateTrainingResultSaveButton();
}

} // namespace Ui
} // namespace DirtSim
