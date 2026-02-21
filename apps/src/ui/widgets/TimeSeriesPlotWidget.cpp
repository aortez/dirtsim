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
constexpr int yAxisRangeLabelWidthPx = 44;
constexpr int yAxisRangeLabelGapPx = 4;
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
    lv_obj_set_style_pad_bottom(chart_, 6, 0);
    if (config_.showYAxisRangeLabels) {
        lv_obj_set_style_pad_left(chart_, yAxisRangeLabelWidthPx + yAxisRangeLabelGapPx, 0);
    }
    lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(chart_, config_.lineColor, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart_, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_clear_flag(chart_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart_, LV_OBJ_FLAG_CLICKABLE);

    lv_chart_set_type(chart_, config_.chartType);
    if (config_.chartType == LV_CHART_TYPE_BAR) {
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
    lv_chart_set_all_values(chart_, series_, 0);

    if (config_.showYAxisRangeLabels) {
        yAxisMaxLabel_ = lv_label_create(chart_);
        lv_obj_set_width(yAxisMaxLabel_, yAxisRangeLabelWidthPx);
        lv_label_set_long_mode(yAxisMaxLabel_, LV_LABEL_LONG_CLIP);
        lv_label_set_text(yAxisMaxLabel_, "");
        lv_obj_set_style_text_color(yAxisMaxLabel_, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(yAxisMaxLabel_, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(yAxisMaxLabel_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_clear_flag(yAxisMaxLabel_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(yAxisMaxLabel_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(
            yAxisMaxLabel_, LV_ALIGN_TOP_LEFT, -(yAxisRangeLabelWidthPx + yAxisRangeLabelGapPx), 0);

        yAxisMinLabel_ = lv_label_create(chart_);
        lv_obj_set_width(yAxisMinLabel_, yAxisRangeLabelWidthPx);
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
            -(yAxisRangeLabelWidthPx + yAxisRangeLabelGapPx),
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
            bottomLabelsRow_, yAxisRangeLabelWidthPx + yAxisRangeLabelGapPx, 0);
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
    if (!chart_ || !series_) {
        return;
    }

    if (samples.empty()) {
        clear();
        return;
    }

    updateYAxisRange(samples);

    const uint32_t pointCount = std::max(minPointCount_, static_cast<uint32_t>(samples.size()));
    if (lv_chart_get_point_count(chart_) != pointCount) {
        lv_chart_set_point_count(chart_, pointCount);
    }

    const auto toPlotValue = [this](float value) {
        if (config_.hideZeroValuePoints
            && std::abs(value) <= std::numeric_limits<float>::epsilon()) {
            return static_cast<int32_t>(LV_CHART_POINT_NONE);
        }
        return toChartValue(value);
    };

    chartValues_.assign(pointCount, 0);
    if (samples.size() == 1 && pointCount > 1) {
        const int32_t value = toPlotValue(samples.front());
        chartValues_[0] = value;
        for (uint32_t i = 1; i < pointCount; ++i) {
            chartValues_[i] = value;
        }
    }
    else {
        for (size_t i = 0; i < samples.size(); ++i) {
            chartValues_[i] = toPlotValue(samples[i]);
        }
    }

    for (uint32_t i = 0; i < pointCount; ++i) {
        lv_chart_set_series_value_by_id(chart_, series_, i, chartValues_[i]);
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
    updateYAxisRangeLabels(minValue, maxValue);
}

void TimeSeriesPlotWidget::updateYAxisRange(const std::vector<float>& samples)
{
    if (!config_.autoScaleY) {
        setYAxisRange(config_.defaultMinY, config_.defaultMaxY);
        return;
    }

    auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    const float minValue = *minIt;
    const float maxValue = *maxIt;
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
    lv_label_set_text(yAxisMinLabel_, minBuf);
    lv_label_set_text(yAxisMaxLabel_, maxBuf);

    hasDisplayedYAxisRange_ = true;
    displayedYAxisMin_ = minValue;
    displayedYAxisMax_ = maxValue;
}

} // namespace Ui
} // namespace DirtSim
