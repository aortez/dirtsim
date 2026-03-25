#include "ScannerChannelMapWidget.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace DirtSim {
namespace Ui {

namespace {

const lv_color_t kChartBackgroundColor = lv_color_hex(0x10101A);
const lv_color_t kContainerBackgroundColor = lv_color_hex(0x1A1A2E);
const lv_color_t kContainerBorderColor = lv_color_hex(0x3A3A5A);
const lv_color_t kGuideColor = lv_color_hex(0x303046);
const lv_color_t kLabelColor = lv_color_hex(0xAAAAAA);
const lv_color_t kIncidentalColor = lv_color_hex(0xD56BFF);
const lv_color_t kManualRailFillColor = lv_color_hex(0x7C6730);
const lv_color_t kManualRailBorderColor = lv_color_hex(0xE7C75A);
constexpr int kBandGapPx = 14;
constexpr int kBubbleRadiusLargePx = 8;
constexpr int kBubbleRadiusMediumPx = 6;
constexpr int kBubbleRadiusSmallPx = 4;
constexpr int kChannelLabelBottomPaddingPx = 2;
constexpr int kLabelTopPaddingPx = 4;
constexpr int kOuterPaddingPx = 2;
constexpr int kPlotBottomPaddingPx = 4;
constexpr int kPlotTopPaddingPx = 2;
constexpr int kRailHeightPx = 10;
constexpr int kRailMarkerHeightPx = 6;
constexpr int kRailTopPaddingPx = 4;
constexpr int kRssiAxisLabelWidthPx = 24;
constexpr int kRssiGuideLabelGapPx = 6;
constexpr int kRssiBucketMaxDbm = -30;
constexpr int kRssiBucketMinDbm = -90;

const lv_font_t* channelMapLabelFont()
{
    return &lv_font_montserrat_12;
}

const lv_font_t* rssiAxisLabelFont()
{
    return &lv_font_montserrat_12;
}

lv_color_t accentColor(const OsManager::ScannerBand band)
{
    return band == OsManager::ScannerBand::Band24Ghz ? lv_color_hex(0x00CED1)
                                                     : lv_color_hex(0xFFDD66);
}

int bubbleRadius(const uint32_t count)
{
    if (count >= 4) {
        return kBubbleRadiusLargePx;
    }
    if (count >= 2) {
        return kBubbleRadiusMediumPx;
    }

    return kBubbleRadiusSmallPx;
}

lv_opa_t bubbleOpacity(const std::optional<uint64_t> freshestAgeMs)
{
    if (!freshestAgeMs.has_value() || freshestAgeMs.value() <= 2000) {
        return LV_OPA_COVER;
    }
    if (freshestAgeMs.value() <= 8000) {
        return LV_OPA_70;
    }

    return LV_OPA_40;
}

int bubbleY(const lv_area_t& plotArea, const int rssiBucketDbm)
{
    const int clampedDbm = std::clamp(rssiBucketDbm, kRssiBucketMinDbm, kRssiBucketMaxDbm);
    const float normalized = static_cast<float>(clampedDbm - kRssiBucketMinDbm)
        / static_cast<float>(kRssiBucketMaxDbm - kRssiBucketMinDbm);
    const int plotHeight = std::max(1, lv_area_get_height(&plotArea) - 1);
    return plotArea.y2 - static_cast<int>(std::round(normalized * plotHeight));
}

int guideY(const lv_area_t& plotArea, const int signalDbm)
{
    return bubbleY(plotArea, signalDbm);
}

bool channelXCenter(
    const std::vector<int>& channels, const lv_area_t& plotArea, const int channel, int& xCenterOut)
{
    const auto it = std::find(channels.begin(), channels.end(), channel);
    if (it == channels.end()) {
        return false;
    }

    const size_t index = static_cast<size_t>(std::distance(channels.begin(), it));
    size_t gapCount = 0;
    size_t gapsBeforeIndex = 0;
    for (size_t i = 1; i < channels.size(); ++i) {
        if (channels[i] - channels[i - 1] <= 4) {
            continue;
        }

        ++gapCount;
        if (i <= index) {
            ++gapsBeforeIndex;
        }
    }

    const float availableWidth = static_cast<float>(lv_area_get_width(&plotArea))
        - static_cast<float>(gapCount * kBandGapPx);
    const float slotWidth = availableWidth / static_cast<float>(channels.size());
    const float xBase = static_cast<float>(plotArea.x1) + static_cast<float>(index) * slotWidth
        + static_cast<float>(gapsBeforeIndex * kBandGapPx);
    xCenterOut = static_cast<int>(std::round(xBase + slotWidth * 0.5f));
    return true;
}

bool channelSlotBounds(
    const std::vector<int>& channels,
    const lv_area_t& plotArea,
    const int channel,
    int& x1Out,
    int& x2Out)
{
    const auto it = std::find(channels.begin(), channels.end(), channel);
    if (it == channels.end()) {
        return false;
    }

    const size_t index = static_cast<size_t>(std::distance(channels.begin(), it));
    size_t gapCount = 0;
    size_t gapsBeforeIndex = 0;
    for (size_t i = 1; i < channels.size(); ++i) {
        if (channels[i] - channels[i - 1] <= 4) {
            continue;
        }

        ++gapCount;
        if (i <= index) {
            ++gapsBeforeIndex;
        }
    }

    const float availableWidth = static_cast<float>(lv_area_get_width(&plotArea))
        - static_cast<float>(gapCount * kBandGapPx);
    const float slotWidth = availableWidth / static_cast<float>(channels.size());
    const float xBase = static_cast<float>(plotArea.x1) + static_cast<float>(index) * slotWidth
        + static_cast<float>(gapsBeforeIndex * kBandGapPx);
    x1Out = static_cast<int>(std::round(xBase));
    x2Out = static_cast<int>(std::round(xBase + slotWidth)) - 1;
    return true;
}

void drawGuideLine(lv_layer_t* layer, const lv_area_t& plotArea, const int y)
{
    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.color = kGuideColor;
    lineDsc.opa = LV_OPA_60;
    lineDsc.width = 1;

    lineDsc.p1.x = plotArea.x1;
    lineDsc.p1.y = y;
    lineDsc.p2.x = plotArea.x2;
    lineDsc.p2.y = y;
    lv_draw_line(layer, &lineDsc);
}

void drawLabel(
    lv_layer_t* layer,
    const std::string& text,
    const int centerX,
    const int topY,
    const lv_color_t color,
    const lv_font_t* font = channelMapLabelFont())
{
    lv_draw_label_dsc_t labelDsc;
    lv_draw_label_dsc_init(&labelDsc);
    labelDsc.color = color;
    labelDsc.font = font;
    labelDsc.text = text.c_str();
    labelDsc.text_local = 1;
    labelDsc.opa = LV_OPA_COVER;

    lv_point_t textSize;
    lv_text_get_size(
        &textSize,
        text.c_str(),
        labelDsc.font,
        labelDsc.letter_space,
        labelDsc.line_space,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE);

    lv_area_t textArea{
        .x1 = centerX - textSize.x / 2,
        .y1 = topY,
        .x2 = centerX - textSize.x / 2 + textSize.x - 1,
        .y2 = topY + textSize.y - 1,
    };
    lv_draw_label(layer, &labelDsc, &textArea);
}

} // namespace

ScannerChannelMapWidget::ScannerChannelMapWidget(lv_obj_t* parent)
{
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(container_, kContainerBackgroundColor, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(container_, kContainerBorderColor, 0);
    lv_obj_set_style_border_width(container_, 1, 0);
    lv_obj_set_style_pad_all(container_, 6, 0);
    lv_obj_set_style_pad_gap(container_, 4, 0);
    lv_obj_set_style_radius(container_, 8, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    map_ = lv_obj_create(container_);
    lv_obj_set_size(map_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(map_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(map_, 0, 0);
    lv_obj_set_style_pad_all(map_, 0, 0);
    lv_obj_set_style_radius(map_, 0, 0);
    lv_obj_clear_flag(map_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(map_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(map_, onMapDraw, LV_EVENT_DRAW_MAIN, this);
}

void ScannerChannelMapWidget::clear()
{
    model_.currentTuning.reset();
    model_.bubbles.clear();
    lv_obj_invalidate(map_);
}

lv_obj_t* ScannerChannelMapWidget::getContainer() const
{
    return container_;
}

void ScannerChannelMapWidget::setModel(Model model)
{
    model_ = std::move(model);
    lv_obj_invalidate(map_);
}

void ScannerChannelMapWidget::onMapDraw(lv_event_t* e)
{
    auto* self = static_cast<ScannerChannelMapWidget*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->drawMap(e);
}

void ScannerChannelMapWidget::drawMap(lv_event_t* e) const
{
    if (!map_) {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(e);
    if (!layer) {
        return;
    }

    const std::vector<int> channels = OsManager::scannerBandPrimaryChannels(model_.band);
    if (channels.empty()) {
        return;
    }

    lv_area_t contentArea;
    lv_obj_get_content_coords(map_, &contentArea);

    const int channelLabelHeight = lv_font_get_line_height(channelMapLabelFont());
    const int railReservedHeight = kRailTopPaddingPx + kRailHeightPx;
    lv_area_t plotArea{
        .x1 = contentArea.x1 + kOuterPaddingPx + kRssiAxisLabelWidthPx + kRssiGuideLabelGapPx,
        .y1 = contentArea.y1 + kPlotTopPaddingPx,
        .x2 = contentArea.x2 - kOuterPaddingPx,
        .y2 = contentArea.y2 - channelLabelHeight - kLabelTopPaddingPx
            - kChannelLabelBottomPaddingPx - railReservedHeight - kPlotBottomPaddingPx,
    };
    if (plotArea.x2 <= plotArea.x1 || plotArea.y2 <= plotArea.y1) {
        return;
    }

    lv_draw_rect_dsc_t backgroundDsc;
    lv_draw_rect_dsc_init(&backgroundDsc);
    backgroundDsc.bg_color = kChartBackgroundColor;
    backgroundDsc.bg_opa = LV_OPA_COVER;
    backgroundDsc.radius = 0;
    lv_draw_rect(layer, &backgroundDsc, &plotArea);

    for (const int signalDbm : std::array<int, 3>{ { -85, -65, -45 } }) {
        const int y = guideY(plotArea, signalDbm);
        drawGuideLine(layer, plotArea, y);
        drawLabel(
            layer,
            std::to_string(signalDbm),
            contentArea.x1 + kRssiAxisLabelWidthPx / 2,
            y - lv_font_get_line_height(rssiAxisLabelFont()) / 2,
            kLabelColor,
            rssiAxisLabelFont());
    }

    lv_area_t railArea{
        .x1 = plotArea.x1,
        .y1 = plotArea.y2 + kLabelTopPaddingPx + channelLabelHeight + kChannelLabelBottomPaddingPx
            + kRailTopPaddingPx,
        .x2 = plotArea.x2,
        .y2 = plotArea.y2 + kLabelTopPaddingPx + channelLabelHeight + kChannelLabelBottomPaddingPx
            + kRailTopPaddingPx + kRailHeightPx - 1,
    };

    lv_draw_rect_dsc_t railDsc;
    lv_draw_rect_dsc_init(&railDsc);
    railDsc.bg_color = kGuideColor;
    railDsc.bg_opa = LV_OPA_50;
    railDsc.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(layer, &railDsc, &railArea);

    if (model_.currentTuning.has_value() && model_.currentTuning->band == model_.band) {
        const auto coveredChannels =
            OsManager::scannerTuningCoveredPrimaryChannels(model_.currentTuning.value());
        int slotLeft = 0;
        int slotRight = 0;
        bool hasBounds = false;
        for (const int channel : coveredChannels) {
            int candidateLeft = 0;
            int candidateRight = 0;
            if (!channelSlotBounds(channels, plotArea, channel, candidateLeft, candidateRight)) {
                continue;
            }

            if (!hasBounds) {
                slotLeft = candidateLeft;
                slotRight = candidateRight;
                hasBounds = true;
                continue;
            }

            slotLeft = std::min(slotLeft, candidateLeft);
            slotRight = std::max(slotRight, candidateRight);
        }

        if (hasBounds) {
            if (model_.mode == OsManager::ScannerConfigMode::Manual) {
                lv_draw_rect_dsc_t tuningDsc;
                lv_draw_rect_dsc_init(&tuningDsc);
                tuningDsc.bg_color = kManualRailFillColor;
                tuningDsc.bg_opa = LV_OPA_90;
                tuningDsc.border_color = kManualRailBorderColor;
                tuningDsc.border_opa = LV_OPA_COVER;
                tuningDsc.border_width = 1;
                tuningDsc.radius = LV_RADIUS_CIRCLE;

                lv_area_t tuningArea{
                    .x1 = slotLeft + 1,
                    .y1 = railArea.y1 + (kRailHeightPx - kRailMarkerHeightPx) / 2,
                    .x2 = slotRight - 1,
                    .y2 = railArea.y1 + (kRailHeightPx - kRailMarkerHeightPx) / 2
                        + kRailMarkerHeightPx - 1,
                };
                lv_draw_rect(layer, &tuningDsc, &tuningArea);

                lv_draw_line_dsc_t centerTickDsc;
                lv_draw_line_dsc_init(&centerTickDsc);
                centerTickDsc.color = kManualRailBorderColor;
                centerTickDsc.opa = LV_OPA_COVER;
                centerTickDsc.width = 1;
                const int centerX = (slotLeft + slotRight) / 2;
                centerTickDsc.p1.x = centerX;
                centerTickDsc.p1.y = railArea.y1 - 3;
                centerTickDsc.p2.x = centerX;
                centerTickDsc.p2.y = railArea.y2 + 1;
                lv_draw_line(layer, &centerTickDsc);
            }
            else {
                int primaryCenterX = 0;
                if (channelXCenter(
                        channels, plotArea, model_.currentTuning->primaryChannel, primaryCenterX)) {
                    lv_draw_rect_dsc_t markerDsc;
                    lv_draw_rect_dsc_init(&markerDsc);
                    markerDsc.bg_color = accentColor(model_.band);
                    markerDsc.bg_opa = LV_OPA_COVER;
                    markerDsc.radius = LV_RADIUS_CIRCLE;

                    lv_area_t markerArea{
                        .x1 = primaryCenterX - 8,
                        .y1 = railArea.y1 + (kRailHeightPx - kRailMarkerHeightPx) / 2,
                        .x2 = primaryCenterX + 8,
                        .y2 = railArea.y1 + (kRailHeightPx - kRailMarkerHeightPx) / 2
                            + kRailMarkerHeightPx - 1,
                    };
                    lv_draw_rect(layer, &markerDsc, &markerArea);
                }
            }
        }
    }

    std::vector<Bubble> bubbles = model_.bubbles;
    std::sort(bubbles.begin(), bubbles.end(), [](const Bubble& a, const Bubble& b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }

        const uint64_t aAge = a.freshestAgeMs.value_or(0);
        const uint64_t bAge = b.freshestAgeMs.value_or(0);
        if (aAge != bAge) {
            return aAge > bAge;
        }

        return a.count < b.count;
    });

    for (const Bubble& bubble : bubbles) {
        int centerX = 0;
        if (!channelXCenter(channels, plotArea, bubble.channel, centerX)) {
            continue;
        }

        const int centerY = bubbleY(plotArea, bubble.rssiBucketDbm);
        const int radius = bubbleRadius(bubble.count);
        const lv_opa_t opacity = bubbleOpacity(bubble.freshestAgeMs);

        lv_draw_rect_dsc_t bubbleDsc;
        lv_draw_rect_dsc_init(&bubbleDsc);
        bubbleDsc.radius = LV_RADIUS_CIRCLE;
        bubbleDsc.bg_color = accentColor(model_.band);
        bubbleDsc.border_color = kIncidentalColor;

        switch (bubble.kind) {
            case BubbleKind::Direct:
                bubbleDsc.bg_opa = opacity;
                bubbleDsc.border_width = 0;
                break;
            case BubbleKind::Incidental:
                bubbleDsc.bg_opa = LV_OPA_TRANSP;
                bubbleDsc.border_opa = opacity;
                bubbleDsc.border_width = 2;
                break;
            case BubbleKind::Mixed:
                bubbleDsc.bg_opa = opacity;
                bubbleDsc.border_opa = opacity;
                bubbleDsc.border_width = 2;
                break;
        }

        lv_area_t bubbleArea{
            .x1 = centerX - radius,
            .y1 = centerY - radius,
            .x2 = centerX + radius,
            .y2 = centerY + radius,
        };
        lv_draw_rect(layer, &bubbleDsc, &bubbleArea);
    }

    const int labelTop = plotArea.y2 + kLabelTopPaddingPx;
    int lastLabelRight = std::numeric_limits<int>::min();
    for (const int channel : channels) {
        int centerX = 0;
        if (!channelXCenter(channels, plotArea, channel, centerX)) {
            continue;
        }

        const std::string label = std::to_string(channel);
        lv_point_t textSize;
        lv_text_get_size(
            &textSize, label.c_str(), channelMapLabelFont(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const int labelLeft = centerX - textSize.x / 2;
        const int labelRight = labelLeft + textSize.x - 1;
        if (labelLeft <= lastLabelRight + 4) {
            continue;
        }

        lastLabelRight = labelRight;
        drawLabel(layer, std::to_string(channel), centerX, labelTop, kLabelColor);
    }
}

} // namespace Ui
} // namespace DirtSim
