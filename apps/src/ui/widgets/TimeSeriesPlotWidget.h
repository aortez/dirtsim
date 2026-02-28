#pragma once

#include "lvgl/lvgl.h"
#include <cstdint>
#include <string>
#include <vector>

namespace DirtSim {
namespace Ui {

class TimeSeriesPlotWidget {
public:
    struct Config {
        std::string title;
        lv_color_t lineColor = lv_color_hex(0x88AACC);
        lv_color_t secondaryLineColor = lv_color_hex(0x66BBFF);
        lv_color_t highlightColor = lv_color_hex(0xFF4FA3);
        float defaultMinY = 0.0f;
        float defaultMaxY = 1.0f;
        float valueScale = 100.0f;
        bool autoScaleY = true;
        bool hideZeroValuePoints = false;
        bool showSecondarySeries = false;
        bool showHighlights = false;
        bool showYAxisRangeLabels = true;
        lv_chart_type_t chartType = LV_CHART_TYPE_LINE;
        int32_t barGroupGapPx = -1;
        int32_t barSeriesGapPx = -1;
        int32_t highlightMarkerSizePx = 7;
        uint32_t minPointCount = 2;
    };

    TimeSeriesPlotWidget(lv_obj_t* parent, Config config);
    ~TimeSeriesPlotWidget() = default;

    void clear();
    void setTitle(const std::string& title);
    void setBottomLabels(const std::string& left, const std::string& right);
    void clearBottomLabels();
    void setSamples(const std::vector<float>& samples);
    void setSamplesWithSecondary(
        const std::vector<float>& samples, const std::vector<float>& secondarySamples);
    void setSamplesWithHighlights(
        const std::vector<float>& samples, const std::vector<uint8_t>& highlightMask);
    void setSamplesWithSecondaryAndHighlights(
        const std::vector<float>& samples,
        const std::vector<float>& secondarySamples,
        const std::vector<uint8_t>& highlightMask);

    lv_obj_t* getContainer() const;

private:
    void setSamplesInternal(
        const std::vector<float>& samples,
        const std::vector<float>* secondarySamples,
        const std::vector<uint8_t>* highlightMask);
    int32_t toChartValue(float value) const;
    int32_t measureYAxisLabelTextWidth(const char* text) const;
    void setYAxisRange(float minValue, float maxValue);
    void updateYAxisLabelLayout(int32_t requiredLabelWidthPx);
    void updateYAxisRange(
        const std::vector<float>& samples, const std::vector<float>* secondarySamples);
    void updateYAxisRangeLabels(float minValue, float maxValue);

    Config config_;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* titleLabel_ = nullptr;
    lv_obj_t* chart_ = nullptr;
    lv_obj_t* highlightChart_ = nullptr;
    lv_obj_t* yAxisMaxLabel_ = nullptr;
    lv_obj_t* yAxisMinLabel_ = nullptr;
    lv_obj_t* bottomLabelsRow_ = nullptr;
    lv_obj_t* bottomLeftLabel_ = nullptr;
    lv_obj_t* bottomRightLabel_ = nullptr;
    lv_chart_series_t* highlightSeries_ = nullptr;
    lv_chart_series_t* secondarySeries_ = nullptr;
    lv_chart_series_t* series_ = nullptr;
    std::vector<int32_t> chartValues_;
    uint32_t minPointCount_ = 2;
    int32_t yAxisRangeLabelWidthPx_ = 20;
    bool hasDisplayedYAxisRange_ = false;
    float displayedYAxisMin_ = 0.0f;
    float displayedYAxisMax_ = 0.0f;
};

} // namespace Ui
} // namespace DirtSim
