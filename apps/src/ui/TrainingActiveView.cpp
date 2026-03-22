#include "TrainingActiveView.h"
#include "UiComponentManager.h"
#include "controls/ScenarioControlsFactory.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/WorldData.h"
#include "core/organisms/evolution/NesPolicyLayout.h"
#include "rendering/CellRenderer.h"
#include "rendering/RenderMode.h"
#include "rendering/Starfield.h"
#include "rendering/VideoSurface.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/FitnessPresentation.h"
#include "state-machine/Event.h"
#include "state-machine/EventSink.h"
#include "ui_builders/LVGLBuilder.h"
#include "widgets/TimeSeriesPlotWidget.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <lvgl/lvgl.h>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace DirtSim {
namespace Ui {

namespace {
constexpr std::array<uint8_t, 8> kNesButtonMasks{
    NesPolicyLayout::ButtonUp,     NesPolicyLayout::ButtonDown,  NesPolicyLayout::ButtonLeft,
    NesPolicyLayout::ButtonRight,  NesPolicyLayout::ButtonA,     NesPolicyLayout::ButtonB,
    NesPolicyLayout::ButtonSelect, NesPolicyLayout::ButtonStart,
};
constexpr std::array<const char*, 8> kNesButtonLabels{ "U", "D", "L", "R", "A", "B", "SE", "ST" };
constexpr std::array<const char*, 4> kNesOutputLabels{ "X", "Y", "A", "B" };
constexpr float kNesOutputBarMaxMagnitude = 4.0f;

const char* trainingPhaseLabel(TrainingPhase phase)
{
    switch (phase) {
        case TrainingPhase::Normal:
            return "Normal";
        case TrainingPhase::Plateau:
            return "Plateau";
        case TrainingPhase::Stuck:
            return "Stuck";
        case TrainingPhase::Recovery:
            return "Recovery";
    }
    return "Unknown";
}

lv_color_t trainingPhaseTextColor(TrainingPhase phase)
{
    switch (phase) {
        case TrainingPhase::Normal:
            return lv_color_hex(0x00CC66);
        case TrainingPhase::Plateau:
            return lv_color_hex(0xFFAA33);
        case TrainingPhase::Stuck:
            return lv_color_hex(0xFF6666);
        case TrainingPhase::Recovery:
            return lv_color_hex(0x66CCFF);
    }
    return lv_color_hex(0x888888);
}

const char* adaptiveMutationModeLabel(AdaptiveMutationMode mode)
{
    switch (mode) {
        case AdaptiveMutationMode::Baseline:
            return "Baseline";
        case AdaptiveMutationMode::Explore:
            return "Explore";
        case AdaptiveMutationMode::Rescue:
            return "Rescue";
        case AdaptiveMutationMode::Recover:
            return "Recover";
    }
    return "Unknown";
}

lv_color_t adaptiveMutationModeTextColor(AdaptiveMutationMode mode)
{
    switch (mode) {
        case AdaptiveMutationMode::Baseline:
            return lv_color_hex(0x00CC66);
        case AdaptiveMutationMode::Explore:
            return lv_color_hex(0xFFAA33);
        case AdaptiveMutationMode::Rescue:
            return lv_color_hex(0xFF6666);
        case AdaptiveMutationMode::Recover:
            return lv_color_hex(0x66CCFF);
    }
    return lv_color_hex(0x888888);
}

void setCompactStatsLabelStyle(lv_obj_t* label, lv_color_t color)
{
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

void setCompactStatsBadgeAccent(lv_obj_t* label, lv_color_t color)
{
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_color(label, color, 0);
    lv_obj_set_style_border_color(label, color, 0);
}

void setCompactStatsBadgeStyle(lv_obj_t* label, lv_color_t color)
{
    setCompactStatsLabelStyle(label, color);
    lv_obj_set_style_bg_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_20, 0);
    lv_obj_set_style_border_color(label, color, 0);
    lv_obj_set_style_border_opa(label, LV_OPA_40, 0);
    lv_obj_set_style_border_width(label, 1, 0);
    lv_obj_set_style_pad_left(label, 8, 0);
    lv_obj_set_style_pad_right(label, 8, 0);
    lv_obj_set_style_pad_top(label, 3, 0);
    lv_obj_set_style_pad_bottom(label, 3, 0);
    lv_obj_set_style_radius(label, 6, 0);
}

struct BestRenderRequest {
    TrainingActiveView* view = nullptr;
    std::shared_ptr<std::atomic<bool>> alive;
};

bool isNesControllerOverlayEnabled(const UserSettings& userSettings)
{
    return userSettings.uiTraining.nesControllerOverlayEnabled.value_or(false);
}

float normalizeNesOverlayOutput(float value)
{
    return std::clamp(value / kNesOutputBarMaxMagnitude, -1.0f, 1.0f);
}

std::string nesControllerSourceLabelBuild(NesGameAdapterControllerSource source)
{
    switch (source) {
        case NesGameAdapterControllerSource::InferredPolicy:
            return "POLICY";
        case NesGameAdapterControllerSource::ScriptedSetup:
            return "SCRIPT";
        case NesGameAdapterControllerSource::LiveInput:
            return "LIVE";
    }
    return "POLICY";
}

std::string nesOutputValueTextBuild(float value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string presentationMetricDetailTextBuild(const Api::FitnessPresentationMetric& metric)
{
    std::ostringstream detail;
    detail << std::fixed;

    bool hasDetail = false;
    if (metric.reference.has_value()) {
        detail << "ref " << std::setprecision(4) << metric.reference.value();
        if (!metric.unit.empty()) {
            detail << " " << metric.unit;
        }
        hasDetail = true;
    }
    if (metric.normalized.has_value()) {
        if (hasDetail) {
            detail << " | ";
        }
        detail << "norm " << std::setprecision(4) << metric.normalized.value();
        hasDetail = true;
    }

    return hasDetail ? detail.str() : std::string{};
}

std::string presentationNumberTextBuild(double value)
{
    const bool isIntegerLike = std::abs(value - std::round(value)) < 0.00005;
    std::ostringstream text;
    if (isIntegerLike) {
        text << std::llround(value);
    }
    else {
        text << std::fixed << std::setprecision(4) << value;
    }
    return text.str();
}

std::string presentationValueTextBuild(const Api::FitnessPresentationMetric& metric)
{
    std::string valueText = presentationNumberTextBuild(metric.value);
    if (!metric.unit.empty()) {
        valueText += " ";
        valueText += metric.unit;
    }
    return valueText;
}

std::string presentationSectionScoreTextBuild(const Api::FitnessPresentationSection& section)
{
    if (!section.score.has_value()) {
        return std::string{};
    }

    std::ostringstream score;
    score << "Score " << std::fixed << std::setprecision(4) << section.score.value();
    return score.str();
}

void setPresentationCardStyle(lv_obj_t* card, lv_color_t bgColor, lv_color_t borderColor)
{
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, bgColor, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_radius(card, LVGLBuilder::Style::RADIUS, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, borderColor, 0);
    lv_obj_set_style_pad_all(card, LVGLBuilder::Style::TROUGH_PADDING, 0);
    lv_obj_set_style_pad_gap(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

void setPresentationMetricValueLabelStyle(lv_obj_t* label)
{
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE8F0FF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
}

void setPresentationMetricLabelStyle(lv_obj_t* label)
{
    lv_obj_set_style_text_color(label, lv_color_hex(0xD6DFEF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

void setPresentationDetailLabelStyle(lv_obj_t* label)
{
    lv_obj_set_style_text_color(label, lv_color_hex(0x90A4BF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

void setPresentationWrappedLabelCommon(lv_obj_t* label)
{
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

void setTransparentColumnLayout(lv_obj_t* container, int gapPx)
{
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_gap(container, gapPx, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* createLongTermCollapsibleSection(
    lv_obj_t* parent,
    const char* title,
    uint32_t accentColor,
    bool initiallyExpanded,
    int contentGapPx)
{
    auto sectionBuilder = LVGLBuilder::collapsiblePanel(parent);
    if (lv_obj_t* sectionPanel = sectionBuilder.title(title)
                                     .size(LV_PCT(100))
                                     .initiallyExpanded(initiallyExpanded)
                                     .backgroundColor(0x141420)
                                     .headerColor(0x1A1A2E)
                                     .buildOrLog()) {
        lv_obj_t* sectionContent = sectionBuilder.getContent();

        lv_obj_set_style_bg_opa(sectionPanel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sectionPanel, 0, 0);
        lv_obj_set_style_pad_all(sectionPanel, 0, 0);
        lv_obj_set_style_pad_gap(sectionPanel, 6, 0);

        if (lv_obj_t* sectionHeader = sectionBuilder.getHeader()) {
            lv_obj_set_style_bg_opa(sectionHeader, LV_OPA_60, 0);
            lv_obj_set_style_border_width(sectionHeader, 1, 0);
            lv_obj_set_style_border_color(sectionHeader, lv_color_hex(0x2A2A44), 0);
            lv_obj_set_style_pad_left(sectionHeader, 8, 0);
            lv_obj_set_style_pad_right(sectionHeader, 8, 0);
        }

        if (lv_obj_t* sectionIndicator = sectionBuilder.getIndicator()) {
            lv_obj_set_style_text_color(sectionIndicator, lv_color_hex(accentColor), 0);
            lv_obj_set_style_text_font(sectionIndicator, &lv_font_montserrat_16, 0);
        }

        if (lv_obj_t* sectionTitle = sectionBuilder.getTitleLabel()) {
            lv_obj_set_style_text_color(sectionTitle, lv_color_hex(accentColor), 0);
            lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_14, 0);
        }

        if (sectionContent) {
            setTransparentColumnLayout(sectionContent, contentGapPx);
        }
        return sectionContent;
    }

    lv_obj_t* sectionTitle = lv_label_create(parent);
    lv_label_set_text(sectionTitle, title);
    lv_obj_set_style_text_color(sectionTitle, lv_color_hex(accentColor), 0);
    lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_14, 0);
    return parent;
}

void setTransparentRowLayout(lv_obj_t* container, int gapPx)
{
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_gap(container, gapPx, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
}

void presentationPlaceholderRender(lv_obj_t* parent, const char* text)
{
    lv_obj_clean(parent);

    lv_obj_t* placeholder = lv_label_create(parent);
    setPresentationWrappedLabelCommon(placeholder);
    setPresentationDetailLabelStyle(placeholder);
    lv_label_set_text(placeholder, text);
}

void presentationSectionRender(lv_obj_t* parent, const Api::FitnessPresentationSection& section)
{
    lv_obj_t* card = lv_obj_create(parent);
    setPresentationCardStyle(card, lv_color_hex(0x121A2A), lv_color_hex(0x344A6A));

    lv_obj_t* headerRow = lv_obj_create(card);
    setTransparentRowLayout(headerRow, 8);

    lv_obj_t* titleLabel = lv_label_create(headerRow);
    lv_obj_set_flex_grow(titleLabel, 1);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x99DDFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_14, 0);
    lv_label_set_text(titleLabel, section.label.c_str());

    const std::string scoreText = presentationSectionScoreTextBuild(section);
    if (!scoreText.empty()) {
        lv_obj_t* scoreLabel = lv_label_create(headerRow);
        lv_obj_set_style_text_color(scoreLabel, lv_color_hex(0xE8F0FF), 0);
        lv_obj_set_style_text_font(scoreLabel, LVGLBuilder::Style::CONTROL_FONT, 0);
        lv_label_set_text(scoreLabel, scoreText.c_str());
    }

    if (section.metrics.empty()) {
        lv_obj_t* emptyLabel = lv_label_create(card);
        setPresentationWrappedLabelCommon(emptyLabel);
        setPresentationDetailLabelStyle(emptyLabel);
        lv_label_set_text(emptyLabel, "No details.");
        return;
    }

    for (const auto& metric : section.metrics) {
        lv_obj_t* metricCard = lv_obj_create(card);
        setPresentationCardStyle(metricCard, lv_color_hex(0x0D1421), lv_color_hex(0x22324A));

        lv_obj_t* metricHeader = lv_obj_create(metricCard);
        setTransparentRowLayout(metricHeader, 8);

        lv_obj_t* metricLabel = lv_label_create(metricHeader);
        lv_obj_set_width(metricLabel, LV_PCT(60));
        lv_label_set_long_mode(metricLabel, LV_LABEL_LONG_WRAP);
        setPresentationMetricLabelStyle(metricLabel);
        lv_label_set_text(metricLabel, metric.label.c_str());

        lv_obj_t* valueLabel = lv_label_create(metricHeader);
        setPresentationMetricValueLabelStyle(valueLabel);
        lv_label_set_text(valueLabel, presentationValueTextBuild(metric).c_str());

        const std::string detailText = presentationMetricDetailTextBuild(metric);
        if (!detailText.empty()) {
            lv_obj_t* detailLabel = lv_label_create(metricCard);
            setPresentationWrappedLabelCommon(detailLabel);
            setPresentationDetailLabelStyle(detailLabel);
            lv_label_set_text(detailLabel, detailText.c_str());
        }
    }
}

} // namespace

std::vector<float> TrainingActiveView::buildDistributionSeries(
    const Api::EvolutionProgress& progress)
{
    std::vector<float> distribution;
    if (progress.lastGenerationFitnessHistogram.empty()) {
        return distribution;
    }

    uint64_t total = 0;
    for (const uint32_t count : progress.lastGenerationFitnessHistogram) {
        total += count;
    }
    if (total == 0) {
        return distribution;
    }

    distribution.reserve(progress.lastGenerationFitnessHistogram.size());
    for (const uint32_t count : progress.lastGenerationFitnessHistogram) {
        distribution.push_back(static_cast<float>(count) / static_cast<float>(total));
    }
    return distribution;
}

std::vector<float> TrainingActiveView::buildCpuCoreSeries(const Api::EvolutionProgress& progress)
{
    std::vector<float> cpuByCore;
    cpuByCore.reserve(progress.cpuPercentPerCore.size());
    for (const double cpuPercent : progress.cpuPercentPerCore) {
        cpuByCore.push_back(static_cast<float>(std::clamp(cpuPercent, 0.0, 100.0)));
    }
    return cpuByCore;
}

TrainingActiveView::TrainingActiveView(
    UiComponentManager* uiManager,
    EventSink& eventSink,
    Network::WebSocketServiceInterface* wsService,
    UserSettingsManager& userSettingsManager,
    UserSettings& userSettings,
    const Starfield::Snapshot* starfieldSnapshot)
    : uiManager_(uiManager),
      eventSink_(eventSink),
      wsService_(wsService),
      userSettingsManager_(userSettingsManager),
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
    constexpr int contentRowGapPx = 10;
    constexpr int fitnessPlotGapPx = 10;
    constexpr int fitnessPlotPanelMaxHeightPx = 190;
    constexpr int fitnessPlotPanelMinHeightPx = 130;
    constexpr int mainLayoutPaddingPx = 5;
    constexpr int mainLayoutGapPx = 8;
    constexpr int streamPanelWidthPx = 220;
    constexpr int longTermMinWidthPx = 160;
    constexpr int longTermPreferredWidthPx = 280;
    constexpr int centerMinWidthPx = 360;
    const int fitnessPlotPanelHeightPx =
        std::clamp(displayHeight / 3, fitnessPlotPanelMinHeightPx, fitnessPlotPanelMaxHeightPx);

    starfield_ =
        std::make_unique<Starfield>(container_, displayWidth, displayHeight, starfieldSnapshot_);
    renderer_ = std::make_unique<CellRenderer>();
    bestRenderer_ = std::make_unique<CellRenderer>();

    // Main layout: stream panel + stats/world content.
    contentRow_ = lv_obj_create(container_);
    lv_obj_set_size(contentRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(contentRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentRow_, 0, 0);
    lv_obj_set_style_pad_all(contentRow_, 0, 0);
    lv_obj_set_style_pad_gap(contentRow_, contentRowGapPx, 0);
    lv_obj_set_flex_flow(contentRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        contentRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(contentRow_, LV_OBJ_FLAG_SCROLLABLE);

    createStreamPanel(contentRow_);

    const int estimatedMainWidth =
        std::max(centerMinWidthPx + longTermMinWidthPx, displayWidth - streamPanelWidthPx - 20);
    int longTermMinWidth =
        std::clamp(estimatedMainWidth / 4, longTermMinWidthPx, longTermPreferredWidthPx);
    int centerColumnWidth =
        estimatedMainWidth - longTermMinWidth - mainLayoutGapPx - (mainLayoutPaddingPx * 2);
    if (centerColumnWidth < centerMinWidthPx) {
        centerColumnWidth = centerMinWidthPx;
        longTermMinWidth = std::max(
            longTermMinWidthPx,
            estimatedMainWidth - centerColumnWidth - mainLayoutGapPx - (mainLayoutPaddingPx * 2));
    }

    mainLayout_ = lv_obj_create(contentRow_);
    lv_obj_set_size(mainLayout_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(mainLayout_, 1);
    lv_obj_set_style_bg_opa(mainLayout_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mainLayout_, 0, 0);
    lv_obj_set_style_pad_all(mainLayout_, mainLayoutPaddingPx, 0);
    lv_obj_set_style_pad_gap(mainLayout_, mainLayoutGapPx, 0);
    lv_obj_set_flex_flow(mainLayout_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        mainLayout_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(mainLayout_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* centerLayout = lv_obj_create(mainLayout_);
    lv_obj_set_size(centerLayout, centerColumnWidth, LV_PCT(100));
    lv_obj_set_style_bg_opa(centerLayout, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(centerLayout, 0, 0);
    lv_obj_set_style_pad_all(centerLayout, 0, 0);
    lv_obj_set_style_pad_gap(centerLayout, 5, 0);
    lv_obj_set_flex_flow(centerLayout, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        centerLayout, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(centerLayout, LV_OBJ_FLAG_SCROLLABLE);

    longTermPanel_ = lv_obj_create(mainLayout_);
    lv_obj_set_size(longTermPanel_, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_grow(longTermPanel_, 1);
    lv_obj_set_style_min_width(longTermPanel_, longTermMinWidth, 0);
    lv_obj_set_style_bg_color(longTermPanel_, lv_color_hex(0x141420), 0);
    lv_obj_set_style_bg_opa(longTermPanel_, LV_OPA_90, 0);
    lv_obj_set_style_radius(longTermPanel_, 8, 0);
    lv_obj_set_style_border_width(longTermPanel_, 1, 0);
    lv_obj_set_style_border_color(longTermPanel_, lv_color_hex(0x2A2A44), 0);
    lv_obj_set_style_pad_all(longTermPanel_, 10, 0);
    lv_obj_set_style_pad_gap(longTermPanel_, 6, 0);
    lv_obj_set_flex_flow(longTermPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        longTermPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(longTermPanel_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(longTermPanel_, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* bestCommandsParent = createLongTermCollapsibleSection(
        longTermPanel_, "Best Command Histogram", 0xFFDD66, false, 0);

    bestCommandSummaryLabel_ = lv_label_create(bestCommandsParent);
    lv_obj_set_width(bestCommandSummaryLabel_, LV_PCT(100));
    lv_label_set_long_mode(bestCommandSummaryLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(bestCommandSummaryLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bestCommandSummaryLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_text(bestCommandSummaryLabel_, "No best snapshot yet.");

    lv_obj_t* bestFitnessPresentationParent = createLongTermCollapsibleSection(
        longTermPanel_, "Best Fitness Presentation", 0x99DDFF, true, 8);

    bestFitnessPresentationContent_ = lv_obj_create(bestFitnessPresentationParent);
    setTransparentColumnLayout(bestFitnessPresentationContent_, 8);
    renderBestFitnessPresentationPlaceholder("No best snapshot yet.");

    const int strategyColumnWidth = std::clamp(centerColumnWidth / 3, 280, 360);

    // ========== TOP: Stats panel (condensed) ==========
    statsPanel_ = lv_obj_create(centerLayout);
    lv_obj_set_size(statsPanel_, LV_PCT(100), LV_SIZE_CONTENT);
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

    lv_obj_t* headerRow = lv_obj_create(statsPanel_);
    setTransparentRowLayout(headerRow, 18);
    lv_obj_set_flex_flow(headerRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(headerRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* summaryColumn = lv_obj_create(headerRow);
    setTransparentColumnLayout(summaryColumn, 6);
    lv_obj_set_size(summaryColumn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(summaryColumn, 1);
    lv_obj_set_style_min_width(summaryColumn, 0, 0);

    lv_obj_t* strategyColumn = lv_obj_create(headerRow);
    setTransparentColumnLayout(strategyColumn, 4);
    lv_obj_set_size(strategyColumn, strategyColumnWidth, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(strategyColumn, lv_color_hex(0x121626), 0);
    lv_obj_set_style_bg_opa(strategyColumn, LV_OPA_50, 0);
    lv_obj_set_style_border_width(strategyColumn, 1, 0);
    lv_obj_set_style_border_color(strategyColumn, lv_color_hex(0x2A2A44), 0);
    lv_obj_set_style_pad_all(strategyColumn, 6, 0);

    // Title row: "EVOLUTION" + status.
    lv_obj_t* titleRow = lv_obj_create(summaryColumn);
    setTransparentRowLayout(titleRow, 12);

    lv_obj_t* title = lv_label_create(titleRow);
    lv_label_set_text(title, "EVOLUTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_pad_right(title, 12, 0);

    statusLabel_ = lv_label_create(titleRow);
    lv_label_set_text(statusLabel_, "Ready");
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x888888), 0);

    // Progress row.
    lv_obj_t* progressRow = lv_obj_create(summaryColumn);
    setTransparentRowLayout(progressRow, 10);
    lv_obj_set_flex_flow(progressRow, LV_FLEX_FLOW_ROW_WRAP);

    // Generation progress.
    genLabel_ = lv_label_create(progressRow);
    lv_label_set_text(genLabel_, "Gen 0/0");
    lv_obj_set_style_text_color(genLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(genLabel_, &lv_font_montserrat_12, 0);

    generationBar_ = lv_bar_create(progressRow);
    lv_obj_set_size(generationBar_, 100, 12);
    lv_bar_set_range(generationBar_, 0, 100);
    lv_bar_set_value(generationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x00AA66), LV_PART_INDICATOR);
    lv_obj_set_style_radius(generationBar_, 4, 0);
    lv_obj_set_style_radius(generationBar_, 4, LV_PART_INDICATOR);

    // Evaluation progress.
    evalLabel_ = lv_label_create(progressRow);
    lv_label_set_text(evalLabel_, "Eval 0");
    lv_obj_set_style_text_color(evalLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(evalLabel_, &lv_font_montserrat_12, 0);

    evaluationBar_ = lv_bar_create(progressRow);
    lv_obj_set_size(evaluationBar_, 100, 12);
    lv_bar_set_range(evaluationBar_, 0, 100);
    lv_bar_set_value(evaluationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x6688CC), LV_PART_INDICATOR);
    lv_obj_set_style_radius(evaluationBar_, 4, 0);
    lv_obj_set_style_radius(evaluationBar_, 4, LV_PART_INDICATOR);

    bestThisGenLabel_ = lv_label_create(progressRow);
    lv_label_set_text(bestThisGenLabel_, "Last: --");
    lv_obj_set_style_text_color(bestThisGenLabel_, lv_color_hex(0xAAAACC), 0);
    lv_obj_set_style_text_font(bestThisGenLabel_, &lv_font_montserrat_12, 0);

    bestAllTimeLabel_ = lv_label_create(progressRow);
    lv_label_set_text(bestAllTimeLabel_, "Best: --");
    lv_obj_set_style_text_color(bestAllTimeLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(bestAllTimeLabel_, &lv_font_montserrat_12, 0);

    // Time and runtime stats row.
    lv_obj_t* timeRow = lv_obj_create(summaryColumn);
    setTransparentRowLayout(timeRow, 10);
    lv_obj_set_flex_flow(timeRow, LV_FLEX_FLOW_ROW_WRAP);

    totalTimeLabel_ = lv_label_create(timeRow);
    lv_label_set_text(totalTimeLabel_, "Time 0.0s");
    lv_obj_set_style_text_color(totalTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(totalTimeLabel_, &lv_font_montserrat_12, 0);

    simTimeLabel_ = lv_label_create(timeRow);
    lv_label_set_text(simTimeLabel_, "Sim 0.0s");
    lv_obj_set_style_text_color(simTimeLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(simTimeLabel_, &lv_font_montserrat_12, 0);

    speedupLabel_ = lv_label_create(timeRow);
    lv_label_set_text(speedupLabel_, "Speed 0.0x");
    lv_obj_set_style_text_color(speedupLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(speedupLabel_, &lv_font_montserrat_12, 0);

    etaLabel_ = lv_label_create(timeRow);
    lv_label_set_text(etaLabel_, "ETA --");
    lv_obj_set_style_text_color(etaLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(etaLabel_, &lv_font_montserrat_12, 0);

    cpuLabel_ = lv_label_create(timeRow);
    lv_label_set_text(cpuLabel_, "CPU --");
    lv_obj_set_style_text_color(cpuLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(cpuLabel_, &lv_font_montserrat_12, 0);

    parallelismLabel_ = lv_label_create(timeRow);
    lv_label_set_text(parallelismLabel_, "Par --");
    lv_obj_set_style_text_color(parallelismLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(parallelismLabel_, &lv_font_montserrat_12, 0);

    genomeCountLabel_ = lv_label_create(timeRow);
    lv_label_set_text(genomeCountLabel_, "Genomes --");
    lv_obj_set_style_text_color(genomeCountLabel_, lv_color_hex(0x88AACC), 0);
    lv_obj_set_style_text_font(genomeCountLabel_, &lv_font_montserrat_12, 0);

    lv_obj_t* adaptationRow = lv_obj_create(strategyColumn);
    setTransparentRowLayout(adaptationRow, 12);
    lv_obj_set_flex_flow(adaptationRow, LV_FLEX_FLOW_ROW_WRAP);

    adaptationPhaseLabel_ = lv_label_create(adaptationRow);
    lv_label_set_text(adaptationPhaseLabel_, "Phase: Normal");
    setCompactStatsBadgeStyle(adaptationPhaseLabel_, trainingPhaseTextColor(TrainingPhase::Normal));

    adaptationMutationModeLabel_ = lv_label_create(adaptationRow);
    lv_label_set_text(adaptationMutationModeLabel_, "Mut: Baseline");
    setCompactStatsBadgeStyle(
        adaptationMutationModeLabel_,
        adaptiveMutationModeTextColor(AdaptiveMutationMode::Baseline));

    lv_obj_t* adaptationSummaryRow = lv_obj_create(strategyColumn);
    setTransparentRowLayout(adaptationSummaryRow, 12);
    lv_obj_set_flex_flow(adaptationSummaryRow, LV_FLEX_FLOW_ROW_WRAP);

    adaptationSinceImprovementLabel_ = lv_label_create(adaptationSummaryRow);
    lv_label_set_text(adaptationSinceImprovementLabel_, "Since: 0");
    setCompactStatsLabelStyle(adaptationSinceImprovementLabel_, lv_color_hex(0x88AACC));

    adaptationResolvedLabel_ = lv_label_create(adaptationSummaryRow);
    lv_label_set_text(adaptationResolvedLabel_, "Resolved: --");
    setCompactStatsLabelStyle(adaptationResolvedLabel_, lv_color_hex(0xCCCCCC));

    lv_obj_t* adaptationActualRow = lv_obj_create(strategyColumn);
    setTransparentRowLayout(adaptationActualRow, 12);
    lv_obj_set_flex_flow(adaptationActualRow, LV_FLEX_FLOW_ROW_WRAP);

    adaptationActualWeightChangesLabel_ = lv_label_create(adaptationActualRow);
    lv_label_set_text(adaptationActualWeightChangesLabel_, "Actual dW: --");
    setCompactStatsLabelStyle(adaptationActualWeightChangesLabel_, lv_color_hex(0xCCCCCC));

    adaptationActualResetAvgLabel_ = lv_label_create(adaptationActualRow);
    lv_label_set_text(adaptationActualResetAvgLabel_, "Reset: --");
    setCompactStatsLabelStyle(adaptationActualResetAvgLabel_, lv_color_hex(0x88AACC));

    lv_obj_t* adaptationDetailRow = lv_obj_create(strategyColumn);
    setTransparentRowLayout(adaptationDetailRow, 12);
    lv_obj_set_flex_flow(adaptationDetailRow, LV_FLEX_FLOW_ROW_WRAP);

    adaptationLastImprovementLabel_ = lv_label_create(adaptationDetailRow);
    lv_label_set_text(adaptationLastImprovementLabel_, "Last: --");
    setCompactStatsLabelStyle(adaptationLastImprovementLabel_, lv_color_hex(0x667799));

    adaptationStagnationLabel_ = lv_label_create(adaptationDetailRow);
    lv_label_set_text(adaptationStagnationLabel_, "S: 0");
    setCompactStatsLabelStyle(adaptationStagnationLabel_, lv_color_hex(0x667799));

    adaptationRecoveryLabel_ = lv_label_create(adaptationDetailRow);
    lv_label_set_text(adaptationRecoveryLabel_, "R: 0");
    setCompactStatsLabelStyle(adaptationRecoveryLabel_, lv_color_hex(0x667799));

    constexpr int worldColumnGapPx = 10;
    const int worldColumnWidth = std::max(160, (centerColumnWidth - worldColumnGapPx) / 2);
    const int worldContainerSize = std::max(145, worldColumnWidth - 10);

    // ========== BOTTOM: Two world views side by side ==========
    bottomRow_ = lv_obj_create(centerLayout);
    lv_obj_set_size(bottomRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottomRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow_, 0, 0);
    lv_obj_set_style_pad_all(bottomRow_, 0, 0);
    lv_obj_set_style_pad_gap(bottomRow_, worldColumnGapPx, 0);
    lv_obj_set_flex_flow(bottomRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        bottomRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(bottomRow_, LV_OBJ_FLAG_SCROLLABLE);

    // Left panel: Live feed.
    lv_obj_t* leftPanel = lv_obj_create(bottomRow_);
    lv_obj_set_size(leftPanel, worldColumnWidth, LV_SIZE_CONTENT);
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
    lv_obj_set_size(worldContainer_, worldContainerSize, worldContainerSize);
    lv_obj_set_style_bg_color(worldContainer_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(worldContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(worldContainer_, 8, 0);
    lv_obj_set_style_border_width(worldContainer_, 2, 0);
    lv_obj_set_style_border_color(worldContainer_, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(worldContainer_, 5, 0);
    lv_obj_clear_flag(worldContainer_, LV_OBJ_FLAG_SCROLLABLE);

    renderer_->initialize(worldContainer_, 9, 9);
    createNesVideoOverlay(worldContainer_, liveNesOverlayWidgets_);

    // Right panel: Best snapshot.
    lv_obj_t* rightPanel = lv_obj_create(bottomRow_);
    lv_obj_set_size(rightPanel, worldColumnWidth, LV_SIZE_CONTENT);
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
    lv_obj_set_size(bestWorldContainer_, worldContainerSize, worldContainerSize);
    lv_obj_set_style_bg_color(bestWorldContainer_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(bestWorldContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bestWorldContainer_, 8, 0);
    lv_obj_set_style_border_width(bestWorldContainer_, 2, 0);
    lv_obj_set_style_border_color(bestWorldContainer_, lv_color_hex(0x3A3A5A), 0);
    lv_obj_set_style_pad_all(bestWorldContainer_, 5, 0);
    lv_obj_clear_flag(bestWorldContainer_, LV_OBJ_FLAG_SCROLLABLE);

    bestRenderer_->initialize(bestWorldContainer_, 9, 9);
    createNesVideoOverlay(bestWorldContainer_, bestNesOverlayWidgets_);

    fitnessPlotsPanel_ = lv_obj_create(centerLayout);
    lv_obj_set_size(fitnessPlotsPanel_, LV_PCT(100), fitnessPlotPanelHeightPx);
    lv_obj_set_style_bg_opa(fitnessPlotsPanel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fitnessPlotsPanel_, 0, 0);
    lv_obj_set_style_pad_all(fitnessPlotsPanel_, 0, 0);
    lv_obj_set_style_pad_gap(fitnessPlotsPanel_, 6, 0);
    lv_obj_set_flex_flow(fitnessPlotsPanel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        fitnessPlotsPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(fitnessPlotsPanel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* fitnessTitle = lv_label_create(fitnessPlotsPanel_);
    lv_label_set_text(fitnessTitle, "Fitness Insights");
    lv_obj_set_style_text_color(fitnessTitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(fitnessTitle, &lv_font_montserrat_12, 0);

    fitnessPlotsRow_ = lv_obj_create(fitnessPlotsPanel_);
    lv_obj_set_size(fitnessPlotsRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(fitnessPlotsRow_, 1);
    lv_obj_set_style_bg_opa(fitnessPlotsRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fitnessPlotsRow_, 0, 0);
    lv_obj_set_style_pad_all(fitnessPlotsRow_, 0, 0);
    lv_obj_set_style_pad_gap(fitnessPlotsRow_, fitnessPlotGapPx, 0);
    lv_obj_set_flex_flow(fitnessPlotsRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        fitnessPlotsRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(fitnessPlotsRow_, LV_OBJ_FLAG_SCROLLABLE);

    lastGenerationDistributionPlot_ = std::make_unique<TimeSeriesPlotWidget>(
        fitnessPlotsRow_,
        TimeSeriesPlotWidget::Config{
            .title = "Last Gen Distribution",
            .lineColor = lv_color_hex(0x66BBFF),
            .defaultMinY = 0.0f,
            .defaultMaxY = 1.0f,
            .valueScale = 100.0f,
            .autoScaleY = false,
            .hideZeroValuePoints = true,
            .chartType = LV_CHART_TYPE_BAR,
            .minPointCount = 1,
        });

    bestFitnessPlot_ = std::make_unique<TimeSeriesPlotWidget>(
        fitnessPlotsRow_,
        TimeSeriesPlotWidget::Config{
            .title = "Fitness",
            .lineColor = lv_color_hex(0x666666),
            .secondaryLineColor = lv_color_hex(0x66BBFF),
            .highlightColor = lv_color_hex(0xFF4FA3),
            .defaultMinY = 0.0f,
            .defaultMaxY = 1.0f,
            .valueScale = 100.0f,
            .autoScaleY = true,
            .autoScaleClampToZero = true,
            .showSecondarySeries = true,
            .showHighlights = true,
            .highlightMarkerSizePx = 8,
        });

    clearFitnessPlots();
    setNesControllerOverlayEnabled(isNesControllerOverlayEnabled(userSettings_));

    LOG_INFO(
        Controls, "Training active UI created with live feed, best snapshot, and long-term panel");
}

void TrainingActiveView::createNesVideoOverlay(
    lv_obj_t* parent, TrainingActiveView::NesOverlayWidgets& overlay)
{
    overlay = {};

    overlay.container = lv_obj_create(parent);
    lv_obj_set_width(overlay.container, LV_PCT(92));
    lv_obj_set_height(overlay.container, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(overlay.container, lv_color_hex(0x08101A), 0);
    lv_obj_set_style_bg_opa(overlay.container, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay.container, 1, 0);
    lv_obj_set_style_border_color(overlay.container, lv_color_hex(0x2A3B52), 0);
    lv_obj_set_style_radius(overlay.container, 6, 0);
    lv_obj_set_style_pad_all(overlay.container, 5, 0);
    lv_obj_set_style_pad_gap(overlay.container, 4, 0);
    lv_obj_set_flex_flow(overlay.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        overlay.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(overlay.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(overlay.container, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_add_flag(overlay.container, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* headerRow = lv_obj_create(overlay.container);
    setTransparentRowLayout(headerRow, 4);

    overlay.sourceBadge = lv_label_create(headerRow);
    lv_obj_set_style_pad_hor(overlay.sourceBadge, 5, 0);
    lv_obj_set_style_pad_ver(overlay.sourceBadge, 1, 0);
    lv_obj_set_style_radius(overlay.sourceBadge, 4, 0);
    lv_obj_set_style_text_font(overlay.sourceBadge, &lv_font_montserrat_12, 0);
    lv_label_set_text(overlay.sourceBadge, "POLICY");

    overlay.sourceFrameLabel = lv_label_create(headerRow);
    lv_obj_set_flex_grow(overlay.sourceFrameLabel, 1);
    lv_obj_set_style_text_align(overlay.sourceFrameLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(overlay.sourceFrameLabel, lv_color_hex(0xA5B5C9), 0);
    lv_obj_set_style_text_font(overlay.sourceFrameLabel, &lv_font_montserrat_12, 0);
    lv_label_set_text(overlay.sourceFrameLabel, "--");

    for (size_t i = 0; i < kNesOutputLabels.size(); ++i) {
        lv_obj_t* row = lv_obj_create(overlay.container);
        setTransparentRowLayout(row, 4);

        lv_obj_t* label = lv_label_create(row);
        lv_obj_set_width(label, 12);
        lv_obj_set_style_text_color(label, lv_color_hex(0xD8E4F5), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_label_set_text(label, kNesOutputLabels[i]);

        lv_obj_t* track = lv_obj_create(row);
        lv_obj_set_height(track, 12);
        lv_obj_set_flex_grow(track, 1);
        lv_obj_set_style_bg_color(track, lv_color_hex(0x132030), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(track, 1, 0);
        lv_obj_set_style_border_color(track, lv_color_hex(0x22364D), 0);
        lv_obj_set_style_radius(track, 4, 0);
        lv_obj_set_style_pad_all(track, 1, 0);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* centerLine = lv_obj_create(track);
        lv_obj_set_size(centerLine, 2, LV_PCT(100));
        lv_obj_set_style_bg_color(centerLine, lv_color_hex(0x72839B), 0);
        lv_obj_set_style_bg_opa(centerLine, LV_OPA_70, 0);
        lv_obj_set_style_border_width(centerLine, 0, 0);
        lv_obj_set_style_radius(centerLine, 0, 0);
        lv_obj_align(centerLine, LV_ALIGN_CENTER, 0, 0);

        overlay.outputFills[i] = lv_obj_create(track);
        lv_obj_set_size(overlay.outputFills[i], 1, 8);
        lv_obj_set_style_border_width(overlay.outputFills[i], 0, 0);
        lv_obj_set_style_radius(overlay.outputFills[i], 3, 0);
        lv_obj_align(overlay.outputFills[i], LV_ALIGN_LEFT_MID, lv_obj_get_width(track) / 2, 0);

        overlay.outputValueLabels[i] = lv_label_create(row);
        lv_obj_set_width(overlay.outputValueLabels[i], 34);
        lv_obj_set_style_text_align(overlay.outputValueLabels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(overlay.outputValueLabels[i], lv_color_hex(0xA5B5C9), 0);
        lv_obj_set_style_text_font(overlay.outputValueLabels[i], &lv_font_montserrat_12, 0);
        lv_label_set_text(overlay.outputValueLabels[i], "0.00");
    }

    lv_obj_t* buttons = lv_obj_create(overlay.container);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttons, 0, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_pad_row(buttons, 3, 0);
    lv_obj_set_style_pad_column(buttons, 3, 0);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < kNesButtonLabels.size(); ++i) {
        overlay.buttonChips[i] = lv_label_create(buttons);
        lv_obj_set_size(overlay.buttonChips[i], 24, 18);
        lv_obj_set_style_text_align(overlay.buttonChips[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(overlay.buttonChips[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_top(overlay.buttonChips[i], 2, 0);
        lv_obj_set_style_radius(overlay.buttonChips[i], 4, 0);
        lv_label_set_text(overlay.buttonChips[i], kNesButtonLabels[i]);
    }
}

void TrainingActiveView::updateNesVideoOverlay(
    TrainingActiveView::NesOverlayWidgets& overlay,
    const std::optional<NesControllerTelemetry>& telemetry,
    bool videoVisible)
{
    if (!overlay.container) {
        return;
    }

    if (!videoVisible || !isNesControllerOverlayEnabled(userSettings_) || !telemetry.has_value()) {
        lv_obj_add_flag(overlay.container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const NesControllerTelemetry& value = telemetry.value();
    lv_obj_clear_flag(overlay.container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay.container);

    const std::string sourceLabel = nesControllerSourceLabelBuild(value.controllerSource);
    lv_label_set_text(overlay.sourceBadge, sourceLabel.c_str());
    if (value.controllerSource == NesGameAdapterControllerSource::ScriptedSetup) {
        lv_obj_set_style_bg_color(overlay.sourceBadge, lv_color_hex(0x3F2032), 0);
        lv_obj_set_style_bg_opa(overlay.sourceBadge, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(overlay.sourceBadge, lv_color_hex(0xFFD6E6), 0);
    }
    else if (value.controllerSource == NesGameAdapterControllerSource::LiveInput) {
        lv_obj_set_style_bg_color(overlay.sourceBadge, lv_color_hex(0x1B2747), 0);
        lv_obj_set_style_bg_opa(overlay.sourceBadge, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(overlay.sourceBadge, lv_color_hex(0xBFD8FF), 0);
    }
    else {
        lv_obj_set_style_bg_color(overlay.sourceBadge, lv_color_hex(0x173629), 0);
        lv_obj_set_style_bg_opa(overlay.sourceBadge, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(overlay.sourceBadge, lv_color_hex(0xA8FFD4), 0);
    }

    if (value.controllerSourceFrameIndex.has_value()) {
        lv_label_set_text_fmt(
            overlay.sourceFrameLabel,
            "F%llu",
            static_cast<unsigned long long>(value.controllerSourceFrameIndex.value()));
    }
    else {
        lv_label_set_text(overlay.sourceFrameLabel, "--");
    }

    const std::array<float, 4> rawOutputs{ value.xRaw, value.yRaw, value.aRaw, value.bRaw };
    for (size_t i = 0; i < rawOutputs.size(); ++i) {
        if (!overlay.outputFills[i] || !overlay.outputValueLabels[i]) {
            continue;
        }

        lv_label_set_text(
            overlay.outputValueLabels[i], nesOutputValueTextBuild(rawOutputs[i]).c_str());

        lv_obj_t* track = lv_obj_get_parent(overlay.outputFills[i]);
        lv_obj_update_layout(track);
        const int trackWidth = std::max(4, lv_obj_get_width(track) - 2);
        const int trackHeight = std::max(4, lv_obj_get_height(track) - 2);
        const int halfWidth = std::max(1, (trackWidth / 2) - 1);
        const float normalized = normalizeNesOverlayOutput(rawOutputs[i]);
        const int fillWidth =
            std::max(1, static_cast<int>(std::round(std::abs(normalized) * halfWidth)));
        const int fillX = normalized >= 0.0f ? (trackWidth / 2) : ((trackWidth / 2) - fillWidth);

        lv_obj_set_size(overlay.outputFills[i], fillWidth, std::max(4, trackHeight - 2));
        lv_obj_align(overlay.outputFills[i], LV_ALIGN_LEFT_MID, fillX, 0);
        lv_obj_set_style_bg_color(
            overlay.outputFills[i],
            normalized >= 0.0f ? lv_color_hex(0xFF9A62) : lv_color_hex(0x66B8FF),
            0);
        lv_obj_set_style_bg_opa(overlay.outputFills[i], LV_OPA_COVER, 0);
    }

    for (size_t i = 0; i < overlay.buttonChips.size(); ++i) {
        const uint8_t bit = kNesButtonMasks[i];
        updateNesVideoOverlayButton(
            overlay.buttonChips[i],
            (value.inferredControllerMask & bit) != 0,
            (value.resolvedControllerMask & bit) != 0,
            i >= 4);
    }
}

void TrainingActiveView::updateNesVideoOverlayButton(
    lv_obj_t* chip, bool inferredPressed, bool resolvedPressed, bool actionButton)
{
    if (!chip) {
        return;
    }

    if (resolvedPressed) {
        lv_obj_set_style_bg_color(
            chip, actionButton ? lv_color_hex(0xFFB359) : lv_color_hex(0x78D5FF), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_text_color(chip, lv_color_hex(0x08101A), 0);
        return;
    }

    if (inferredPressed) {
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x0B1624), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_border_color(
            chip, actionButton ? lv_color_hex(0xFF9A62) : lv_color_hex(0x66B8FF), 0);
        lv_obj_set_style_text_color(
            chip, actionButton ? lv_color_hex(0xFFD0B2) : lv_color_hex(0xC5EBFF), 0);
        return;
    }

    lv_obj_set_style_bg_color(chip, lv_color_hex(0x0B1624), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x22364D), 0);
    lv_obj_set_style_text_color(chip, lv_color_hex(0x637389), 0);
}

void TrainingActiveView::destroyUI()
{
    if (renderer_) {
        renderer_->cleanup();
    }
    if (bestRenderer_) {
        bestRenderer_->cleanup();
    }
    cpuCorePlot_.reset();
    bestFitnessPlot_.reset();
    lastGenerationDistributionPlot_.reset();
    scenarioControls_.reset();

    videoSurface_.reset();
    bestVideoSurface_.reset();
    bestVideoFrame_.reset();
    starfield_.reset();
    if (container_) {
        lv_obj_clean(container_);
    }

    adaptationActualResetAvgLabel_ = nullptr;
    adaptationActualWeightChangesLabel_ = nullptr;
    adaptationLastImprovementLabel_ = nullptr;
    adaptationMutationModeLabel_ = nullptr;
    adaptationPhaseLabel_ = nullptr;
    adaptationRecoveryLabel_ = nullptr;
    adaptationResolvedLabel_ = nullptr;
    adaptationSinceImprovementLabel_ = nullptr;
    adaptationStagnationLabel_ = nullptr;
    bestAllTimeLabel_ = nullptr;
    bestFitnessLabel_ = nullptr;
    bestCommandSummaryLabel_ = nullptr;
    bestFitnessPresentationContent_ = nullptr;
    bestThisGenLabel_ = nullptr;
    bestWorldContainer_ = nullptr;
    container_ = nullptr;
    cpuLabel_ = nullptr;
    etaLabel_ = nullptr;
    evalLabel_ = nullptr;
    evaluationBar_ = nullptr;
    fitnessPlotsPanel_ = nullptr;
    fitnessPlotsRow_ = nullptr;
    genLabel_ = nullptr;
    genomeCountLabel_ = nullptr;
    generationBar_ = nullptr;
    statsPanel_ = nullptr;
    bottomRow_ = nullptr;
    contentRow_ = nullptr;
    mainLayout_ = nullptr;
    longTermPanel_ = nullptr;
    parallelismLabel_ = nullptr;
    streamPanel_ = nullptr;
    streamIntervalStepper_ = nullptr;
    bestPlaybackToggle_ = nullptr;
    bestPlaybackIntervalStepper_ = nullptr;
    nesControllerOverlayToggle_ = nullptr;
    pauseResumeButton_ = nullptr;
    pauseResumeLabel_ = nullptr;
    scenarioControlsButton_ = nullptr;
    scenarioControlsOverlay_ = nullptr;
    scenarioControlsOverlayTitle_ = nullptr;
    scenarioControlsOverlayContent_ = nullptr;
    stopTrainingButton_ = nullptr;
    simTimeLabel_ = nullptr;
    speedupLabel_ = nullptr;
    statusLabel_ = nullptr;
    totalTimeLabel_ = nullptr;
    worldContainer_ = nullptr;
    liveNesOverlayWidgets_ = {};
    bestNesOverlayWidgets_ = {};
    liveNesControllerTelemetry_.reset();
    bestNesControllerTelemetry_.reset();
    currentScenarioConfig_ = Config::Empty{};
    currentScenarioId_ = Scenario::EnumType::Empty;
    scenarioControlsScenarioId_ = Scenario::EnumType::Empty;
    hasScenarioState_ = false;
    scenarioControlsOverlayVisible_ = false;
}

void TrainingActiveView::renderWorld(
    const WorldData& worldData,
    const std::optional<ScenarioVideoFrame>& scenarioVideoFrame,
    const std::optional<NesControllerTelemetry>& nesControllerTelemetry)
{
    if (!worldContainer_) {
        return;
    }

    liveNesControllerTelemetry_ = nesControllerTelemetry;
    if (scenarioVideoFrame.has_value()) {
        if (!videoSurface_) {
            if (renderer_) {
                renderer_->cleanup();
            }
            videoSurface_ = std::make_unique<VideoSurface>();
        }
        videoSurface_->present(scenarioVideoFrame.value(), worldContainer_);
        updateNesVideoOverlay(liveNesOverlayWidgets_, liveNesControllerTelemetry_, true);
        return;
    }

    liveNesControllerTelemetry_.reset();
    updateNesVideoOverlay(liveNesOverlayWidgets_, liveNesControllerTelemetry_, false);
    if (videoSurface_) {
        videoSurface_.reset();
    }

    if (!renderer_) {
        return;
    }
    renderer_->renderWorldData(worldData, worldContainer_, false, RenderMode::SHARP);
}

void TrainingActiveView::updateBestSnapshot(
    const WorldData& worldData,
    double fitness,
    int generation,
    int commandsAccepted,
    int commandsRejected,
    const std::vector<std::pair<std::string, int>>& topCommandSignatures,
    const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures,
    const Api::FitnessPresentation& fitnessPresentation,
    const std::optional<ScenarioVideoFrame>& scenarioVideoFrame)
{
    bestSnapshotWorldData_ = std::make_unique<WorldData>(worldData);
    bestSnapshotFitness_ = fitness;
    bestSnapshotGeneration_ = generation;
    hasBestSnapshot_ = true;
    if (scenarioVideoFrame.has_value()) {
        bestVideoFrame_ = scenarioVideoFrame;
    }
    else {
        bestVideoFrame_.reset();
    }
    if (!userSettings_.uiTraining.bestPlaybackEnabled) {
        bestNesControllerTelemetry_.reset();
    }
    if (!userSettings_.uiTraining.bestPlaybackEnabled) {
        bestWorldData_ = std::make_unique<WorldData>(worldData);
        bestFitness_ = fitness;
        bestGeneration_ = generation;
    }
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
    if (bestCommandSummaryLabel_) {
        std::ostringstream summary;
        const int totalOutcomes = commandsAccepted + commandsRejected;
        double acceptedRatio = 0.0;
        if (totalOutcomes > 0) {
            acceptedRatio = (100.0 * static_cast<double>(commandsAccepted))
                / static_cast<double>(totalOutcomes);
        }
        summary << "Accepted: " << commandsAccepted << "\n";
        summary << "Rejected: " << commandsRejected << "\n";
        summary << "Acceptance: " << std::fixed << std::setprecision(1) << acceptedRatio << "%\n";

        const auto commandTypeFromSignature = [](const std::string& signature) -> std::string {
            constexpr std::string_view delimiter = " -> ";
            const std::string_view signatureView{ signature };
            const size_t outcomePos = signatureView.find(delimiter);
            const std::string_view commandView = outcomePos == std::string_view::npos
                ? signatureView
                : signatureView.substr(0, outcomePos);
            const size_t parenPos = commandView.find('(');
            return std::string(
                parenPos == std::string_view::npos ? commandView : commandView.substr(0, parenPos));
        };

        std::unordered_map<std::string, int> histogramByType;
        histogramByType.reserve(topCommandSignatures.size());
        for (const auto& [signature, count] : topCommandSignatures) {
            if (count <= 0) {
                continue;
            }
            histogramByType[commandTypeFromSignature(signature)] += count;
        }

        std::vector<std::pair<std::string, int>> histogram;
        histogram.reserve(histogramByType.size());
        for (const auto& [commandType, count] : histogramByType) {
            histogram.emplace_back(commandType, count);
        }
        std::sort(histogram.begin(), histogram.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });

        summary << "\nCommand Histogram:\n";
        if (histogram.empty()) {
            summary << "(none)";
        }
        else {
            constexpr size_t histogramLimit = 10;
            const size_t limit = std::min(histogram.size(), histogramLimit);
            for (size_t i = 0; i < limit; ++i) {
                summary << (i + 1) << ". " << histogram[i].first << " x" << histogram[i].second;
                if (i + 1 < limit) {
                    summary << "\n";
                }
            }
        }

        summary << "\n\nTop Command Signatures:\n";
        if (topCommandSignatures.empty()) {
            summary << "(none)";
        }
        else {
            size_t rank = 1;
            for (const auto& [signature, count] : topCommandSignatures) {
                summary << rank << ". " << signature << " x" << count;
                if (rank < topCommandSignatures.size()) {
                    summary << "\n";
                }
                rank++;
            }
        }
        summary << "\n\nTop Outcome Signatures:\n";
        if (topCommandOutcomeSignatures.empty()) {
            summary << "(none)";
        }
        else {
            size_t rank = 1;
            for (const auto& [signature, count] : topCommandOutcomeSignatures) {
                summary << rank << ". " << signature << " x" << count;
                if (rank < topCommandOutcomeSignatures.size()) {
                    summary << "\n";
                }
                rank++;
            }
        }
        lv_label_set_text(bestCommandSummaryLabel_, summary.str().c_str());
    }
    renderBestFitnessPresentation(fitnessPresentation);
    if (!userSettings_.uiTraining.bestPlaybackEnabled) {
        scheduleBestRender();
    }
}

void TrainingActiveView::renderBestFitnessPresentation(
    const Api::FitnessPresentation& fitnessPresentation)
{
    if (!bestFitnessPresentationContent_) {
        return;
    }

    lv_obj_clean(bestFitnessPresentationContent_);

    lv_obj_t* headerCard = lv_obj_create(bestFitnessPresentationContent_);
    setPresentationCardStyle(headerCard, lv_color_hex(0x162236), lv_color_hex(0x35557A));

    lv_obj_t* titleRow = lv_obj_create(headerCard);
    setTransparentRowLayout(titleRow, 8);

    lv_obj_t* modelLabel = lv_label_create(titleRow);
    lv_obj_set_flex_grow(modelLabel, 1);
    lv_obj_set_style_text_color(modelLabel, lv_color_hex(0xDCE6FF), 0);
    lv_obj_set_style_text_font(modelLabel, &lv_font_montserrat_14, 0);
    lv_label_set_text_fmt(modelLabel, "Model: %s", fitnessPresentation.modelId.c_str());

    lv_obj_t* totalLabel = lv_label_create(titleRow);
    lv_obj_set_style_text_color(totalLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(totalLabel, &lv_font_montserrat_14, 0);
    lv_label_set_text_fmt(totalLabel, "Total %.4f", fitnessPresentation.totalFitness);

    if (!fitnessPresentation.summary.empty()) {
        lv_obj_t* summaryLabel = lv_label_create(headerCard);
        setPresentationWrappedLabelCommon(summaryLabel);
        setPresentationDetailLabelStyle(summaryLabel);
        lv_label_set_text(summaryLabel, fitnessPresentation.summary.c_str());
    }

    if (fitnessPresentation.sections.empty()) {
        lv_obj_t* emptyLabel = lv_label_create(headerCard);
        setPresentationWrappedLabelCommon(emptyLabel);
        setPresentationDetailLabelStyle(emptyLabel);
        lv_label_set_text(emptyLabel, "No presentation sections.");
        return;
    }

    for (const auto& section : fitnessPresentation.sections) {
        presentationSectionRender(bestFitnessPresentationContent_, section);
    }
}

void TrainingActiveView::renderBestFitnessPresentationPlaceholder(const char* text)
{
    if (!bestFitnessPresentationContent_) {
        return;
    }

    presentationPlaceholderRender(bestFitnessPresentationContent_, text);
}

void TrainingActiveView::setStreamIntervalMs(int value)
{
    userSettings_.uiTraining.streamIntervalMs = value;
    if (streamIntervalStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(streamIntervalStepper_, value);
    }
}

void TrainingActiveView::setBestPlaybackEnabled(bool enabled)
{
    userSettings_.uiTraining.bestPlaybackEnabled = enabled;
    if (bestPlaybackToggle_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(bestPlaybackToggle_, enabled);
    }
    if (bestPlaybackIntervalStepper_) {
        if (enabled) {
            lv_obj_clear_state(bestPlaybackIntervalStepper_, LV_STATE_DISABLED);
            lv_obj_set_style_opa(bestPlaybackIntervalStepper_, LV_OPA_COVER, 0);
        }
        else {
            lv_obj_add_state(bestPlaybackIntervalStepper_, LV_STATE_DISABLED);
            lv_obj_set_style_opa(bestPlaybackIntervalStepper_, LV_OPA_50, 0);
        }
    }

    if (!enabled) {
        bestNesControllerTelemetry_.reset();
        updateNesVideoOverlay(bestNesOverlayWidgets_, bestNesControllerTelemetry_, false);
    }

    if (!enabled && hasBestSnapshot_ && bestSnapshotWorldData_) {
        bestWorldData_ = std::make_unique<WorldData>(*bestSnapshotWorldData_);
        bestFitness_ = bestSnapshotFitness_;
        bestGeneration_ = bestSnapshotGeneration_;
        scheduleBestRender();
    }
}

void TrainingActiveView::setBestPlaybackIntervalMs(int value)
{
    userSettings_.uiTraining.bestPlaybackIntervalMs = std::max(1, value);
    if (bestPlaybackIntervalStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            bestPlaybackIntervalStepper_, userSettings_.uiTraining.bestPlaybackIntervalMs);
    }
}

void TrainingActiveView::setNesControllerOverlayEnabled(bool enabled)
{
    userSettings_.uiTraining.nesControllerOverlayEnabled = enabled;
    if (nesControllerOverlayToggle_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(nesControllerOverlayToggle_, enabled);
    }

    updateNesVideoOverlay(
        liveNesOverlayWidgets_, liveNesControllerTelemetry_, videoSurface_ != nullptr);
    updateNesVideoOverlay(
        bestNesOverlayWidgets_, bestNesControllerTelemetry_, bestVideoFrame_.has_value());
}

void TrainingActiveView::updateScenarioConfig(
    Scenario::EnumType scenarioId, const ScenarioConfig& config)
{
    currentScenarioId_ = scenarioId;
    currentScenarioConfig_ = config;
    hasScenarioState_ = true;

    if (scenarioControlsOverlayVisible_) {
        refreshScenarioControlsOverlay();
    }
    else {
        updateScenarioButtonState();
    }
}

void TrainingActiveView::showScenarioControlsOverlay()
{
    if (!hasScenarioState_) {
        updateScenarioButtonState();
        return;
    }

    scenarioControlsOverlayVisible_ = true;
    refreshScenarioControlsOverlay();
}

void TrainingActiveView::createScenarioControlsOverlay()
{
    if (!contentRow_ || scenarioControlsOverlay_) {
        return;
    }

    scenarioControlsOverlay_ = lv_obj_create(contentRow_);
    lv_obj_add_flag(scenarioControlsOverlay_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(scenarioControlsOverlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(scenarioControlsOverlay_, 320, 420);
    lv_obj_set_style_bg_color(scenarioControlsOverlay_, lv_color_hex(0x111728), 0);
    lv_obj_set_style_bg_opa(scenarioControlsOverlay_, LV_OPA_90, 0);
    lv_obj_set_style_radius(scenarioControlsOverlay_, 8, 0);
    lv_obj_set_style_border_width(scenarioControlsOverlay_, 1, 0);
    lv_obj_set_style_border_color(scenarioControlsOverlay_, lv_color_hex(0x4A5A80), 0);
    lv_obj_set_style_pad_all(scenarioControlsOverlay_, 10, 0);
    lv_obj_set_style_pad_row(scenarioControlsOverlay_, 8, 0);
    lv_obj_set_flex_flow(scenarioControlsOverlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        scenarioControlsOverlay_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(scenarioControlsOverlay_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scenarioControlsOverlay_, LV_OBJ_FLAG_SCROLLABLE);

    scenarioControlsOverlayTitle_ = lv_label_create(scenarioControlsOverlay_);
    lv_label_set_text(scenarioControlsOverlayTitle_, "Scenario Controls");
    lv_obj_set_style_text_color(scenarioControlsOverlayTitle_, lv_color_hex(0xDCE6FF), 0);
    lv_obj_set_style_text_font(scenarioControlsOverlayTitle_, &lv_font_montserrat_14, 0);

    scenarioControlsOverlayContent_ = lv_obj_create(scenarioControlsOverlay_);
    lv_obj_set_size(scenarioControlsOverlayContent_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(scenarioControlsOverlayContent_, 1);
    lv_obj_set_style_bg_opa(scenarioControlsOverlayContent_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scenarioControlsOverlayContent_, 0, 0);
    lv_obj_set_style_pad_all(scenarioControlsOverlayContent_, 0, 0);
    lv_obj_set_style_pad_row(scenarioControlsOverlayContent_, 8, 0);
    lv_obj_set_flex_flow(scenarioControlsOverlayContent_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        scenarioControlsOverlayContent_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scenarioControlsOverlayContent_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scenarioControlsOverlayContent_, LV_SCROLLBAR_MODE_AUTO);
}

void TrainingActiveView::hideScenarioControlsOverlay()
{
    scenarioControlsOverlayVisible_ = false;
    if (scenarioControlsOverlay_) {
        lv_obj_add_flag(scenarioControlsOverlay_, LV_OBJ_FLAG_HIDDEN);
    }
    updateScenarioButtonState();
}

void TrainingActiveView::refreshScenarioControlsOverlay()
{
    if (!scenarioControlsOverlay_ || !scenarioControlsOverlayContent_ || !contentRow_) {
        return;
    }

    if (!hasScenarioState_) {
        hideScenarioControlsOverlay();
        return;
    }

    if (!scenarioControlsOverlayVisible_) {
        updateScenarioButtonState();
        return;
    }

    constexpr int panelGapPx = 8;
    constexpr int panelDesiredWidthPx = 340;
    constexpr int panelDesiredHeightPx = 420;
    constexpr int panelMinWidthPx = 180;
    constexpr int panelMinHeightPx = 140;

    const int contentWidth = lv_obj_get_width(contentRow_);
    const int contentHeight = lv_obj_get_height(contentRow_);
    if (contentWidth <= (2 * panelGapPx) || contentHeight <= (2 * panelGapPx)) {
        return;
    }

    int anchorX = panelGapPx;
    int anchorRightX = panelGapPx;
    if (streamPanel_ && scenarioControlsButton_) {
        anchorX = lv_obj_get_x(streamPanel_) + lv_obj_get_x(scenarioControlsButton_);
        anchorRightX = anchorX + lv_obj_get_width(scenarioControlsButton_);
    }
    else {
        anchorRightX = anchorX;
    }

    const int maxPanelWidth = std::max(1, contentWidth - (2 * panelGapPx));
    int panelWidth = std::min(panelDesiredWidthPx, maxPanelWidth);
    if (maxPanelWidth >= panelMinWidthPx) {
        panelWidth = std::max(panelWidth, panelMinWidthPx);
    }

    const int maxPanelHeight = std::max(1, contentHeight - (2 * panelGapPx));
    int panelHeight = std::min(panelDesiredHeightPx, maxPanelHeight);
    if (maxPanelHeight >= panelMinHeightPx) {
        panelHeight = std::max(panelHeight, panelMinHeightPx);
    }

    const int rightX = anchorRightX + panelGapPx;
    const int leftX = anchorX - panelGapPx - panelWidth;
    const bool fitsRight = rightX + panelWidth + panelGapPx <= contentWidth;
    const bool fitsLeft = leftX >= panelGapPx;

    int panelX = panelGapPx;
    if (fitsRight) {
        panelX = rightX;
    }
    else if (fitsLeft) {
        panelX = leftX;
    }
    else {
        panelX = std::clamp(
            rightX, panelGapPx, std::max(panelGapPx, contentWidth - panelWidth - panelGapPx));
    }
    // Keep the flyout pinned to the top edge so it doesn't drift down and clip off-screen.
    const int panelY = panelGapPx;

    lv_obj_set_size(scenarioControlsOverlay_, panelWidth, panelHeight);
    lv_obj_set_pos(scenarioControlsOverlay_, panelX, panelY);

    if (scenarioControlsOverlayTitle_) {
        std::string title = std::string("Scenario Controls: ")
            + std::string(Scenario::toString(currentScenarioId_));
        lv_label_set_text(scenarioControlsOverlayTitle_, title.c_str());
    }

    if (!scenarioControls_ || scenarioControlsScenarioId_ != currentScenarioId_) {
        scenarioControls_.reset();
        lv_obj_clean(scenarioControlsOverlayContent_);

        scenarioControls_ = ScenarioControlsFactory::create(
            scenarioControlsOverlayContent_,
            wsService_,
            userSettingsManager_,
            &eventSink_,
            currentScenarioId_,
            currentScenarioConfig_,
            nullptr);
        scenarioControlsScenarioId_ = currentScenarioId_;

        if (!scenarioControls_) {
            lv_obj_t* placeholder = lv_label_create(scenarioControlsOverlayContent_);
            lv_obj_set_width(placeholder, LV_PCT(100));
            lv_label_set_long_mode(placeholder, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(placeholder, lv_color_hex(0xAAAAAA), 0);
            lv_label_set_text_fmt(
                placeholder,
                "No controls available for %s.",
                Scenario::toString(currentScenarioId_).c_str());
        }
    }

    if (scenarioControls_) {
        scenarioControls_->updateFromConfig(currentScenarioConfig_);
    }

    lv_obj_remove_flag(scenarioControlsOverlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(scenarioControlsOverlay_);
    updateScenarioButtonState();
}

void TrainingActiveView::updateScenarioButtonState()
{
    if (!scenarioControlsButton_) {
        return;
    }

    if (!hasScenarioState_) {
        scenarioControlsOverlayVisible_ = false;
        if (scenarioControlsOverlay_) {
            lv_obj_add_flag(scenarioControlsOverlay_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_state(scenarioControlsButton_, LV_STATE_DISABLED);
        lv_obj_set_style_opa(scenarioControlsButton_, LV_OPA_50, 0);
        LVGLBuilder::ActionButtonBuilder::setIcon(scenarioControlsButton_, LV_SYMBOL_RIGHT);
        return;
    }

    lv_obj_clear_state(scenarioControlsButton_, LV_STATE_DISABLED);
    lv_obj_set_style_opa(scenarioControlsButton_, LV_OPA_COVER, 0);
    LVGLBuilder::ActionButtonBuilder::setIcon(
        scenarioControlsButton_,
        scenarioControlsOverlayVisible_ ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT);
}

void TrainingActiveView::updateBestPlaybackFrame(
    const WorldData& worldData,
    double fitness,
    int generation,
    const std::optional<ScenarioVideoFrame>& scenarioVideoFrame,
    const std::optional<NesControllerTelemetry>& nesControllerTelemetry)
{
    if (!userSettings_.uiTraining.bestPlaybackEnabled) {
        return;
    }

    bestWorldData_ = std::make_unique<WorldData>(worldData);
    bestNesControllerTelemetry_ = nesControllerTelemetry;
    if (scenarioVideoFrame.has_value()) {
        bestVideoFrame_ = scenarioVideoFrame;
    }
    else {
        bestVideoFrame_.reset();
    }
    bestFitness_ = fitness;
    bestGeneration_ = generation;
    scheduleBestRender();
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

void TrainingActiveView::updateProgress(const Api::EvolutionProgress& progress)
{
    if (!genLabel_ || !evalLabel_ || !generationBar_ || !evaluationBar_) {
        return;
    }

    setTrainingPaused(progress.isPaused);

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
    else if (statusLabel_) {
        if (progress.validatingBest) {
            lv_label_set_text(statusLabel_, "Validating Best...");
            lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x66CCFF), 0);
        }
        else if (evolutionStarted_) {
            lv_label_set_text(statusLabel_, "Training...");
            lv_obj_set_style_text_color(
                statusLabel_, trainingPhaseTextColor(progress.trainingPhase), 0);
        }
        else {
            lv_label_set_text(statusLabel_, "Ready");
            lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x888888), 0);
        }
    }

    char buf[64];

    // Update time displays (compact format).
    if (totalTimeLabel_) {
        snprintf(buf, sizeof(buf), "Time %.1fs", progress.totalTrainingSeconds);
        lv_label_set_text(totalTimeLabel_, buf);
    }

    if (simTimeLabel_) {
        snprintf(buf, sizeof(buf), "Sim %.1fs", progress.currentSimTime);
        lv_label_set_text(simTimeLabel_, buf);
    }

    if (speedupLabel_) {
        snprintf(buf, sizeof(buf), "Speed %.1fx", progress.speedupFactor);
        lv_label_set_text(speedupLabel_, buf);
    }

    if (etaLabel_) {
        if (progress.etaSeconds <= 0.0) {
            lv_label_set_text(etaLabel_, "ETA --");
        }
        else if (progress.etaSeconds < 60.0) {
            snprintf(buf, sizeof(buf), "ETA %.0fs", progress.etaSeconds);
            lv_label_set_text(etaLabel_, buf);
        }
        else if (progress.etaSeconds < 3600.0) {
            int mins = static_cast<int>(progress.etaSeconds) / 60;
            int secs = static_cast<int>(progress.etaSeconds) % 60;
            snprintf(buf, sizeof(buf), "ETA %dm %ds", mins, secs);
            lv_label_set_text(etaLabel_, buf);
        }
        else {
            int hours = static_cast<int>(progress.etaSeconds) / 3600;
            int mins = (static_cast<int>(progress.etaSeconds) % 3600) / 60;
            snprintf(buf, sizeof(buf), "ETA %dh %dm", hours, mins);
            lv_label_set_text(etaLabel_, buf);
        }
    }

    if (cpuLabel_) {
        if (progress.cpuPercent > 0.0) {
            snprintf(buf, sizeof(buf), "CPU %.0f%%", progress.cpuPercent);
            lv_label_set_text(cpuLabel_, buf);
        }
        else {
            lv_label_set_text(cpuLabel_, "CPU --");
        }
    }
    if (cpuCorePlot_) {
        if (progress.cpuPercentPerCore.empty()) {
            cpuCorePlot_->clear();
        }
        else {
            cpuCorePlot_->setSamples(buildCpuCoreSeries(progress));
        }
    }

    if (parallelismLabel_) {
        if (progress.activeParallelism > 0) {
            snprintf(buf, sizeof(buf), "Par %d", progress.activeParallelism);
            lv_label_set_text(parallelismLabel_, buf);
        }
        else {
            lv_label_set_text(parallelismLabel_, "Par --");
        }
    }

    // Update generation progress.
    if (progress.maxGenerations > 0) {
        snprintf(buf, sizeof(buf), "Gen %d/%d", progress.generation, progress.maxGenerations);
    }
    else {
        snprintf(buf, sizeof(buf), "Gen %d", progress.generation);
    }
    lv_label_set_text(genLabel_, buf);

    const int genPercent =
        progress.maxGenerations > 0 ? (progress.generation * 100) / progress.maxGenerations : 0;
    lv_bar_set_value(generationBar_, genPercent, LV_ANIM_ON);

    // Update evaluation progress.
    int evalPercent = 0;
    if (progress.validatingBest && progress.validatingBestTargetSamples > 0) {
        snprintf(
            buf,
            sizeof(buf),
            "Validate %d/%d",
            progress.validatingBestCompletedSamples,
            progress.validatingBestTargetSamples);
        lv_label_set_text(evalLabel_, buf);
        evalPercent =
            (progress.validatingBestCompletedSamples * 100) / progress.validatingBestTargetSamples;
    }
    else {
        if (progress.populationSize > 0) {
            snprintf(buf, sizeof(buf), "Eval %d/%d", progress.currentEval, progress.populationSize);
        }
        else {
            snprintf(buf, sizeof(buf), "Eval %d", progress.currentEval);
        }
        lv_label_set_text(evalLabel_, buf);
        evalPercent = progress.populationSize > 0
            ? (progress.currentEval * 100) / progress.populationSize
            : 0;
    }
    lv_bar_set_value(evaluationBar_, evalPercent, LV_ANIM_ON);

    if (genomeCountLabel_) {
        if (progress.genomeArchiveMaxSize > 0) {
            snprintf(
                buf,
                sizeof(buf),
                "Genomes %d/%d",
                progress.totalGenomeCount,
                progress.genomeArchiveMaxSize);
            lv_label_set_text(genomeCountLabel_, buf);
            lv_obj_set_style_text_color(genomeCountLabel_, lv_color_hex(0x88AACC), 0);
        }
        else {
            snprintf(buf, sizeof(buf), "Genomes %d", progress.totalGenomeCount);
            lv_label_set_text(genomeCountLabel_, buf);
            lv_obj_set_style_text_color(genomeCountLabel_, lv_color_hex(0x88AACC), 0);
        }
    }

    // Update fitness labels (compact format).
    if (bestThisGenLabel_) {
        if (progress.lastCompletedGeneration >= 0) {
            snprintf(buf, sizeof(buf), "Last: %.2f", progress.lastGenerationFitnessMax);
            lv_label_set_text(bestThisGenLabel_, buf);
        }
        else if (progress.bestThisGenSource != "none" || progress.robustEvaluationCount > 0) {
            snprintf(buf, sizeof(buf), "Current: %.2f", progress.bestFitnessThisGen);
            lv_label_set_text(bestThisGenLabel_, buf);
        }
        else {
            lv_label_set_text(bestThisGenLabel_, "Last: --");
        }
    }

    if (bestAllTimeLabel_) {
        snprintf(buf, sizeof(buf), "Best: %.4f", progress.bestFitnessAllTime);
        lv_label_set_text(bestAllTimeLabel_, buf);
    }

    if (adaptationPhaseLabel_) {
        snprintf(buf, sizeof(buf), "Phase: %s", trainingPhaseLabel(progress.trainingPhase));
        lv_label_set_text(adaptationPhaseLabel_, buf);
        setCompactStatsBadgeAccent(
            adaptationPhaseLabel_, trainingPhaseTextColor(progress.trainingPhase));
    }

    if (adaptationSinceImprovementLabel_) {
        snprintf(buf, sizeof(buf), "Since: %d", std::max(progress.generationsSinceImprovement, 0));
        lv_label_set_text(adaptationSinceImprovementLabel_, buf);
    }

    if (adaptationMutationModeLabel_) {
        snprintf(
            buf,
            sizeof(buf),
            "Mut: %s",
            adaptiveMutationModeLabel(progress.lastBreeding.mutationMode));
        lv_label_set_text(adaptationMutationModeLabel_, buf);
        setCompactStatsBadgeAccent(
            adaptationMutationModeLabel_,
            adaptiveMutationModeTextColor(progress.lastBreeding.mutationMode));
    }

    if (adaptationResolvedLabel_) {
        if (progress.lastCompletedGeneration < 0 && progress.generation == 0) {
            lv_label_set_text(adaptationResolvedLabel_, "Resolved: --");
        }
        else if (progress.lastBreeding.usesBudget) {
            snprintf(
                buf,
                sizeof(buf),
                "Resolved: %d/%d/%.3f",
                progress.lastBreeding.resolvedPerturbationsPerOffspring,
                progress.lastBreeding.resolvedResetsPerOffspring,
                progress.lastBreeding.resolvedSigma);
            lv_label_set_text(adaptationResolvedLabel_, buf);
        }
        else {
            snprintf(
                buf,
                sizeof(buf),
                "Resolved: legacy sigma %.3f",
                progress.lastBreeding.resolvedSigma);
            lv_label_set_text(adaptationResolvedLabel_, buf);
        }
    }

    if (adaptationActualWeightChangesLabel_) {
        if (progress.lastCompletedGeneration < 0 && progress.generation == 0) {
            lv_label_set_text(adaptationActualWeightChangesLabel_, "Actual dW: --");
        }
        else if (progress.lastBreeding.weightChangesMin == progress.lastBreeding.weightChangesMax) {
            snprintf(
                buf,
                sizeof(buf),
                "Actual dW: %.1f [%d]",
                progress.lastBreeding.weightChangesAvg,
                progress.lastBreeding.weightChangesMin);
            lv_label_set_text(adaptationActualWeightChangesLabel_, buf);
        }
        else {
            snprintf(
                buf,
                sizeof(buf),
                "Actual dW: %.1f [%d..%d]",
                progress.lastBreeding.weightChangesAvg,
                progress.lastBreeding.weightChangesMin,
                progress.lastBreeding.weightChangesMax);
            lv_label_set_text(adaptationActualWeightChangesLabel_, buf);
        }
    }

    if (adaptationActualResetAvgLabel_) {
        if (progress.lastCompletedGeneration < 0 && progress.generation == 0) {
            lv_label_set_text(adaptationActualResetAvgLabel_, "Reset: --");
        }
        else {
            snprintf(buf, sizeof(buf), "Reset: %.1f", progress.lastBreeding.resetsAvg);
            lv_label_set_text(adaptationActualResetAvgLabel_, buf);
        }
    }

    if (adaptationLastImprovementLabel_) {
        if (progress.lastImprovementGeneration >= 0) {
            snprintf(buf, sizeof(buf), "Last: %d", progress.lastImprovementGeneration);
            lv_label_set_text(adaptationLastImprovementLabel_, buf);
        }
        else {
            lv_label_set_text(adaptationLastImprovementLabel_, "Last: --");
        }
    }

    if (adaptationStagnationLabel_) {
        snprintf(buf, sizeof(buf), "S: %d", std::max(progress.stagnationLevel, 0));
        lv_label_set_text(adaptationStagnationLabel_, buf);
    }

    if (adaptationRecoveryLabel_) {
        snprintf(buf, sizeof(buf), "R: %d", std::max(progress.recoveryLevel, 0));
        lv_label_set_text(adaptationRecoveryLabel_, buf);
    }

    if (lastGenerationDistributionPlot_) {
        lastGenerationDistributionPlot_->setTitle("Last Gen Distribution");
        if (progress.lastGenerationFitnessHistogram.empty()) {
            lastGenerationDistributionPlot_->clearBottomLabels();
        }
        else {
            char minBuf[24];
            char maxBuf[24];
            snprintf(minBuf, sizeof(minBuf), "%.2f", progress.lastGenerationFitnessMin);
            snprintf(maxBuf, sizeof(maxBuf), "%.2f", progress.lastGenerationFitnessMax);
            lastGenerationDistributionPlot_->setBottomLabels(minBuf, maxBuf);
        }
        lastGenerationDistributionPlot_->setSamples(buildDistributionSeries(progress));
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

void TrainingActiveView::updateFitnessPlots(
    const std::vector<float>& bestFitnessSeries,
    const std::vector<float>& averageFitnessSeries,
    const std::vector<uint8_t>& newBestMask)
{
    if (bestFitnessPlot_) {
        bestFitnessPlot_->setSamplesWithSecondaryAndHighlights(
            bestFitnessSeries, averageFitnessSeries, newBestMask);
    }
    if (fitnessPlotsPanel_) {
        lv_obj_invalidate(fitnessPlotsPanel_);
    }
}

void TrainingActiveView::clearFitnessPlots()
{
    if (lastGenerationDistributionPlot_) {
        lastGenerationDistributionPlot_->setTitle("Last Gen Distribution");
        lastGenerationDistributionPlot_->clearBottomLabels();
        lastGenerationDistributionPlot_->clear();
    }
    if (bestFitnessPlot_) {
        bestFitnessPlot_->clear();
    }
    if (fitnessPlotsPanel_) {
        lv_obj_invalidate(fitnessPlotsPanel_);
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
        bestSnapshotWorldData_.reset();
        bestVideoFrame_.reset();
        liveNesControllerTelemetry_.reset();
        bestNesControllerTelemetry_.reset();
        bestFitness_ = 0.0;
        bestGeneration_ = 0;
        bestSnapshotFitness_ = 0.0;
        bestSnapshotGeneration_ = 0;
        hasBestSnapshot_ = false;
        hasShownBestSnapshot_ = false;
        clearFitnessPlots();
        if (cpuCorePlot_) {
            cpuCorePlot_->clear();
        }
        if (bestCommandSummaryLabel_) {
            lv_label_set_text(bestCommandSummaryLabel_, "No best snapshot yet.");
        }
        renderBestFitnessPresentationPlaceholder("No best snapshot yet.");
        scenarioControls_.reset();
        currentScenarioConfig_ = Config::Empty{};
        currentScenarioId_ = Scenario::EnumType::Empty;
        scenarioControlsScenarioId_ = Scenario::EnumType::Empty;
        hasScenarioState_ = false;
        hideScenarioControlsOverlay();
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
    setBestPlaybackEnabled(userSettings_.uiTraining.bestPlaybackEnabled);
    setBestPlaybackIntervalMs(userSettings_.uiTraining.bestPlaybackIntervalMs);
    setNesControllerOverlayEnabled(isNesControllerOverlayEnabled(userSettings_));
}

void TrainingActiveView::setEvolutionCompleted(GenomeId /*bestGenomeId*/)
{
    evolutionStarted_ = false;

    if (statusLabel_) {
        lv_label_set_text(statusLabel_, "Complete!");
        lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xFFDD66), 0);
    }

    if (pauseResumeButton_) {
        lv_obj_add_flag(pauseResumeButton_, LV_OBJ_FLAG_HIDDEN);
    }
    setTrainingPaused(false);
}

void TrainingActiveView::renderBestWorld()
{
    if (!bestWorldContainer_ || !bestWorldData_) {
        LOG_WARN(
            Controls,
            "TrainingActiveView: renderBestWorld skipped (container={}, data={})",
            static_cast<const void*>(bestWorldContainer_),
            static_cast<const void*>(bestWorldData_.get()));
        return;
    }

    // Use VideoSurface for NES frames.
    if (bestVideoFrame_.has_value()) {
        if (!bestVideoSurface_) {
            bestVideoSurface_ = std::make_unique<VideoSurface>();
        }
        bestVideoSurface_->present(bestVideoFrame_.value(), bestWorldContainer_);
        updateNesVideoOverlay(bestNesOverlayWidgets_, bestNesControllerTelemetry_, true);
    }
    else {
        updateNesVideoOverlay(bestNesOverlayWidgets_, bestNesControllerTelemetry_, false);
        if (!bestRenderer_) {
            LOG_WARN(Controls, "TrainingActiveView: renderBestWorld skipped (no renderer)");
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

        bestRenderer_->renderWorldData(
            *bestWorldData_, bestWorldContainer_, false, RenderMode::SHARP);
    }
    if (!hasShownBestSnapshot_) {
        lv_refr_now(lv_display_get_default());
        hasShownBestSnapshot_ = true;
    }

    if (bestFitnessLabel_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Best: %.4f (Gen %d)", bestFitness_, bestGeneration_);
        lv_label_set_text(bestFitnessLabel_, buf);
    }
}

void TrainingActiveView::scheduleBestRender()
{
    if (!bestWorldContainer_ || !bestWorldData_) {
        return;
    }
    if (!bestVideoFrame_.has_value() && !bestRenderer_) {
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
                                 .value(userSettings_.uiTraining.streamIntervalMs)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(100))
                                 .callback(onStreamIntervalChanged, this)
                                 .buildOrLog();

    bestPlaybackToggle_ = LVGLBuilder::actionButton(streamPanel_)
                              .text("Best Playback")
                              .mode(LVGLBuilder::ActionMode::Toggle)
                              .checked(userSettings_.uiTraining.bestPlaybackEnabled)
                              .width(LV_PCT(100))
                              .height(LVGLBuilder::Style::ACTION_SIZE)
                              .layoutRow()
                              .alignLeft()
                              .callback(onBestPlaybackToggled, this)
                              .buildOrLog();

    bestPlaybackIntervalStepper_ =
        LVGLBuilder::actionStepper(streamPanel_)
            .label("Best Playback (ms)")
            .range(1, 5000)
            .step(1)
            .value(std::max(1, userSettings_.uiTraining.bestPlaybackIntervalMs))
            .valueFormat("%.0f")
            .valueScale(1.0)
            .width(LV_PCT(100))
            .callback(onBestPlaybackIntervalChanged, this)
            .buildOrLog();

    scenarioControlsButton_ = LVGLBuilder::actionButton(streamPanel_)
                                  .text("Scenario Controls")
                                  .icon(LV_SYMBOL_RIGHT)
                                  .mode(LVGLBuilder::ActionMode::Push)
                                  .width(LV_PCT(100))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutRow()
                                  .alignLeft()
                                  .callback(onScenarioControlsClicked, this)
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

    nesControllerOverlayToggle_ = LVGLBuilder::actionButton(streamPanel_)
                                      .text("NN + Pad")
                                      .mode(LVGLBuilder::ActionMode::Toggle)
                                      .checked(isNesControllerOverlayEnabled(userSettings_))
                                      .width(LV_PCT(100))
                                      .height(LVGLBuilder::Style::ACTION_SIZE)
                                      .layoutRow()
                                      .alignLeft()
                                      .callback(onNesControllerOverlayToggled, this)
                                      .buildOrLog();

    cpuCorePlot_ = std::make_unique<TimeSeriesPlotWidget>(
        streamPanel_,
        TimeSeriesPlotWidget::Config{
            .title = "CPU",
            .lineColor = lv_color_hex(0x66CC88),
            .defaultMinY = 0.0f,
            .defaultMaxY = 100.0f,
            .valueScale = 1.0f,
            .autoScaleY = false,
            .showYAxisRangeLabels = true,
            .chartType = LV_CHART_TYPE_BAR,
            .barGroupGapPx = 1,
            .barSeriesGapPx = 0,
            .minPointCount = 1,
        });
    if (cpuCorePlot_ && cpuCorePlot_->getContainer()) {
        lv_obj_t* cpuPlotContainer = cpuCorePlot_->getContainer();
        lv_obj_set_width(cpuPlotContainer, LV_PCT(100));
        lv_obj_set_height(cpuPlotContainer, 118);
        lv_obj_set_flex_grow(cpuPlotContainer, 0);
    }
    if (cpuCorePlot_) {
        cpuCorePlot_->clear();
    }

    createScenarioControlsOverlay();
    updateScenarioButtonState();
    setBestPlaybackEnabled(userSettings_.uiTraining.bestPlaybackEnabled);
    setBestPlaybackIntervalMs(userSettings_.uiTraining.bestPlaybackIntervalMs);
    setNesControllerOverlayEnabled(isNesControllerOverlayEnabled(userSettings_));
}

void TrainingActiveView::onStreamIntervalChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self || !self->streamIntervalStepper_) {
        return;
    }

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->streamIntervalStepper_);
    self->setStreamIntervalMs(value);
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = value,
            .bestPlaybackEnabled = self->userSettings_.uiTraining.bestPlaybackEnabled,
            .bestPlaybackIntervalMs = self->userSettings_.uiTraining.bestPlaybackIntervalMs,
            .nesControllerOverlayEnabled =
                self->userSettings_.uiTraining.nesControllerOverlayEnabled,
        });
}

void TrainingActiveView::onBestPlaybackToggled(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self || !self->bestPlaybackToggle_) {
        return;
    }

    const bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->bestPlaybackToggle_);
    self->setBestPlaybackEnabled(enabled);
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = self->userSettings_.uiTraining.streamIntervalMs,
            .bestPlaybackEnabled = self->userSettings_.uiTraining.bestPlaybackEnabled,
            .bestPlaybackIntervalMs = self->userSettings_.uiTraining.bestPlaybackIntervalMs,
            .nesControllerOverlayEnabled =
                self->userSettings_.uiTraining.nesControllerOverlayEnabled,
        });
}

void TrainingActiveView::onBestPlaybackIntervalChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self || !self->bestPlaybackIntervalStepper_) {
        return;
    }

    const int32_t value =
        LVGLBuilder::ActionStepperBuilder::getValue(self->bestPlaybackIntervalStepper_);
    self->setBestPlaybackIntervalMs(value);
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = self->userSettings_.uiTraining.streamIntervalMs,
            .bestPlaybackEnabled = self->userSettings_.uiTraining.bestPlaybackEnabled,
            .bestPlaybackIntervalMs = self->userSettings_.uiTraining.bestPlaybackIntervalMs,
            .nesControllerOverlayEnabled =
                self->userSettings_.uiTraining.nesControllerOverlayEnabled,
        });
}

void TrainingActiveView::onNesControllerOverlayToggled(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self || !self->nesControllerOverlayToggle_) {
        return;
    }

    const bool enabled =
        LVGLBuilder::ActionButtonBuilder::isChecked(self->nesControllerOverlayToggle_);
    self->setNesControllerOverlayEnabled(enabled);
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = self->userSettings_.uiTraining.streamIntervalMs,
            .bestPlaybackEnabled = self->userSettings_.uiTraining.bestPlaybackEnabled,
            .bestPlaybackIntervalMs = self->userSettings_.uiTraining.bestPlaybackIntervalMs,
            .nesControllerOverlayEnabled = enabled,
        });
}

void TrainingActiveView::onScenarioControlsClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingActiveView*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    if (!self->hasScenarioState_) {
        self->updateScenarioButtonState();
        return;
    }

    if (self->scenarioControlsOverlayVisible_) {
        self->hideScenarioControlsOverlay();
        return;
    }

    self->showScenarioControlsOverlay();
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
