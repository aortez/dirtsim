#include "TrainingView.h"
#include "UiComponentManager.h"
#include "controls/EvolutionConfigPanel.h"
#include "controls/EvolutionControls.h"
#include "controls/ExpandablePanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/WorldData.h"
#include "rendering/CellRenderer.h"
#include "rendering/RenderMode.h"
#include "server/api/EvolutionProgress.h"
#include "state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {

TrainingView::TrainingView(UiComponentManager* uiManager, EventSink& eventSink)
    : uiManager_(uiManager), eventSink_(eventSink)
{
    renderer_ = std::make_unique<CellRenderer>();
    createUI();
}

TrainingView::~TrainingView()
{
    destroyUI();
}

void TrainingView::createUI()
{
    DIRTSIM_ASSERT(uiManager_, "TrainingView requires valid UiComponentManager");

    container_ = uiManager_->getWorldDisplayArea();
    DIRTSIM_ASSERT(container_, "Failed to get world display area");

    lv_obj_clean(container_);

    // Create horizontal layout container for world + stats.
    lv_obj_t* mainLayout = lv_obj_create(container_);
    lv_obj_set_size(mainLayout, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(mainLayout, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mainLayout, 0, 0);
    lv_obj_set_style_pad_all(mainLayout, 10, 0);
    lv_obj_set_flex_flow(mainLayout, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        mainLayout, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(mainLayout, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: World view container.
    worldContainer_ = lv_obj_create(mainLayout);
    lv_obj_set_size(worldContainer_, 300, 300);
    lv_obj_set_style_bg_color(worldContainer_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(worldContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(worldContainer_, 8, 0);
    lv_obj_set_style_border_width(worldContainer_, 2, 0);
    lv_obj_set_style_border_color(worldContainer_, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(worldContainer_, 5, 0);
    lv_obj_clear_flag(worldContainer_, LV_OBJ_FLAG_SCROLLABLE);

    // Initialize the renderer with the world container.
    renderer_->initialize(worldContainer_, 9, 9);

    // Right side: Stats container.
    lv_obj_t* statsPanel = lv_obj_create(mainLayout);
    lv_obj_set_size(statsPanel, 300, 300);
    lv_obj_set_style_bg_color(statsPanel, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(statsPanel, LV_OPA_90, 0);
    lv_obj_set_style_radius(statsPanel, 8, 0);
    lv_obj_set_style_border_width(statsPanel, 2, 0);
    lv_obj_set_style_border_color(statsPanel, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(statsPanel, 15, 0);
    lv_obj_set_flex_flow(statsPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statsPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Title: "EVOLUTION".
    lv_obj_t* title = lv_label_create(statsPanel);
    lv_label_set_text(title, "EVOLUTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_pad_bottom(title, 5, 0);

    // Status label.
    statusLabel_ = lv_label_create(statsPanel);
    lv_label_set_text(statusLabel_, "Ready");
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x888888), 0);
    lv_obj_set_style_pad_bottom(statusLabel_, 8, 0);

    // Time displays container (left-aligned).
    lv_obj_t* timeContainer = lv_obj_create(statsPanel);
    lv_obj_set_size(timeContainer, 250, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(timeContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timeContainer, 0, 0);
    lv_obj_set_style_pad_all(timeContainer, 0, 0);
    lv_obj_set_flex_flow(timeContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        timeContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(timeContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(timeContainer, 10, 0);

    // Total training time.
    totalTimeLabel_ = lv_label_create(timeContainer);
    lv_label_set_text(totalTimeLabel_, "Training: 0.0s");
    lv_obj_set_style_text_color(totalTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(totalTimeLabel_, &lv_font_montserrat_12, 0);

    // Current sim time.
    simTimeLabel_ = lv_label_create(timeContainer);
    lv_label_set_text(simTimeLabel_, "Sim Time: 0.0s");
    lv_obj_set_style_text_color(simTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(simTimeLabel_, &lv_font_montserrat_12, 0);

    // Cumulative sim time.
    cumulativeTimeLabel_ = lv_label_create(timeContainer);
    lv_label_set_text(cumulativeTimeLabel_, "Cumulative: 0.0s");
    lv_obj_set_style_text_color(cumulativeTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(cumulativeTimeLabel_, &lv_font_montserrat_12, 0);

    // Speedup factor.
    speedupLabel_ = lv_label_create(timeContainer);
    lv_label_set_text(speedupLabel_, "Speedup: 0.0x");
    lv_obj_set_style_text_color(speedupLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(speedupLabel_, &lv_font_montserrat_12, 0);

    // ETA.
    etaLabel_ = lv_label_create(timeContainer);
    lv_label_set_text(etaLabel_, "ETA: --");
    lv_obj_set_style_text_color(etaLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(etaLabel_, &lv_font_montserrat_12, 0);

    // Generation progress section (label on left, bar on right).
    lv_obj_t* genRow = lv_obj_create(statsPanel);
    lv_obj_set_size(genRow, 250, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(genRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(genRow, 0, 0);
    lv_obj_set_style_pad_all(genRow, 0, 0);
    lv_obj_set_flex_flow(genRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(genRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(genRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(genRow, 8, 0);

    genLabel_ = lv_label_create(genRow);
    lv_label_set_text(genLabel_, "Gen: 0/0");
    lv_obj_set_style_text_color(genLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(genLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_width(genLabel_, 70);

    generationBar_ = lv_bar_create(genRow);
    lv_obj_set_size(generationBar_, 170, 14);
    lv_bar_set_range(generationBar_, 0, 100);
    lv_bar_set_value(generationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x00AA66), LV_PART_INDICATOR);
    lv_obj_set_style_radius(generationBar_, 4, 0);
    lv_obj_set_style_radius(generationBar_, 4, LV_PART_INDICATOR);

    // Evaluation progress section (label on left, bar on right).
    lv_obj_t* evalRow = lv_obj_create(statsPanel);
    lv_obj_set_size(evalRow, 250, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(evalRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(evalRow, 0, 0);
    lv_obj_set_style_pad_all(evalRow, 0, 0);
    lv_obj_set_flex_flow(evalRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(evalRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(evalRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(evalRow, 12, 0);

    evalLabel_ = lv_label_create(evalRow);
    lv_label_set_text(evalLabel_, "Eval: 0/0");
    lv_obj_set_style_text_color(evalLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(evalLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_width(evalLabel_, 70);

    evaluationBar_ = lv_bar_create(evalRow);
    lv_obj_set_size(evaluationBar_, 170, 14);
    lv_bar_set_range(evaluationBar_, 0, 100);
    lv_bar_set_value(evaluationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x6688CC), LV_PART_INDICATOR);
    lv_obj_set_style_radius(evaluationBar_, 4, 0);
    lv_obj_set_style_radius(evaluationBar_, 4, LV_PART_INDICATOR);

    // Fitness statistics section.
    lv_obj_t* fitnessContainer = lv_obj_create(statsPanel);
    lv_obj_set_size(fitnessContainer, 250, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fitnessContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fitnessContainer, 0, 0);
    lv_obj_set_style_pad_all(fitnessContainer, 0, 0);
    lv_obj_set_flex_flow(fitnessContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        fitnessContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(fitnessContainer, LV_OBJ_FLAG_SCROLLABLE);

    bestThisGenLabel_ = lv_label_create(fitnessContainer);
    lv_label_set_text(bestThisGenLabel_, "Best (this gen):  --");
    lv_obj_set_style_text_color(bestThisGenLabel_, lv_color_hex(0xAAAACC), 0);

    bestAllTimeLabel_ = lv_label_create(fitnessContainer);
    lv_label_set_text(bestAllTimeLabel_, "Best (all time):  --");
    lv_obj_set_style_text_color(bestAllTimeLabel_, lv_color_hex(0xFFDD66), 0);

    averageLabel_ = lv_label_create(fitnessContainer);
    lv_label_set_text(averageLabel_, "Average:          --");
    lv_obj_set_style_text_color(averageLabel_, lv_color_hex(0xAAAACC), 0);

    LOG_INFO(Controls, "Training UI created with live world view");
}

void TrainingView::destroyUI()
{
    if (renderer_) {
        renderer_->cleanup();
    }

    if (container_) {
        lv_obj_clean(container_);
    }

    averageLabel_ = nullptr;
    bestAllTimeLabel_ = nullptr;
    bestThisGenLabel_ = nullptr;
    container_ = nullptr;
    cumulativeTimeLabel_ = nullptr;
    etaLabel_ = nullptr;
    evalLabel_ = nullptr;
    evaluationBar_ = nullptr;
    genLabel_ = nullptr;
    generationBar_ = nullptr;
    simTimeLabel_ = nullptr;
    speedupLabel_ = nullptr;
    statusLabel_ = nullptr;
    totalTimeLabel_ = nullptr;
    worldContainer_ = nullptr;
}

void TrainingView::renderWorld(const WorldData& worldData)
{
    if (!renderer_ || !worldContainer_) {
        return;
    }

    renderer_->renderWorldData(worldData, worldContainer_, false, RenderMode::SHARP);
}

void TrainingView::updateProgress(const Api::EvolutionProgress& progress)
{
    if (!genLabel_ || !evalLabel_ || !generationBar_ || !evaluationBar_) return;

    // Detect training completion.
    const bool isComplete =
        (progress.maxGenerations > 0 && progress.generation >= progress.maxGenerations
         && progress.currentEval >= progress.populationSize);

    if (isComplete) {
        setEvolutionCompleted(progress.bestGenomeId);
    }

    char buf[64];

    // Update time displays.
    if (totalTimeLabel_) {
        snprintf(buf, sizeof(buf), "Training: %.1fs", progress.totalTrainingSeconds);
        lv_label_set_text(totalTimeLabel_, buf);
    }

    if (simTimeLabel_) {
        snprintf(buf, sizeof(buf), "Sim Time: %.1fs", progress.currentSimTime);
        lv_label_set_text(simTimeLabel_, buf);
    }

    if (cumulativeTimeLabel_) {
        snprintf(buf, sizeof(buf), "Cumulative: %.1fs", progress.cumulativeSimTime);
        lv_label_set_text(cumulativeTimeLabel_, buf);
    }

    if (speedupLabel_) {
        snprintf(buf, sizeof(buf), "Speedup: %.1fx", progress.speedupFactor);
        lv_label_set_text(speedupLabel_, buf);
    }

    if (etaLabel_) {
        if (progress.etaSeconds <= 0.0) {
            lv_label_set_text(etaLabel_, "ETA: --");
        }
        else if (progress.etaSeconds < 60.0) {
            snprintf(buf, sizeof(buf), "ETA: %.0fs", progress.etaSeconds);
            lv_label_set_text(etaLabel_, buf);
        }
        else if (progress.etaSeconds < 3600.0) {
            int mins = static_cast<int>(progress.etaSeconds) / 60;
            int secs = static_cast<int>(progress.etaSeconds) % 60;
            snprintf(buf, sizeof(buf), "ETA: %dm %ds", mins, secs);
            lv_label_set_text(etaLabel_, buf);
        }
        else {
            int hours = static_cast<int>(progress.etaSeconds) / 3600;
            int mins = (static_cast<int>(progress.etaSeconds) % 3600) / 60;
            snprintf(buf, sizeof(buf), "ETA: %dh %dm", hours, mins);
            lv_label_set_text(etaLabel_, buf);
        }
    }

    // Update generation progress.
    snprintf(buf, sizeof(buf), "Gen: %d/%d", progress.generation, progress.maxGenerations);
    lv_label_set_text(genLabel_, buf);

    const int genPercent =
        progress.maxGenerations > 0 ? (progress.generation * 100) / progress.maxGenerations : 0;
    lv_bar_set_value(generationBar_, genPercent, LV_ANIM_ON);

    // Update evaluation progress.
    snprintf(buf, sizeof(buf), "Eval: %d/%d", progress.currentEval, progress.populationSize);
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

void TrainingView::onIconSelected(IconId selectedId, IconId previousId)
{
    LOG_INFO(
        Controls,
        "TrainingView: Icon selection {} -> {}",
        static_cast<int>(previousId),
        static_cast<int>(selectedId));

    // Show panel content for selected icon.
    if (selectedId != IconId::COUNT && selectedId != IconId::TREE) {
        showPanelContent(selectedId);
    }
    else if (selectedId == IconId::COUNT) {
        // No icon selected - clear panel.
        clearPanelContent();
    }
}

void TrainingView::showPanelContent(IconId panelId)
{
    if (panelId == activePanel_) return; // Already showing this panel.

    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "TrainingView: No expandable panel available");
        return;
    }

    // Clear existing content.
    clearPanelContent();

    // Get content area.
    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "TrainingView: No panel content area available");
        return;
    }

    // Create content for the selected panel.
    switch (panelId) {
        case IconId::CORE:
            createCorePanel(container);
            break;

        case IconId::EVOLUTION:
            createEvolutionConfigPanel(container);
            break;

        case IconId::NETWORK:
        case IconId::PHYSICS:
        case IconId::PLAY:
        case IconId::SCENARIO:
        case IconId::TREE:
        case IconId::COUNT:
            LOG_WARN(Controls, "TrainingView: Unhandled panel id {}", static_cast<int>(panelId));
            break;
    }

    // Show panel to display content.
    panel->show();

    activePanel_ = panelId;
}

void TrainingView::clearPanelContent()
{
    evolutionConfigPanel_.reset();
    evolutionControls_.reset();

    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (panel) {
        panel->clearContent();
    }

    activePanel_ = IconId::COUNT;
}

void TrainingView::createCorePanel(lv_obj_t* container)
{
    evolutionControls_ = std::make_unique<EvolutionControls>(
        container, eventSink_, evolutionStarted_, evolutionConfig_, mutationConfig_);
    LOG_INFO(Controls, "TrainingView: Created Training Home panel");
}

void TrainingView::createEvolutionConfigPanel(lv_obj_t* container)
{
    evolutionConfigPanel_ = std::make_unique<EvolutionConfigPanel>(
        container, eventSink_, evolutionStarted_, evolutionConfig_, mutationConfig_);
    LOG_INFO(Controls, "TrainingView: Created Evolution config panel");
}

void TrainingView::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;

    if (statusLabel_) {
        if (started) {
            lv_label_set_text(statusLabel_, "Training...");
            lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x00CC66), 0);
        }
        else {
            lv_label_set_text(statusLabel_, "Ready");
            lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x888888), 0);
        }
    }

    // Update panels if open.
    if (evolutionControls_) {
        evolutionControls_->setEvolutionStarted(started);
    }
    if (evolutionConfigPanel_) {
        evolutionConfigPanel_->setEvolutionStarted(started);
    }
}

void TrainingView::setEvolutionCompleted(GenomeId bestGenomeId)
{
    evolutionStarted_ = false;

    // Show "Complete!" on main status.
    if (statusLabel_) {
        lv_label_set_text(statusLabel_, "Complete!");
        lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xFFDD66), 0);
    }

    // Update panels to show completion and re-enable controls.
    if (evolutionControls_) {
        evolutionControls_->setEvolutionCompleted(bestGenomeId);
    }
    if (evolutionConfigPanel_) {
        evolutionConfigPanel_->setEvolutionCompleted();
    }
}

} // namespace Ui
} // namespace DirtSim
