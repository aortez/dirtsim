#include "TrainingView.h"
#include "UiComponentManager.h"
#include "core/LoggingChannels.h"
#include "server/api/EvolutionProgress.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

TrainingView::TrainingView(UiComponentManager* uiManager) : uiManager_(uiManager)
{
    createUI();
}

TrainingView::~TrainingView()
{
    destroyUI();
}

void TrainingView::createUI()
{
    if (!uiManager_) {
        LOG_ERROR(Controls, "No UiComponentManager available");
        return;
    }

    container_ = uiManager_->getMainMenuContainer();
    if (!container_) {
        LOG_ERROR(Controls, "Failed to get main menu container");
        return;
    }

    // Clean any existing widgets from previous state.
    lv_obj_clean(container_);

    // Create centered content container.
    lv_obj_t* content = lv_obj_create(container_);
    lv_obj_set_size(content, 400, 350);
    lv_obj_center(content);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_90, 0);
    lv_obj_set_style_radius(content, 12, 0);
    lv_obj_set_style_border_width(content, 2, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title: "EVOLUTION".
    lv_obj_t* title = lv_label_create(content);
    lv_label_set_text(title, "EVOLUTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_pad_bottom(title, 20, 0);

    // Generation progress section.
    genLabel_ = lv_label_create(content);
    lv_label_set_text(genLabel_, "Generation: 0 / 0");
    lv_obj_set_style_text_color(genLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_bottom(genLabel_, 5, 0);

    generationBar_ = lv_bar_create(content);
    lv_obj_set_size(generationBar_, 340, 20);
    lv_bar_set_range(generationBar_, 0, 100);
    lv_bar_set_value(generationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x00AA66), LV_PART_INDICATOR);
    lv_obj_set_style_radius(generationBar_, 5, 0);
    lv_obj_set_style_radius(generationBar_, 5, LV_PART_INDICATOR);
    lv_obj_set_style_pad_bottom(generationBar_, 15, 0);

    // Evaluation progress section.
    evalLabel_ = lv_label_create(content);
    lv_label_set_text(evalLabel_, "Evaluating: 0 / 0");
    lv_obj_set_style_text_color(evalLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_bottom(evalLabel_, 5, 0);

    evaluationBar_ = lv_bar_create(content);
    lv_obj_set_size(evaluationBar_, 340, 20);
    lv_bar_set_range(evaluationBar_, 0, 100);
    lv_bar_set_value(evaluationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x6688CC), LV_PART_INDICATOR);
    lv_obj_set_style_radius(evaluationBar_, 5, 0);
    lv_obj_set_style_radius(evaluationBar_, 5, LV_PART_INDICATOR);
    lv_obj_set_style_pad_bottom(evaluationBar_, 20, 0);

    // Fitness statistics section.
    lv_obj_t* statsContainer = lv_obj_create(content);
    lv_obj_set_size(statsContainer, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(statsContainer, 0, 0);
    lv_obj_set_style_pad_all(statsContainer, 0, 0);
    lv_obj_set_flex_flow(statsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(statsContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(statsContainer, 20, 0);

    bestThisGenLabel_ = lv_label_create(statsContainer);
    lv_label_set_text(bestThisGenLabel_, "Best (this gen):  --");
    lv_obj_set_style_text_color(bestThisGenLabel_, lv_color_hex(0xAAAACC), 0);

    bestAllTimeLabel_ = lv_label_create(statsContainer);
    lv_label_set_text(bestAllTimeLabel_, "Best (all time):  --");
    lv_obj_set_style_text_color(bestAllTimeLabel_, lv_color_hex(0xFFDD66), 0);

    averageLabel_ = lv_label_create(statsContainer);
    lv_label_set_text(averageLabel_, "Average:          --");
    lv_obj_set_style_text_color(averageLabel_, lv_color_hex(0xAAAACC), 0);

    // Stop button using ActionButton style.
    stopButton_ = LVGLBuilder::ActionButtonBuilder(content)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .width(120)
                      .height(50)
                      .backgroundColor(0x882222)
                      .buildOrLog();

    LOG_INFO(Controls, "Training UI created");
}

void TrainingView::destroyUI()
{
    if (container_) {
        lv_obj_clean(container_);
    }

    averageLabel_ = nullptr;
    bestAllTimeLabel_ = nullptr;
    bestThisGenLabel_ = nullptr;
    container_ = nullptr;
    evalLabel_ = nullptr;
    evaluationBar_ = nullptr;
    genLabel_ = nullptr;
    generationBar_ = nullptr;
    stopButton_ = nullptr;
}

void TrainingView::updateProgress(const Api::EvolutionProgress& progress)
{
    if (!genLabel_ || !evalLabel_ || !generationBar_ || !evaluationBar_) return;

    char buf[64];

    // Update generation progress.
    snprintf(buf, sizeof(buf), "Generation: %d / %d", progress.generation, progress.maxGenerations);
    lv_label_set_text(genLabel_, buf);

    const int genPercent =
        progress.maxGenerations > 0 ? (progress.generation * 100) / progress.maxGenerations : 0;
    lv_bar_set_value(generationBar_, genPercent, LV_ANIM_ON);

    // Update evaluation progress.
    snprintf(
        buf, sizeof(buf), "Evaluating: %d / %d", progress.currentEval, progress.populationSize);
    lv_label_set_text(evalLabel_, buf);

    const int evalPercent =
        progress.populationSize > 0 ? (progress.currentEval * 100) / progress.populationSize : 0;
    lv_bar_set_value(evaluationBar_, evalPercent, LV_ANIM_ON);

    // Update fitness labels.
    if (bestThisGenLabel_) {
        snprintf(buf, sizeof(buf), "Best (this gen):  %.2f", progress.bestFitnessThisGen);
        lv_label_set_text(bestThisGenLabel_, buf);
    }

    if (bestAllTimeLabel_) {
        snprintf(buf, sizeof(buf), "Best (all time):  %.2f", progress.bestFitnessAllTime);
        lv_label_set_text(bestAllTimeLabel_, buf);
    }

    if (averageLabel_) {
        snprintf(buf, sizeof(buf), "Average:          %.2f", progress.averageFitness);
        lv_label_set_text(averageLabel_, buf);
    }
}

void TrainingView::setStopCallback(void (*callback)(lv_event_t*), void* userData)
{
    if (stopButton_) {
        lv_obj_add_event_cb(stopButton_, callback, LV_EVENT_CLICKED, userData);
    }
}

} // namespace Ui
} // namespace DirtSim
