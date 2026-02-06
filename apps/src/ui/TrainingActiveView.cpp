#include "TrainingActiveView.h"
#include "UiComponentManager.h"
#include "controls/EvolutionControls.h"
#include "controls/ExpandablePanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/WorldData.h"
#include "rendering/CellRenderer.h"
#include "rendering/RenderMode.h"
#include "rendering/Starfield.h"
#include "server/api/EvolutionProgress.h"
#include "state-machine/Event.h"
#include "state-machine/EventSink.h"
#include "ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

namespace {
struct BestRenderRequest {
    TrainingActiveView* view = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
};

} // namespace

TrainingActiveView::TrainingActiveView(
    UiComponentManager* uiManager,
    EventSink& eventSink,
    Network::WebSocketServiceInterface* wsService,
    UserSettings& userSettings,
    const Starfield::Snapshot* starfieldSnapshot)
    : uiManager_(uiManager),
      eventSink_(eventSink),
      wsService_(wsService),
      userSettings_(userSettings),
      starfieldSnapshot_(starfieldSnapshot)
{
    alive_ = std::make_shared<std::atomic<bool>>(true);
    createUI();
}

TrainingActiveView::~TrainingActiveView()
{
    if (alive_) {
        alive_->store(false);
    }
    destroyUI();
}

void TrainingActiveView::createUI()
{
    DIRTSIM_ASSERT(uiManager_, "TrainingActiveView requires valid UiComponentManager");

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

    createActiveUI(displayWidth, displayHeight);
}

void TrainingActiveView::createActiveUI(int displayWidth, int displayHeight)
{
    starfield_ =
        std::make_unique<Starfield>(container_, displayWidth, displayHeight, starfieldSnapshot_);
    renderer_ = std::make_unique<CellRenderer>();
    bestRenderer_ = std::make_unique<CellRenderer>();

    // Main layout: panel column + stream panel + stats/world content.
    contentRow_ = lv_obj_create(container_);
    lv_obj_set_size(contentRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(contentRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentRow_, 0, 0);
    lv_obj_set_style_pad_all(contentRow_, 0, 0);
    lv_obj_set_style_pad_gap(contentRow_, 10, 0);
    lv_obj_set_flex_flow(contentRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        contentRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(contentRow_, LV_OBJ_FLAG_SCROLLABLE);

    panel_ = std::make_unique<ExpandablePanel>(contentRow_);
    panel_->show();
    panel_->setWidth(ExpandablePanel::DefaultWidth);
    panelContent_ = panel_->getContentArea();

    createStreamPanel(contentRow_);

    mainLayout_ = lv_obj_create(contentRow_);
    lv_obj_set_size(mainLayout_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(mainLayout_, 1);
    lv_obj_set_style_bg_opa(mainLayout_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mainLayout_, 0, 0);
    lv_obj_set_style_pad_all(mainLayout_, 5, 0);
    lv_obj_set_style_pad_gap(mainLayout_, 5, 0);
    lv_obj_set_flex_flow(mainLayout_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        mainLayout_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(mainLayout_, LV_OBJ_FLAG_SCROLLABLE);

    // ========== TOP: Stats panel (condensed) ==========
    statsPanel_ = lv_obj_create(mainLayout_);
    lv_obj_set_size(statsPanel_, 580, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(statsPanel_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(statsPanel_, LV_OPA_90, 0);
    lv_obj_set_style_radius(statsPanel_, 8, 0);
    lv_obj_set_style_border_width(statsPanel_, 1, 0);
    lv_obj_set_style_border_color(statsPanel_, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(statsPanel_, 10, 0);
    lv_obj_set_style_pad_gap(statsPanel_, 4, 0);
    lv_obj_set_flex_flow(statsPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statsPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(statsPanel_, LV_OBJ_FLAG_SCROLLABLE);

    // Title row: "EVOLUTION" + status.
    lv_obj_t* titleRow = lv_obj_create(statsPanel_);
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
    lv_obj_t* timeRow = lv_obj_create(statsPanel_);
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
    lv_obj_t* progressRow = lv_obj_create(statsPanel_);
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
    lv_obj_t* fitnessRow = lv_obj_create(statsPanel_);
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

    LOG_INFO(Controls, "Training active UI created with live feed and best snapshot views");
}

void TrainingActiveView::destroyUI()
{
    if (renderer_) {
        renderer_->cleanup();
    }
    if (bestRenderer_) {
        bestRenderer_->cleanup();
    }

    starfield_.reset();
    panel_.reset();

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
    statsPanel_ = nullptr;
    bottomRow_ = nullptr;
    contentRow_ = nullptr;
    mainLayout_ = nullptr;
    streamPanel_ = nullptr;
    streamIntervalStepper_ = nullptr;
    pauseResumeButton_ = nullptr;
    pauseResumeLabel_ = nullptr;
    stopTrainingButton_ = nullptr;
    simTimeLabel_ = nullptr;
    speedupLabel_ = nullptr;
    statusLabel_ = nullptr;
    totalTimeLabel_ = nullptr;
    worldContainer_ = nullptr;
    panelContent_ = nullptr;
}

void TrainingActiveView::renderWorld(const WorldData& worldData)
{
    if (!renderer_ || !worldContainer_) {
        return;
    }

    renderer_->renderWorldData(worldData, worldContainer_, false, RenderMode::SHARP);
}

void TrainingActiveView::updateBestSnapshot(
    const WorldData& worldData, double fitness, int generation)
{
    bestWorldData_ = std::make_unique<WorldData>(worldData);
    bestFitness_ = fitness;
    bestGeneration_ = generation;
    int nonZeroColors = 0;
    float maxBrightness = 0.0f;
    for (const auto& color : worldData.colors.data) {
        if (color.r > 0.0f || color.g > 0.0f || color.b > 0.0f) {
            ++nonZeroColors;
        }
        const float brightness = 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
        maxBrightness = std::max(maxBrightness, brightness);
    }
    LOG_INFO(
        Controls,
        "TrainingActiveView: updateBestSnapshot fitness={:.4f} gen={} world={}x{} cells={} "
        "colors={} organism_ids={} nonzero_colors={} max_brightness={:.3f}",
        fitness,
        generation,
        worldData.width,
        worldData.height,
        worldData.cells.size(),
        worldData.colors.size(),
        worldData.organism_ids.size(),
        nonZeroColors,
        maxBrightness);
    if (bestAllTimeLabel_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "All Time: %.2f", fitness);
        lv_label_set_text(bestAllTimeLabel_, buf);
    }
    scheduleBestRender();
}

void TrainingActiveView::clearPanelContent()
{
    evolutionControls_.reset();
    if (panel_) {
        panel_->clearContent();
        panel_->setWidth(ExpandablePanel::DefaultWidth);
    }
}

void TrainingActiveView::setStreamIntervalMs(int value)
{
    userSettings_.streamIntervalMs = value;
    if (streamIntervalStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(streamIntervalStepper_, value);
    }
}

void TrainingActiveView::setTrainingPaused(bool paused)
{
    if (pauseResumeLabel_) {
        lv_label_set_text(pauseResumeLabel_, paused ? "Resume" : "Pause");
    }
    if (pauseResumeButton_) {
        LVGLBuilder::ActionButtonBuilder::setIcon(
            pauseResumeButton_, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    }
}

void TrainingActiveView::createCorePanel()
{
    if (!panel_) {
        LOG_ERROR(Controls, "TrainingActiveView: No training panel available");
        return;
    }
    panel_->setWidth(ExpandablePanel::DefaultWidth);

    if (!panelContent_) {
        LOG_ERROR(Controls, "TrainingActiveView: No panel content area available");
        return;
    }

    evolutionControls_ = std::make_unique<EvolutionControls>(
        panelContent_, eventSink_, evolutionStarted_, userSettings_.trainingSpec);
    LOG_INFO(Controls, "TrainingActiveView: Created Training Home panel");
}

void TrainingActiveView::updateProgress(const Api::EvolutionProgress& progress)
{
    if (!genLabel_ || !evalLabel_ || !generationBar_ || !evaluationBar_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastProgressUiLog_ == std::chrono::steady_clock::time_point{}) {
        lastProgressUiLog_ = now;
        progressUiUpdateCount_ = 0;
    }
    progressUiUpdateCount_++;
    const auto elapsed = now - lastProgressUiLog_;
    if (elapsed >= std::chrono::seconds(1)) {
        const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        const double rate = elapsedSeconds > 0.0 ? (progressUiUpdateCount_ / elapsedSeconds) : 0.0;
        LOG_INFO(
            Controls,
            "TrainingActiveView progress UI: gen {}/{}, eval {}/{}, time {:.1f}s sim {:.1f}s "
            "speed {:.1f}x eta {:.1f}s updates {:.1f}/s",
            progress.generation,
            progress.maxGenerations,
            progress.currentEval,
            progress.populationSize,
            progress.totalTrainingSeconds,
            progress.currentSimTime,
            progress.speedupFactor,
            progress.etaSeconds,
            rate);
        progressUiUpdateCount_ = 0;
        lastProgressUiLog_ = now;
    }

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

    // LVGL doesn't always repaint this panel promptly under high-rate event load.
    // Invalidate at a bounded rate so we don't force full-panel redraw for every message.
    if (statsPanel_) {
        const auto invalidateNow = std::chrono::steady_clock::now();
        if (lastStatsInvalidate_ == std::chrono::steady_clock::time_point{}
            || (invalidateNow - lastStatsInvalidate_) >= std::chrono::milliseconds(16)) {
            lv_obj_invalidate(statsPanel_);
            lastStatsInvalidate_ = invalidateNow;
        }
    }

    const auto logNow = std::chrono::steady_clock::now();
    if (lastLabelStateLog_ == std::chrono::steady_clock::time_point{}) {
        lastLabelStateLog_ = logNow;
    }
    if (logNow - lastLabelStateLog_ >= std::chrono::seconds(1)) {
        lastLabelStateLog_ = logNow;
    }
}

void TrainingActiveView::updateAnimations()
{
    if (starfield_ && starfield_->isVisible()) {
        starfield_->update();
    }
}

Starfield::Snapshot TrainingActiveView::captureStarfieldSnapshot() const
{
    DIRTSIM_ASSERT(starfield_, "TrainingActiveView requires Starfield");
    return starfield_->capture();
}

void TrainingActiveView::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    if (started) {
        bestWorldData_.reset();
        bestFitness_ = 0.0;
        bestGeneration_ = 0;
        hasShownBestSnapshot_ = false;
    }

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

    if (evolutionControls_) {
        evolutionControls_->setEvolutionStarted(started);
    }

    if (pauseResumeButton_) {
        if (started) {
            lv_obj_clear_flag(pauseResumeButton_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_state(pauseResumeButton_, LV_STATE_DISABLED);
            lv_obj_set_style_opa(pauseResumeButton_, LV_OPA_COVER, 0);
        }
        else {
            lv_obj_add_flag(pauseResumeButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    setTrainingPaused(false);
}

void TrainingActiveView::setEvolutionCompleted(GenomeId bestGenomeId)
{
    evolutionStarted_ = false;

    if (statusLabel_) {
        lv_label_set_text(statusLabel_, "Complete!");
        lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xFFDD66), 0);
    }

    if (evolutionControls_) {
        evolutionControls_->setEvolutionCompleted(bestGenomeId);
    }

    if (pauseResumeButton_) {
        lv_obj_add_flag(pauseResumeButton_, LV_OBJ_FLAG_HIDDEN);
    }
    setTrainingPaused(false);
}

void TrainingActiveView::renderBestWorld()
{
    if (!bestRenderer_ || !bestWorldContainer_ || !bestWorldData_) {
        LOG_WARN(
            Controls,
            "TrainingActiveView: renderBestWorld skipped (renderer={}, container={}, data={})",
            static_cast<const void*>(bestRenderer_.get()),
            static_cast<const void*>(bestWorldContainer_),
            static_cast<const void*>(bestWorldData_.get()));
        return;
    }

    if (bestWorldData_->width <= 0 || bestWorldData_->height <= 0
        || bestWorldData_->cells.empty()) {
        LOG_WARN(
            Controls,
            "TrainingActiveView: renderBestWorld invalid data (world={}x{} cells={} colors={} "
            "organism_ids={})",
            bestWorldData_->width,
            bestWorldData_->height,
            bestWorldData_->cells.size(),
            bestWorldData_->colors.size(),
            bestWorldData_->organism_ids.size());
        return;
    }

    const int32_t containerWidth = lv_obj_get_width(bestWorldContainer_);
    const int32_t containerHeight = lv_obj_get_height(bestWorldContainer_);
    LOG_INFO(
        Controls,
        "TrainingActiveView: renderBestWorld container={}x{} world={}x{}",
        containerWidth,
        containerHeight,
        bestWorldData_->width,
        bestWorldData_->height);

    bestRenderer_->renderWorldData(*bestWorldData_, bestWorldContainer_, false, RenderMode::SHARP);
    if (!hasShownBestSnapshot_) {
        lv_refr_now(lv_display_get_default());
        hasShownBestSnapshot_ = true;
    }

    if (bestFitnessLabel_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Best: %.2f (Gen %d)", bestFitness_, bestGeneration_);
        lv_label_set_text(bestFitnessLabel_, buf);
    }
}

void TrainingActiveView::scheduleBestRender()
{
    if (!bestWorldContainer_ || !bestRenderer_ || !bestWorldData_) {
        return;
    }

    auto* request = new BestRenderRequest{
        .view = this,
        .alive = alive_,
    };
    lv_async_call(TrainingActiveView::renderBestWorldAsync, request);
}

void TrainingActiveView::renderBestWorldAsync(void* data)
{
    auto* request = static_cast<BestRenderRequest*>(data);
    if (!request) {
        return;
    }

    const bool alive = request->alive && request->alive->load();
    if (alive && request->view) {
        request->view->renderBestWorld();
        if (request->view->bestWorldContainer_) {
            lv_obj_invalidate(request->view->bestWorldContainer_);
        }
    }

    delete request;
}

void TrainingActiveView::createStreamPanel(lv_obj_t* parent)
{
    streamPanel_ = lv_obj_create(parent);
    lv_obj_set_size(streamPanel_, 220, LV_PCT(100));
    lv_obj_set_style_bg_color(streamPanel_, lv_color_hex(0x141420), 0);
    lv_obj_set_style_bg_opa(streamPanel_, LV_OPA_90, 0);
    lv_obj_set_style_radius(streamPanel_, 0, 0);
    lv_obj_set_style_border_width(streamPanel_, 1, 0);
    lv_obj_set_style_border_color(streamPanel_, lv_color_hex(0x2A2A44), 0);
    lv_obj_set_style_pad_all(streamPanel_, 10, 0);
    lv_obj_set_style_pad_row(streamPanel_, 10, 0);
    lv_obj_set_flex_flow(streamPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        streamPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(streamPanel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(streamPanel_);
    lv_label_set_text(titleLabel, "Stream");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_14, 0);

    streamIntervalStepper_ = LVGLBuilder::actionStepper(streamPanel_)
                                 .label("Interval (ms)")
                                 .range(0, 5000)
                                 .step(100)
                                 .value(userSettings_.streamIntervalMs)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(100))
                                 .callback(onStreamIntervalChanged, this)
                                 .buildOrLog();

    LVGLBuilder::ActionButtonBuilder stopBuilder(streamPanel_);
    stopBuilder.text("Stop Training")
        .icon(LV_SYMBOL_STOP)
        .mode(LVGLBuilder::ActionMode::Push)
        .layoutRow()
        .alignCenter()
        .width(LV_PCT(100))
        .height(56)
        .backgroundColor(0xAA2222)
        .callback(onStopTrainingClicked, this);
    stopTrainingButton_ = stopBuilder.buildOrLog();

    LVGLBuilder::ActionButtonBuilder pauseBuilder(streamPanel_);
    pauseBuilder.text("Pause")
        .icon(LV_SYMBOL_PAUSE)
        .mode(LVGLBuilder::ActionMode::Push)
        .layoutRow()
        .alignCenter()
        .width(LV_PCT(100))
        .height(56)
        .backgroundColor(0x0066CC)
        .callback(onPauseResumeClicked, this);
    pauseResumeButton_ = pauseBuilder.buildOrLog();
    pauseResumeLabel_ = pauseBuilder.getLabel();
}

void TrainingActiveView::onStreamIntervalChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self || !self->streamIntervalStepper_) {
        return;
    }

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->streamIntervalStepper_);
    self->setStreamIntervalMs(value);
    self->eventSink_.queueEvent(TrainingStreamConfigChangedEvent{ .intervalMs = value });
}

void TrainingActiveView::onStopTrainingClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->eventSink_.queueEvent(StopTrainingClickedEvent{});
}

void TrainingActiveView::onPauseResumeClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->eventSink_.queueEvent(TrainingPauseResumeClickedEvent{});
}

bool TrainingActiveView::isTrainingResultModalVisible() const
{
    return false;
}

} // namespace Ui
} // namespace DirtSim
