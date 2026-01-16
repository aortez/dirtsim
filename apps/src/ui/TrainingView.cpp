#include "TrainingView.h"
#include "UiComponentManager.h"
#include "controls/EvolutionConfigPanel.h"
#include "controls/EvolutionControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/IconRail.h"
#include "controls/TrainingPopulationPanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/WorldData.h"
#include "rendering/CellRenderer.h"
#include "rendering/RenderMode.h"
#include "server/api/EvolutionProgress.h"
#include "state-machine/EventSink.h"
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {

namespace {
constexpr double FITNESS_IMPROVEMENT_EPSILON = 0.001;
}

TrainingView::TrainingView(UiComponentManager* uiManager, EventSink& eventSink)
    : uiManager_(uiManager), eventSink_(eventSink)
{
    renderer_ = std::make_unique<CellRenderer>();
    bestRenderer_ = std::make_unique<CellRenderer>();
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

    // Main layout: column with stats on top, world views on bottom.
    lv_obj_t* mainLayout = lv_obj_create(container_);
    lv_obj_set_size(mainLayout, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(mainLayout, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mainLayout, 0, 0);
    lv_obj_set_style_pad_all(mainLayout, 5, 0);
    lv_obj_set_style_pad_gap(mainLayout, 5, 0);
    lv_obj_set_flex_flow(mainLayout, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        mainLayout, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(mainLayout, LV_OBJ_FLAG_SCROLLABLE);

    // ========== TOP: Stats panel (condensed) ==========
    lv_obj_t* statsPanel = lv_obj_create(mainLayout);
    lv_obj_set_size(statsPanel, 580, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(statsPanel, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(statsPanel, LV_OPA_90, 0);
    lv_obj_set_style_radius(statsPanel, 8, 0);
    lv_obj_set_style_border_width(statsPanel, 1, 0);
    lv_obj_set_style_border_color(statsPanel, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(statsPanel, 10, 0);
    lv_obj_set_style_pad_gap(statsPanel, 4, 0);
    lv_obj_set_flex_flow(statsPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statsPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_SCROLLABLE);

    // Title row: "EVOLUTION" + status.
    lv_obj_t* titleRow = lv_obj_create(statsPanel);
    lv_obj_set_size(titleRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(titleRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(titleRow, 0, 0);
    lv_obj_set_style_pad_all(titleRow, 0, 0);
    lv_obj_set_flex_flow(titleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        titleRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(titleRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(titleRow);
    lv_label_set_text(title, "EVOLUTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_pad_right(title, 15, 0);

    statusLabel_ = lv_label_create(titleRow);
    lv_label_set_text(statusLabel_, "Ready");
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x888888), 0);

    // Time stats row (compact horizontal).
    lv_obj_t* timeRow = lv_obj_create(statsPanel);
    lv_obj_set_size(timeRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(timeRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timeRow, 0, 0);
    lv_obj_set_style_pad_all(timeRow, 0, 0);
    lv_obj_set_style_pad_gap(timeRow, 12, 0);
    lv_obj_set_flex_flow(timeRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        timeRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(timeRow, LV_OBJ_FLAG_SCROLLABLE);

    totalTimeLabel_ = lv_label_create(timeRow);
    lv_label_set_text(totalTimeLabel_, "Time: 0.0s");
    lv_obj_set_style_text_color(totalTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(totalTimeLabel_, &lv_font_montserrat_12, 0);

    simTimeLabel_ = lv_label_create(timeRow);
    lv_label_set_text(simTimeLabel_, "Sim: 0.0s");
    lv_obj_set_style_text_color(simTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(simTimeLabel_, &lv_font_montserrat_12, 0);

    speedupLabel_ = lv_label_create(timeRow);
    lv_label_set_text(speedupLabel_, "Speed: 0.0x");
    lv_obj_set_style_text_color(speedupLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(speedupLabel_, &lv_font_montserrat_12, 0);

    etaLabel_ = lv_label_create(timeRow);
    lv_label_set_text(etaLabel_, "ETA: --");
    lv_obj_set_style_text_color(etaLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(etaLabel_, &lv_font_montserrat_12, 0);

    // Progress bars row.
    lv_obj_t* progressRow = lv_obj_create(statsPanel);
    lv_obj_set_size(progressRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(progressRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(progressRow, 0, 0);
    lv_obj_set_style_pad_all(progressRow, 0, 0);
    lv_obj_set_style_pad_gap(progressRow, 20, 0);
    lv_obj_set_flex_flow(progressRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        progressRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(progressRow, LV_OBJ_FLAG_SCROLLABLE);

    // Generation progress.
    genLabel_ = lv_label_create(progressRow);
    lv_label_set_text(genLabel_, "Gen: 0/0");
    lv_obj_set_style_text_color(genLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(genLabel_, &lv_font_montserrat_12, 0);

    generationBar_ = lv_bar_create(progressRow);
    lv_obj_set_size(generationBar_, 120, 12);
    lv_bar_set_range(generationBar_, 0, 100);
    lv_bar_set_value(generationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x00AA66), LV_PART_INDICATOR);
    lv_obj_set_style_radius(generationBar_, 4, 0);
    lv_obj_set_style_radius(generationBar_, 4, LV_PART_INDICATOR);

    // Evaluation progress.
    evalLabel_ = lv_label_create(progressRow);
    lv_label_set_text(evalLabel_, "Eval: 0/0");
    lv_obj_set_style_text_color(evalLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(evalLabel_, &lv_font_montserrat_12, 0);

    evaluationBar_ = lv_bar_create(progressRow);
    lv_obj_set_size(evaluationBar_, 120, 12);
    lv_bar_set_range(evaluationBar_, 0, 100);
    lv_bar_set_value(evaluationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x6688CC), LV_PART_INDICATOR);
    lv_obj_set_style_radius(evaluationBar_, 4, 0);
    lv_obj_set_style_radius(evaluationBar_, 4, LV_PART_INDICATOR);

    // Fitness stats row.
    lv_obj_t* fitnessRow = lv_obj_create(statsPanel);
    lv_obj_set_size(fitnessRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fitnessRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fitnessRow, 0, 0);
    lv_obj_set_style_pad_all(fitnessRow, 0, 0);
    lv_obj_set_style_pad_gap(fitnessRow, 15, 0);
    lv_obj_set_flex_flow(fitnessRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        fitnessRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(fitnessRow, LV_OBJ_FLAG_SCROLLABLE);

    bestThisGenLabel_ = lv_label_create(fitnessRow);
    lv_label_set_text(bestThisGenLabel_, "This Gen: --");
    lv_obj_set_style_text_color(bestThisGenLabel_, lv_color_hex(0xAAAACC), 0);
    lv_obj_set_style_text_font(bestThisGenLabel_, &lv_font_montserrat_12, 0);

    bestAllTimeLabel_ = lv_label_create(fitnessRow);
    lv_label_set_text(bestAllTimeLabel_, "All Time: --");
    lv_obj_set_style_text_color(bestAllTimeLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(bestAllTimeLabel_, &lv_font_montserrat_12, 0);

    averageLabel_ = lv_label_create(fitnessRow);
    lv_label_set_text(averageLabel_, "Avg: --");
    lv_obj_set_style_text_color(averageLabel_, lv_color_hex(0xAAAACC), 0);
    lv_obj_set_style_text_font(averageLabel_, &lv_font_montserrat_12, 0);

    // ========== BOTTOM: Two world views side by side ==========
    lv_obj_t* bottomRow = lv_obj_create(mainLayout);
    lv_obj_set_size(bottomRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_set_style_pad_gap(bottomRow, 10, 0);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        bottomRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);

    // Left panel: Live feed.
    lv_obj_t* leftPanel = lv_obj_create(bottomRow);
    lv_obj_set_size(leftPanel, 280, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(leftPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftPanel, 0, 0);
    lv_obj_set_style_pad_all(leftPanel, 0, 0);
    lv_obj_set_style_pad_gap(leftPanel, 3, 0);
    lv_obj_set_flex_flow(leftPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        leftPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(leftPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* liveLabel = lv_label_create(leftPanel);
    lv_label_set_text(liveLabel, "Current");
    lv_obj_set_style_text_color(liveLabel, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(liveLabel, &lv_font_montserrat_12, 0);

    worldContainer_ = lv_obj_create(leftPanel);
    lv_obj_set_size(worldContainer_, 270, 270);
    lv_obj_set_style_bg_color(worldContainer_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(worldContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(worldContainer_, 8, 0);
    lv_obj_set_style_border_width(worldContainer_, 2, 0);
    lv_obj_set_style_border_color(worldContainer_, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(worldContainer_, 5, 0);
    lv_obj_clear_flag(worldContainer_, LV_OBJ_FLAG_SCROLLABLE);

    renderer_->initialize(worldContainer_, 9, 9);

    // Right panel: Best snapshot.
    lv_obj_t* rightPanel = lv_obj_create(bottomRow);
    lv_obj_set_size(rightPanel, 280, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rightPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightPanel, 0, 0);
    lv_obj_set_style_pad_all(rightPanel, 0, 0);
    lv_obj_set_style_pad_gap(rightPanel, 3, 0);
    lv_obj_set_flex_flow(rightPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        rightPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);

    bestFitnessLabel_ = lv_label_create(rightPanel);
    lv_label_set_text(bestFitnessLabel_, "Best So Far");
    lv_obj_set_style_text_color(bestFitnessLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(bestFitnessLabel_, &lv_font_montserrat_12, 0);

    bestWorldContainer_ = lv_obj_create(rightPanel);
    lv_obj_set_size(bestWorldContainer_, 270, 270);
    lv_obj_set_style_bg_color(bestWorldContainer_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(bestWorldContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bestWorldContainer_, 8, 0);
    lv_obj_set_style_border_width(bestWorldContainer_, 2, 0);
    lv_obj_set_style_border_color(bestWorldContainer_, lv_color_hex(0x3A3A5A), 0);
    lv_obj_set_style_pad_all(bestWorldContainer_, 5, 0);
    lv_obj_clear_flag(bestWorldContainer_, LV_OBJ_FLAG_SCROLLABLE);

    bestRenderer_->initialize(bestWorldContainer_, 9, 9);

    LOG_INFO(Controls, "Training UI created with live feed and best snapshot views");
}

void TrainingView::destroyUI()
{
    if (renderer_) {
        renderer_->cleanup();
    }
    if (bestRenderer_) {
        bestRenderer_->cleanup();
    }

    if (container_) {
        lv_obj_clean(container_);
    }

    averageLabel_ = nullptr;
    bestAllTimeLabel_ = nullptr;
    bestFitnessLabel_ = nullptr;
    bestThisGenLabel_ = nullptr;
    bestWorldContainer_ = nullptr;
    container_ = nullptr;
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
    // Store for potential best snapshot capture.
    lastRenderedWorld_ = std::make_unique<WorldData>(worldData);
    hasRenderedWorld_ = true;

    if (!renderer_ || !worldContainer_) {
        return;
    }

    renderer_->renderWorldData(worldData, worldContainer_, false, RenderMode::SHARP);
}

void TrainingView::clearPanelContent()
{
    evolutionConfigPanel_.reset();
    evolutionControls_.reset();
    trainingPopulationPanel_.reset();
}

void TrainingView::createCorePanel()
{
    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "TrainingView: No expandable panel available");
        return;
    }
    panel->setWidth(ExpandablePanel::DefaultWidth);

    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "TrainingView: No panel content area available");
        return;
    }

    evolutionControls_ = std::make_unique<EvolutionControls>(
        container, eventSink_, evolutionStarted_, evolutionConfig_, mutationConfig_, trainingSpec_);
    LOG_INFO(Controls, "TrainingView: Created Training Home panel");
}

void TrainingView::createEvolutionConfigPanel()
{
    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "TrainingView: No expandable panel available");
        return;
    }
    panel->setWidth(ExpandablePanel::DefaultWidth);

    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "TrainingView: No panel content area available");
        return;
    }

    evolutionConfigPanel_ = std::make_unique<EvolutionConfigPanel>(
        container, eventSink_, evolutionStarted_, evolutionConfig_, mutationConfig_, trainingSpec_);
    LOG_INFO(Controls, "TrainingView: Created Evolution config panel");
}

void TrainingView::createTrainingPopulationPanel()
{
    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "TrainingView: No expandable panel available");
        return;
    }
    const int displayWidth = lv_disp_get_hor_res(lv_disp_get_default());
    const int maxWidth = displayWidth - IconRail::RAIL_WIDTH;
    int panelWidth = ExpandablePanel::DefaultWidth * 2;
    if (panelWidth > maxWidth && maxWidth > 0) {
        panelWidth = maxWidth;
    }
    if (panelWidth < ExpandablePanel::DefaultWidth) {
        panelWidth = ExpandablePanel::DefaultWidth;
    }
    panel->setWidth(panelWidth);

    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "TrainingView: No panel content area available");
        return;
    }

    trainingPopulationPanel_ = std::make_unique<TrainingPopulationPanel>(
        container, eventSink_, evolutionStarted_, evolutionConfig_, trainingSpec_);
    LOG_INFO(Controls, "TrainingView: Created Training population panel");
}

void TrainingView::updateProgress(const Api::EvolutionProgress& progress)
{
    if (!genLabel_ || !evalLabel_ || !generationBar_ || !evaluationBar_) return;

    // Detect evaluation change with fitness improvement for best snapshot.
    // Note: Depends on renderWorld() being called first to populate lastRenderedWorld_.
    const bool evalChanged = (progress.currentEval != lastEval_)
        || (progress.generation != lastGeneration_ && progress.currentEval == 0);
    const bool fitnessImproved =
        (progress.bestFitnessAllTime > lastBestFitness_ + FITNESS_IMPROVEMENT_EPSILON);

    if (evalChanged && fitnessImproved && hasRenderedWorld_) {
        // Last rendered frame is the final frame of the new best!
        bestWorldData_ = std::make_unique<WorldData>(*lastRenderedWorld_);
        bestFitness_ = progress.bestFitnessAllTime;
        bestGeneration_ = progress.generation;
        renderBestWorld();

        LOG_INFO(
            Controls,
            "TrainingView: Captured best snapshot (fitness={:.4f}, gen={})",
            bestFitness_,
            bestGeneration_);
    }

    // Update tracking state.
    lastEval_ = progress.currentEval;
    lastGeneration_ = progress.generation;
    lastBestFitness_ = progress.bestFitnessAllTime;

    // Detect training completion.
    const bool isComplete =
        (progress.maxGenerations > 0 && progress.generation >= progress.maxGenerations
         && progress.currentEval >= progress.populationSize);

    if (isComplete) {
        setEvolutionCompleted(progress.bestGenomeId);
    }

    char buf[64];

    // Update time displays (compact format).
    if (totalTimeLabel_) {
        snprintf(buf, sizeof(buf), "Time: %.1fs", progress.totalTrainingSeconds);
        lv_label_set_text(totalTimeLabel_, buf);
    }

    if (simTimeLabel_) {
        snprintf(buf, sizeof(buf), "Sim: %.1fs", progress.currentSimTime);
        lv_label_set_text(simTimeLabel_, buf);
    }

    if (speedupLabel_) {
        snprintf(buf, sizeof(buf), "Speed: %.1fx", progress.speedupFactor);
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

    // Update fitness labels (compact format).
    if (bestThisGenLabel_) {
        snprintf(buf, sizeof(buf), "This Gen: %.2f", progress.bestFitnessThisGen);
        lv_label_set_text(bestThisGenLabel_, buf);
    }

    if (bestAllTimeLabel_) {
        snprintf(buf, sizeof(buf), "All Time: %.2f", progress.bestFitnessAllTime);
        lv_label_set_text(bestAllTimeLabel_, buf);
    }

    if (averageLabel_) {
        snprintf(buf, sizeof(buf), "Avg: %.2f", progress.averageFitness);
        lv_label_set_text(averageLabel_, buf);
    }
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
    if (trainingPopulationPanel_) {
        trainingPopulationPanel_->setEvolutionStarted(started);
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
    if (trainingPopulationPanel_) {
        trainingPopulationPanel_->setEvolutionCompleted();
    }
}

void TrainingView::renderBestWorld()
{
    if (!bestRenderer_ || !bestWorldContainer_ || !bestWorldData_) {
        return;
    }

    // Render the best world snapshot.
    bestRenderer_->renderWorldData(*bestWorldData_, bestWorldContainer_, false, RenderMode::SHARP);

    // Update the label to show fitness info.
    if (bestFitnessLabel_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Best: %.2f (Gen %d)", bestFitness_, bestGeneration_);
        lv_label_set_text(bestFitnessLabel_, buf);
    }
}

} // namespace Ui
} // namespace DirtSim
