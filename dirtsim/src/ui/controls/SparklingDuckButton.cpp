#include "SparklingDuckButton.h"
#include "duck_img.h"
#include "core/LoggingChannels.h"
#include <cmath>

namespace DirtSim {
namespace Ui {

SparklingDuckButton::SparklingDuckButton(lv_obj_t* parent, ClickCallback onClick)
    : onClick_(std::move(onClick))
{
    createButton(parent);
    createSparkles(parent);
    LOG_INFO(Controls, "SparklingDuckButton created");
}

SparklingDuckButton::~SparklingDuckButton()
{
    // LVGL objects are cleaned up when parent is deleted.
    LOG_INFO(Controls, "SparklingDuckButton destroyed");
}

void SparklingDuckButton::createButton(lv_obj_t* parent)
{
    // Create the main button.
    button_ = lv_btn_create(parent);
    lv_obj_set_size(button_, BUTTON_WIDTH, BUTTON_HEIGHT);
    lv_obj_center(button_);

    // Style: semi-transparent with glowing border.
    lv_obj_set_style_bg_color(button_, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button_, LV_OPA_40, LV_PART_MAIN); // 40% opacity - see through.
    lv_obj_set_style_border_width(button_, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(button_, lv_color_hex(0xFFD700), LV_PART_MAIN); // Gold border.
    lv_obj_set_style_radius(button_, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button_, 25, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(button_, lv_color_hex(0xFFD700), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(button_, LV_OPA_60, LV_PART_MAIN);

    // Pressed state: slightly more opaque.
    lv_obj_set_style_bg_opa(button_, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button_, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(button_, -2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(button_, -2, LV_STATE_PRESSED);

    // Center the duck image in the button.
    lv_obj_set_layout(button_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(button_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Duck image (no text, just the duck).
    duckImage_ = lv_image_create(button_);
    lv_image_set_src(duckImage_, &duck_img);
    lv_image_set_scale(duckImage_, 230); // Scale to ~90% (256 = 100%).

    // Click handler.
    lv_obj_set_user_data(button_, this);
    lv_obj_add_event_cb(button_, onButtonClicked, LV_EVENT_CLICKED, nullptr);
}

void SparklingDuckButton::createSparkles(lv_obj_t* parent)
{
    // Create sparkle labels that orbit around the button.
    // Using simple star characters for sparkles.
    const char* sparkleChars[] = { "*", "+", ".", "*", "+", ".", "*", "+" };

    for (int i = 0; i < NUM_SPARKLES; i++) {
        sparkles_[i] = lv_label_create(parent);
        lv_label_set_text(sparkles_[i], sparkleChars[i % 8]);

        // Alternate between white and yellow sparkles.
        lv_color_t color = (i % 2 == 0) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xFFFF00);
        lv_obj_set_style_text_color(sparkles_[i], color, 0);

        // Vary the font size for depth effect.
        const lv_font_t* font = (i % 3 == 0) ? &lv_font_montserrat_20
                                             : (i % 3 == 1) ? &lv_font_montserrat_16
                                                            : &lv_font_montserrat_14;
        lv_obj_set_style_text_font(sparkles_[i], font, 0);

        // Add subtle shadow for glow effect.
        lv_obj_set_style_text_opa(sparkles_[i], LV_OPA_80, 0);
    }
}

void SparklingDuckButton::update()
{
    frameCount_++;
    updateSparklePositions();
}

void SparklingDuckButton::updateSparklePositions()
{
    if (!button_) return;

    // Get button center position.
    lv_coord_t btnX = lv_obj_get_x(button_) + BUTTON_WIDTH / 2;
    lv_coord_t btnY = lv_obj_get_y(button_) + BUTTON_HEIGHT / 2;

    // Orbit radius - tight around the duck.
    const float radiusX = BUTTON_WIDTH / 2 - 8;
    const float radiusY = BUTTON_HEIGHT / 2 - 8;

    // Animation speed (radians per frame).
    const float speed = 0.04f;

    for (int i = 0; i < NUM_SPARKLES; i++) {
        if (!sparkles_[i]) continue;

        // Each sparkle has a different phase offset.
        float phase = (2.0f * 3.14159f * i) / NUM_SPARKLES;

        // Add slight wobble to each sparkle's radius.
        float wobble = std::sin(frameCount_ * 0.06f + i) * 3.0f;

        // Calculate position on ellipse.
        float angle = frameCount_ * speed + phase;
        float x = btnX + (radiusX + wobble) * std::cos(angle);
        float y = btnY + (radiusY + wobble * 0.5f) * std::sin(angle);

        lv_obj_set_pos(sparkles_[i], static_cast<lv_coord_t>(x) - 8, static_cast<lv_coord_t>(y) - 8);

        // Pulsing opacity for twinkle effect.
        float opacityPhase = std::sin(frameCount_ * 0.1f + i * 0.7f);
        lv_opa_t opacity = static_cast<lv_opa_t>(180 + 75 * opacityPhase);
        lv_obj_set_style_text_opa(sparkles_[i], opacity, 0);
    }
}

void SparklingDuckButton::onButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<SparklingDuckButton*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (self && self->onClick_) {
        self->onClick_();
    }
}

} // namespace Ui
} // namespace DirtSim
