#include "TrainingView.h"
#include "UiComponentManager.h"
#include "controls/EvolutionControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/GenomeBrowserPanel.h"
#include "controls/IconRail.h"
#include "controls/TrainingConfigPanel.h"
#include "controls/TrainingResultBrowserPanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/WorldData.h"
#include "core/reflect.h"
#include "rendering/CellRenderer.h"
#include "rendering/RenderMode.h"
#include "rendering/Starfield.h"
#include "server/api/EvolutionProgress.h"
#include "state-machine/Event.h"
#include "state-machine/EventSink.h"
#include "ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {

namespace {
constexpr double FITNESS_IMPROVEMENT_EPSILON = 0.001;
constexpr int kBrowserRightGap = 60;

int computeBrowserPanelWidth()
{
    const int displayWidth = lv_disp_get_hor_res(lv_disp_get_default());
    const int maxWidth =
        displayWidth > 0 ? displayWidth - IconRail::RAIL_WIDTH - kBrowserRightGap : 0;
    int panelWidth = ExpandablePanel::DefaultWidth * 2;
    if (maxWidth > 0) {
        panelWidth = std::min(panelWidth, maxWidth);
    }
    if (panelWidth < ExpandablePanel::DefaultWidth) {
        panelWidth = ExpandablePanel::DefaultWidth;
    }
    return panelWidth;
}
} // namespace

TrainingView::TrainingView(
    UiComponentManager* uiManager,
    EventSink& eventSink,
    Network::WebSocketServiceInterface* wsService)
    : uiManager_(uiManager), eventSink_(eventSink), wsService_(wsService)
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
    starfield_ = std::make_unique<Starfield>(container_, displayWidth, displayHeight);

    // Main layout: column with stats on top, world views on bottom.
    mainLayout_ = lv_obj_create(container_);
    lv_obj_set_size(mainLayout_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(mainLayout_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mainLayout_, 0, 0);
    lv_obj_set_style_pad_all(mainLayout_, 5, 0);
    lv_obj_set_style_pad_gap(mainLayout_, 5, 0);
    lv_obj_set_flex_flow(mainLayout_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        mainLayout_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(mainLayout_, LV_OBJ_FLAG_SCROLLABLE);

    // ========== TOP: Stats panel (condensed) ==========
    lv_obj_t* statsPanel = lv_obj_create(mainLayout_);
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
    bottomRow_ = lv_obj_create(mainLayout_);
    lv_obj_set_size(bottomRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottomRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow_, 0, 0);
    lv_obj_set_style_pad_all(bottomRow_, 0, 0);
    lv_obj_set_style_pad_gap(bottomRow_, 10, 0);
    lv_obj_set_flex_flow(bottomRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        bottomRow_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bottomRow_, LV_OBJ_FLAG_SCROLLABLE);

    // Left panel: Live feed.
    lv_obj_t* leftPanel = lv_obj_create(bottomRow_);
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
    lv_obj_t* rightPanel = lv_obj_create(bottomRow_);
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

    updateEvolutionVisibility();

    LOG_INFO(Controls, "Training UI created with live feed and best snapshot views");
}

void TrainingView::destroyUI()
{
    hideTrainingResultModal();

    if (renderer_) {
        renderer_->cleanup();
    }
    if (bestRenderer_) {
        bestRenderer_->cleanup();
    }

    starfield_.reset();

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
    bottomRow_ = nullptr;
    mainLayout_ = nullptr;
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
    evolutionControls_.reset();
    genomeBrowserPanel_.reset();
    trainingConfigPanel_.reset();
    trainingResultBrowserPanel_.reset();
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
        container, eventSink_, evolutionStarted_, trainingSpec_);
    LOG_INFO(Controls, "TrainingView: Created Training Home panel");
}

void TrainingView::createGenomeBrowserPanel()
{
    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "TrainingView: No expandable panel available");
        return;
    }
    panel->setWidth(computeBrowserPanelWidth());

    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "TrainingView: No panel content area available");
        return;
    }

    genomeBrowserPanel_ = std::make_unique<GenomeBrowserPanel>(container, wsService_);
    LOG_INFO(Controls, "TrainingView: Created Genome browser panel");
}

void TrainingView::createTrainingConfigPanel()
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

    trainingConfigPanel_ = std::make_unique<TrainingConfigPanel>(
        container,
        eventSink_,
        panel,
        evolutionStarted_,
        evolutionConfig_,
        mutationConfig_,
        trainingSpec_);
    LOG_INFO(Controls, "TrainingView: Created Training config panel");
}

void TrainingView::createTrainingResultBrowserPanel()
{
    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "TrainingView: No expandable panel available");
        return;
    }
    panel->setWidth(computeBrowserPanelWidth());

    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "TrainingView: No panel content area available");
        return;
    }

    trainingResultBrowserPanel_ =
        std::make_unique<TrainingResultBrowserPanel>(container, wsService_);
    LOG_INFO(Controls, "TrainingView: Created Training result browser panel");
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

void TrainingView::updateAnimations()
{
    if (starfield_ && starfield_->isVisible()) {
        starfield_->update();
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
    if (trainingConfigPanel_) {
        trainingConfigPanel_->setEvolutionStarted(started);
    }

    updateEvolutionVisibility();
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
    if (trainingConfigPanel_) {
        trainingConfigPanel_->setEvolutionCompleted();
    }

    updateEvolutionVisibility();
}

void TrainingView::updateEvolutionVisibility()
{
    const bool hasStarfield = starfield_ && starfield_->getCanvas();
    const bool showEvolution = evolutionStarted_ || !hasStarfield;

    if (mainLayout_) {
        if (showEvolution) {
            lv_obj_clear_flag(mainLayout_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(mainLayout_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
        else {
            lv_obj_add_flag(mainLayout_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(mainLayout_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    }

    if (bottomRow_) {
        if (showEvolution) {
            lv_obj_clear_flag(bottomRow_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(bottomRow_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
        else {
            lv_obj_add_flag(bottomRow_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(bottomRow_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    }

    if (starfield_) {
        starfield_->setVisible(!showEvolution);
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

void TrainingView::showTrainingResultModal(
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

void TrainingView::hideTrainingResultModal()
{
    if (trainingResultOverlay_) {
        lv_obj_del(trainingResultOverlay_);
        trainingResultOverlay_ = nullptr;
    }

    trainingResultCountLabel_ = nullptr;
    trainingResultSaveStepper_ = nullptr;
    trainingResultSaveButton_ = nullptr;
    primaryCandidates_.clear();
    trainingResultSummary_ = Api::TrainingResult::Summary{};
}

bool TrainingView::isTrainingResultModalVisible() const
{
    return trainingResultOverlay_ != nullptr;
}

void TrainingView::updateTrainingResultSaveButton()
{
    if (!trainingResultSaveButton_) {
        return;
    }
    int32_t value = 0;
    if (trainingResultSaveStepper_) {
        value = LVGLBuilder::ActionStepperBuilder::getValue(trainingResultSaveStepper_);
    }

    const bool enabled = (value > 0) && !primaryCandidates_.empty();
    if (enabled) {
        lv_obj_clear_state(trainingResultSaveButton_, LV_STATE_DISABLED);
        lv_obj_set_style_opa(trainingResultSaveButton_, LV_OPA_COVER, 0);
    }
    else {
        lv_obj_add_state(trainingResultSaveButton_, LV_STATE_DISABLED);
        lv_obj_set_style_opa(trainingResultSaveButton_, LV_OPA_50, 0);
    }
}

std::vector<GenomeId> TrainingView::getTrainingResultSaveIds() const
{
    if (!trainingResultSaveStepper_) {
        return {};
    }

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(trainingResultSaveStepper_);
    return getTrainingResultSaveIdsForCount(value);
}

std::vector<GenomeId> TrainingView::getTrainingResultSaveIdsForCount(int count) const
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

void TrainingView::onTrainingResultSaveClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    TrainingResultSaveClickedEvent evt;
    evt.ids = self->getTrainingResultSaveIds();
    self->eventSink_.queueEvent(evt);
}

void TrainingView::onTrainingResultDiscardClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->eventSink_.queueEvent(TrainingResultDiscardClickedEvent{});
}

void TrainingView::onTrainingResultCountChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->updateTrainingResultSaveButton();
}

} // namespace Ui
} // namespace DirtSim
