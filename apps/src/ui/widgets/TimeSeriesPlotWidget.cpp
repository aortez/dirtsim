#include "TimeSeriesPlotWidget.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace DirtSim {
namespace Ui {

namespace {
constexpr float axisPaddingRatio = 0.1f;
constexpr float minAxisPadding = 0.1f;
constexpr int minYAxisRangeLabelWidthPx = 20;
constexpr int yAxisRangeLabelGapPx = 4;
constexpr int yAxisRangeLabelPaddingPx = 2;
} // namespace

TimeSeriesPlotWidget::TimeSeriesPlotWidget(lv_obj_t* parent, Config config)
    : config_(std::move(config))
{
    minPointCount_ = std::max<uint32_t>(1, config_.minPointCount);

    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_grow(container_, 1);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(container_, 1, 0);
    lv_obj_set_style_border_color(container_, lv_color_hex(0x3A3A5A), 0);
    lv_obj_set_style_pad_all(container_, 6, 0);
    lv_obj_set_style_pad_gap(container_, 6, 0);
    lv_obj_set_style_radius(container_, 8, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    titleLabel_ = lv_label_create(container_);
    lv_label_set_text(titleLabel_, config_.title.c_str());
    lv_obj_set_style_text_color(titleLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(titleLabel_, &lv_font_montserrat_12, 0);

    chart_ = lv_chart_create(container_);
    lv_obj_set_size(chart_, LV_PCT(100), 0);
    lv_obj_set_flex_grow(chart_, 1);
    lv_obj_set_style_bg_color(chart_, lv_color_hex(0x10101A), 0);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart_, 0, 0);
    lv_obj_set_style_pad_all(chart_, 4, 0);
    const int32_t chartBottomPadPx = config_.chartType == LV_CHART_TYPE_BAR ? 0 : 6;
    lv_obj_set_style_pad_bottom(chart_, chartBottomPadPx, 0);
    if (config_.showYAxisRangeLabels) {
        lv_obj_set_style_pad_left(chart_, yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx, 0);
    }
    lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart_, config_.lineColor, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_clear_flag(chart_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart_, LV_OBJ_FLAG_CLICKABLE);

    lv_chart_set_type(chart_, config_.chartType);
    if (config_.chartType == LV_CHART_TYPE_BAR) {
        lv_obj_set_style_radius(chart_, 0, LV_PART_ITEMS);
        if (config_.barGroupGapPx >= 0) {
            lv_obj_set_style_pad_column(chart_, config_.barGroupGapPx, LV_PART_MAIN);
        }
        if (config_.barSeriesGapPx >= 0) {
            lv_obj_set_style_pad_column(chart_, config_.barSeriesGapPx, LV_PART_ITEMS);
        }
    }
    lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart_, 2, 3);
    lv_chart_set_point_count(chart_, minPointCount_);

    series_ = lv_chart_add_series(chart_, config_.lineColor, LV_CHART_AXIS_PRIMARY_Y);
    if (config_.showSecondarySeries) {
        secondarySeries_ =
            lv_chart_add_series(chart_, config_.secondaryLineColor, LV_CHART_AXIS_PRIMARY_Y);
    }
    lv_chart_set_all_values(chart_, series_, 0);
    if (secondarySeries_) {
        lv_chart_set_all_values(chart_, secondarySeries_, LV_CHART_POINT_NONE);
    }

    if (config_.showHighlights) {
        highlightChart_ = lv_chart_create(chart_);
        lv_obj_set_size(highlightChart_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(highlightChart_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(highlightChart_, 0, 0);
        lv_obj_set_style_pad_all(highlightChart_, 0, 0);
        lv_obj_set_style_pad_gap(highlightChart_, 0, 0);
        lv_obj_set_style_line_width(highlightChart_, 0, LV_PART_ITEMS);
        lv_obj_set_style_line_opa(highlightChart_, LV_OPA_TRANSP, LV_PART_ITEMS);
        const int32_t markerSizePx = std::max<int32_t>(1, config_.highlightMarkerSizePx);
        lv_obj_set_style_width(highlightChart_, markerSizePx, LV_PART_INDICATOR);
        lv_obj_set_style_height(highlightChart_, markerSizePx, LV_PART_INDICATOR);
        lv_obj_set_style_radius(highlightChart_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(highlightChart_, config_.highlightColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(highlightChart_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(highlightChart_, config_.highlightColor, LV_PART_INDICATOR);
        lv_obj_set_style_border_opa(highlightChart_, LV_OPA_80, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(highlightChart_, 1, LV_PART_INDICATOR);
        lv_obj_clear_flag(highlightChart_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(highlightChart_, LV_OBJ_FLAG_CLICKABLE);

        lv_chart_set_type(highlightChart_, LV_CHART_TYPE_LINE);
        lv_chart_set_update_mode(highlightChart_, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_div_line_count(highlightChart_, 0, 0);
        lv_chart_set_point_count(highlightChart_, minPointCount_);

        highlightSeries_ =
            lv_chart_add_series(highlightChart_, config_.highlightColor, LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_values(highlightChart_, highlightSeries_, LV_CHART_POINT_NONE);
    }

    if (config_.showYAxisRangeLabels) {
        yAxisMaxLabel_ = lv_label_create(chart_);
        lv_obj_set_width(yAxisMaxLabel_, yAxisRangeLabelWidthPx_);
        lv_label_set_long_mode(yAxisMaxLabel_, LV_LABEL_LONG_CLIP);
        lv_label_set_text(yAxisMaxLabel_, "");
        lv_obj_set_style_text_color(yAxisMaxLabel_, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(yAxisMaxLabel_, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(yAxisMaxLabel_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_clear_flag(yAxisMaxLabel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(yAxisMaxLabel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(
            yAxisMaxLabel_,
            LV_ALIGN_TOP_LEFT,
            -(yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx),
            0);

        yAxisMinLabel_ = lv_label_create(chart_);
        lv_obj_set_width(yAxisMinLabel_, yAxisRangeLabelWidthPx_);
        lv_label_set_long_mode(yAxisMinLabel_, LV_LABEL_LONG_CLIP);
        lv_label_set_text(yAxisMinLabel_, "");
        lv_obj_set_style_text_color(yAxisMinLabel_, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(yAxisMinLabel_, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(yAxisMinLabel_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_clear_flag(yAxisMinLabel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(yAxisMinLabel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(
            yAxisMinLabel_,
            LV_ALIGN_BOTTOM_LEFT,
            -(yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx),
            0);
    }

    setYAxisRange(config_.defaultMinY, config_.defaultMaxY);
    lv_chart_refresh(chart_);

    bottomLabelsRow_ = lv_obj_create(container_);
    lv_obj_set_size(bottomLabelsRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottomLabelsRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomLabelsRow_, 0, 0);
    lv_obj_set_style_pad_all(bottomLabelsRow_, 0, 0);
    lv_obj_set_style_pad_gap(bottomLabelsRow_, 0, 0);
    if (config_.showYAxisRangeLabels) {
        lv_obj_set_style_pad_left(
            bottomLabelsRow_, yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx, 0);
        lv_obj_set_style_pad_right(bottomLabelsRow_, 4, 0);
    }
    lv_obj_set_flex_flow(bottomLabelsRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        bottomLabelsRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(bottomLabelsRow_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottomLabelsRow_, LV_OBJ_FLAG_HIDDEN);

    bottomLeftLabel_ = lv_label_create(bottomLabelsRow_);
    lv_obj_set_width(bottomLeftLabel_, LV_PCT(50));
    lv_label_set_text(bottomLeftLabel_, "");
    lv_obj_set_style_text_color(bottomLeftLabel_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(bottomLeftLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(bottomLeftLabel_, LV_TEXT_ALIGN_LEFT, 0);

    bottomRightLabel_ = lv_label_create(bottomLabelsRow_);
    lv_obj_set_width(bottomRightLabel_, LV_PCT(50));
    lv_label_set_text(bottomRightLabel_, "");
    lv_obj_set_style_text_color(bottomRightLabel_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(bottomRightLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(bottomRightLabel_, LV_TEXT_ALIGN_RIGHT, 0);
}

void TimeSeriesPlotWidget::clear()
{
    if (!chart_ || !series_) {
        return;
    }

    chartValues_.assign(minPointCount_, 0);
    lv_chart_set_point_count(chart_, minPointCount_);
    lv_chart_set_all_values(chart_, series_, 0);
    if (secondarySeries_) {
        lv_chart_set_all_values(chart_, secondarySeries_, LV_CHART_POINT_NONE);
    }
    if (highlightChart_ && highlightSeries_) {
        lv_chart_set_point_count(highlightChart_, minPointCount_);
        lv_chart_set_all_values(highlightChart_, highlightSeries_, LV_CHART_POINT_NONE);
        lv_chart_refresh(highlightChart_);
    }
    setYAxisRange(config_.defaultMinY, config_.defaultMaxY);
    lv_chart_refresh(chart_);
}

void TimeSeriesPlotWidget::setTitle(const std::string& title)
{
    if (!titleLabel_) {
        return;
    }

    lv_label_set_text(titleLabel_, title.c_str());
}

void TimeSeriesPlotWidget::setBottomLabels(const std::string& left, const std::string& right)
{
    if (!bottomLabelsRow_ || !bottomLeftLabel_ || !bottomRightLabel_) {
        return;
    }

    lv_label_set_text(bottomLeftLabel_, left.c_str());
    lv_label_set_text(bottomRightLabel_, right.c_str());
    lv_obj_clear_flag(bottomLabelsRow_, LV_OBJ_FLAG_HIDDEN);
}

void TimeSeriesPlotWidget::clearBottomLabels()
{
    if (!bottomLabelsRow_ || !bottomLeftLabel_ || !bottomRightLabel_) {
        return;
    }

    lv_label_set_text(bottomLeftLabel_, "");
    lv_label_set_text(bottomRightLabel_, "");
    lv_obj_add_flag(bottomLabelsRow_, LV_OBJ_FLAG_HIDDEN);
}

void TimeSeriesPlotWidget::setSamples(const std::vector<float>& samples)
{
    setSamplesInternal(samples, nullptr, nullptr);
}

void TimeSeriesPlotWidget::setSamplesWithSecondary(
    const std::vector<float>& samples, const std::vector<float>& secondarySamples)
{
    setSamplesInternal(samples, &secondarySamples, nullptr);
}

void TimeSeriesPlotWidget::setSamplesWithHighlights(
    const std::vector<float>& samples, const std::vector<uint8_t>& highlightMask)
{
    setSamplesInternal(samples, nullptr, &highlightMask);
}

void TimeSeriesPlotWidget::setSamplesWithSecondaryAndHighlights(
    const std::vector<float>& samples,
    const std::vector<float>& secondarySamples,
    const std::vector<uint8_t>& highlightMask)
{
    setSamplesInternal(samples, &secondarySamples, &highlightMask);
}

void TimeSeriesPlotWidget::setSamplesInternal(
    const std::vector<float>& samples,
    const std::vector<float>* secondarySamples,
    const std::vector<uint8_t>* highlightMask)
{
    if (!chart_ || !series_) {
        return;
    }

    if (samples.empty() && (!secondarySamples || secondarySamples->empty())) {
        clear();
        return;
    }

    updateYAxisRange(samples, secondarySamples);

    const size_t secondarySampleCount = secondarySamples ? secondarySamples->size() : 0;
    const size_t sampleCount = std::max(samples.size(), secondarySampleCount);
    const uint32_t pointCount = std::max(
        minPointCount_,
        static_cast<uint32_t>(std::min<size_t>(
            sampleCount, static_cast<size_t>(std::numeric_limits<uint32_t>::max()))));
    if (lv_chart_get_point_count(chart_) != pointCount) {
        lv_chart_set_point_count(chart_, pointCount);
    }
    if (highlightChart_ && lv_chart_get_point_count(highlightChart_) != pointCount) {
        lv_chart_set_point_count(highlightChart_, pointCount);
    }

    const auto toPlotValue = [this](float value) {
        if (config_.hideZeroValuePoints
            && std::abs(value) <= std::numeric_limits<float>::epsilon()) {
            return static_cast<int32_t>(LV_CHART_POINT_NONE);
        }
        return toChartValue(value);
    };

    const auto buildSeriesValues = [&](const std::vector<float>* sourceSamples,
                                       bool padWithNone) -> std::vector<int32_t> {
        std::vector<int32_t> values(pointCount, padWithNone ? LV_CHART_POINT_NONE : 0);
        if (!sourceSamples || sourceSamples->empty()) {
            return values;
        }

        if (sourceSamples->size() == 1 && pointCount > 1) {
            const int32_t value = toPlotValue(sourceSamples->front());
            values[0] = value;
            for (uint32_t i = 1; i < pointCount; ++i) {
                values[i] = value;
            }
            return values;
        }

        for (size_t i = 0; i < sourceSamples->size(); ++i) {
            values[i] = toPlotValue((*sourceSamples)[i]);
        }
        return values;
    };

    chartValues_ = buildSeriesValues(&samples, false);
    const std::vector<int32_t> secondaryValues =
        secondarySeries_ ? buildSeriesValues(secondarySamples, true) : std::vector<int32_t>{};

    for (uint32_t i = 0; i < pointCount; ++i) {
        lv_chart_set_series_value_by_id(chart_, series_, i, chartValues_[i]);
    }
    if (secondarySeries_) {
        for (uint32_t i = 0; i < pointCount; ++i) {
            lv_chart_set_series_value_by_id(chart_, secondarySeries_, i, secondaryValues[i]);
        }
    }

    if (highlightChart_ && highlightSeries_) {
        for (uint32_t i = 0; i < pointCount; ++i) {
            int32_t highlightValue = LV_CHART_POINT_NONE;
            if (highlightMask && i < highlightMask->size() && (*highlightMask)[i]
                && i < samples.size()) {
                highlightValue = toChartValue(samples[i]);
            }
            lv_chart_set_series_value_by_id(highlightChart_, highlightSeries_, i, highlightValue);
        }
        lv_chart_refresh(highlightChart_);
    }

    lv_chart_refresh(chart_);
}

lv_obj_t* TimeSeriesPlotWidget::getContainer() const
{
    return container_;
}

int32_t TimeSeriesPlotWidget::toChartValue(float value) const
{
    return static_cast<int32_t>(std::lround(value * config_.valueScale));
}

int32_t TimeSeriesPlotWidget::measureYAxisLabelTextWidth(const char* text) const
{
    if (!text || text[0] == '\0') {
        return 0;
    }

    const lv_obj_t* referenceLabel = yAxisMinLabel_ ? yAxisMinLabel_ : yAxisMaxLabel_;
    const lv_font_t* font = referenceLabel
        ? lv_obj_get_style_text_font(referenceLabel, LV_PART_MAIN)
        : &lv_font_montserrat_12;
    const int32_t letterSpace =
        referenceLabel ? lv_obj_get_style_text_letter_space(referenceLabel, LV_PART_MAIN) : 0;
    const int32_t lineSpace =
        referenceLabel ? lv_obj_get_style_text_line_space(referenceLabel, LV_PART_MAIN) : 0;

    lv_point_t textSize{};
    lv_text_get_size(
        &textSize,
        text,
        font ? font : &lv_font_montserrat_12,
        letterSpace,
        lineSpace,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE);
    return textSize.x;
}

void TimeSeriesPlotWidget::updateYAxisLabelLayout(int32_t requiredLabelWidthPx)
{
    if (!config_.showYAxisRangeLabels || !chart_ || !yAxisMinLabel_ || !yAxisMaxLabel_) {
        return;
    }

    const int32_t nextWidth = std::max(minYAxisRangeLabelWidthPx, requiredLabelWidthPx);
    if (nextWidth <= yAxisRangeLabelWidthPx_) {
        return;
    }

    yAxisRangeLabelWidthPx_ = nextWidth;
    lv_obj_set_width(yAxisMaxLabel_, yAxisRangeLabelWidthPx_);
    lv_obj_set_width(yAxisMinLabel_, yAxisRangeLabelWidthPx_);

    const int32_t xOffset = -(yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx);
    lv_obj_align(yAxisMaxLabel_, LV_ALIGN_TOP_LEFT, xOffset, 0);
    lv_obj_align(yAxisMinLabel_, LV_ALIGN_BOTTOM_LEFT, xOffset, 0);

    lv_obj_set_style_pad_left(chart_, yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx, 0);
    if (bottomLabelsRow_) {
        lv_obj_set_style_pad_left(
            bottomLabelsRow_, yAxisRangeLabelWidthPx_ + yAxisRangeLabelGapPx, 0);
    }
}

void TimeSeriesPlotWidget::setYAxisRange(float minValue, float maxValue)
{
    if (!chart_) {
        return;
    }

    if (maxValue <= minValue) {
        maxValue = minValue + 1.0f;
    }

    lv_chart_set_axis_range(
        chart_, LV_CHART_AXIS_PRIMARY_Y, toChartValue(minValue), toChartValue(maxValue));
    if (highlightChart_) {
        lv_chart_set_axis_range(
            highlightChart_,
            LV_CHART_AXIS_PRIMARY_Y,
            toChartValue(minValue),
            toChartValue(maxValue));
    }
    updateYAxisRangeLabels(minValue, maxValue);
}

void TimeSeriesPlotWidget::updateYAxisRange(
    const std::vector<float>& samples, const std::vector<float>* secondarySamples)
{
    if (!config_.autoScaleY) {
        setYAxisRange(config_.defaultMinY, config_.defaultMaxY);
        return;
    }

    float minValue = 0.0f;
    float maxValue = 0.0f;
    if (!samples.empty()) {
        auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
        minValue = *minIt;
        maxValue = *maxIt;
    }
    if (secondarySamples && !secondarySamples->empty()) {
        auto [secondaryMinIt, secondaryMaxIt] =
            std::minmax_element(secondarySamples->begin(), secondarySamples->end());
        if (samples.empty()) {
            minValue = *secondaryMinIt;
            maxValue = *secondaryMaxIt;
        }
        else {
            minValue = std::min(minValue, *secondaryMinIt);
            maxValue = std::max(maxValue, *secondaryMaxIt);
        }
    }
    const float range = std::max(maxValue - minValue, minAxisPadding);
    const float padding = std::max(range * axisPaddingRatio, minAxisPadding);
    setYAxisRange(minValue - padding, maxValue + padding);
}

void TimeSeriesPlotWidget::updateYAxisRangeLabels(float minValue, float maxValue)
{
    if (!config_.showYAxisRangeLabels || !yAxisMinLabel_ || !yAxisMaxLabel_) {
        return;
    }

    const int32_t minCent = static_cast<int32_t>(std::lround(minValue * 100.0f));
    const int32_t maxCent = static_cast<int32_t>(std::lround(maxValue * 100.0f));
    if (hasDisplayedYAxisRange_) {
        const int32_t displayedMinCent =
            static_cast<int32_t>(std::lround(displayedYAxisMin_ * 100.0f));
        const int32_t displayedMaxCent =
            static_cast<int32_t>(std::lround(displayedYAxisMax_ * 100.0f));
        if (minCent == displayedMinCent && maxCent == displayedMaxCent) {
            return;
        }
    }

    char minBuf[24];
    char maxBuf[24];
    const auto formatLabel = [](char* out, size_t outSize, float value) {
        const float roundedValue = std::round(value);
        if (std::abs(value - roundedValue) <= 0.005f) {
            snprintf(out, outSize, "%.0f", roundedValue);
            return;
        }
        snprintf(out, outSize, "%.2f", value);
    };
    formatLabel(minBuf, sizeof(minBuf), minValue);
    formatLabel(maxBuf, sizeof(maxBuf), maxValue);
    const int32_t requiredLabelWidthPx =
        std::max(measureYAxisLabelTextWidth(minBuf), measureYAxisLabelTextWidth(maxBuf))
        + yAxisRangeLabelPaddingPx;
    updateYAxisLabelLayout(requiredLabelWidthPx);
    lv_label_set_text(yAxisMinLabel_, minBuf);
    lv_label_set_text(yAxisMaxLabel_, maxBuf);

    hasDisplayedYAxisRange_ = true;
    displayedYAxisMin_ = minValue;
    displayedYAxisMax_ = maxValue;
}

} // namespace Ui
} // namespace DirtSim
