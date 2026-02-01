#include "DuckStopButton.h"
#include "ui/controls/duck_img.h"
#include "ui/rendering/FractalAnimator.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kMinLabelHeight = 18;
constexpr int kInnerPadding = 6;
constexpr int kCornerRadius = 12;
} // namespace

DuckStopButton::DuckStopButton(
    lv_obj_t* parent,
    FractalAnimator& fractalAnimator,
    int width,
    int height,
    const char* labelText)
    : fractalAnimator_(fractalAnimator), width_(width), height_(height)
{
    createButton(parent, labelText);
}

DuckStopButton::~DuckStopButton()
{
    fractalAnimator_.parkIfParent(button_);
}

void DuckStopButton::createButton(lv_obj_t* parent, const char* labelText)
{
    button_ = lv_btn_create(parent);
    lv_obj_set_size(button_, width_, height_);
    lv_obj_set_style_radius(button_, kCornerRadius, 0);
    lv_obj_set_style_bg_opa(button_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_, 2, 0);
    lv_obj_set_style_border_color(button_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(button_, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(button_, 0, 0);
    lv_obj_set_style_pad_all(button_, 0, 0);
    lv_obj_clear_flag(button_, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle press feedback.
    lv_obj_set_style_transform_width(button_, -4, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(button_, -4, LV_STATE_PRESSED);

    fractalAnimator_.attachTo(button_, width_, height_);

    overlay_ = lv_obj_create(button_);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(overlay_);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_40, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, kCornerRadius, 0);
    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);

    content_ = lv_obj_create(button_);
    lv_obj_set_size(content_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_pad_row(content_, 2, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_CLICKABLE);

    duckImage_ = lv_image_create(content_);
    lv_image_set_src(duckImage_, &duck_img);

    showLabel_ = labelText && height_ >= (DUCK_IMG_HEIGHT / 2 + kMinLabelHeight);
    if (showLabel_) {
        label_ = lv_label_create(content_);
        lv_label_set_text(label_, labelText);
        lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label_, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    }

    updateDuckScale();

    // FractalAnimator registers a delete callback on the canvas itself.
}

void DuckStopButton::updateDuckScale()
{
    if (!duckImage_) {
        return;
    }

    const int labelSpace = showLabel_ ? kMinLabelHeight : 0;
    const int maxWidth = std::max(0, width_ - kInnerPadding * 2);
    const int maxHeight = std::max(0, height_ - kInnerPadding * 2 - labelSpace);

    const float scaleX = static_cast<float>(maxWidth) / static_cast<float>(DUCK_IMG_WIDTH);
    const float scaleY = static_cast<float>(maxHeight) / static_cast<float>(DUCK_IMG_HEIGHT);
    float scale = std::min(scaleX, scaleY);
    scale = std::max(0.35f, std::min(scale, 1.0f));

    const int32_t lvScale = static_cast<int32_t>(scale * 256.0f);
    lv_image_set_scale(duckImage_, lvScale);
}

} // namespace Ui
} // namespace DirtSim
