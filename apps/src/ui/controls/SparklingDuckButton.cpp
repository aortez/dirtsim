#include "SparklingDuckButton.h"
#include "core/LoggingChannels.h"
#include "duck_img.h"
#include <cmath>

namespace DirtSim {
namespace Ui {

// Animation constants.
static constexpr float PI = 3.14159265359f;
static constexpr float ORBIT_SPEED = 0.04f;           // Radians per frame (normal orbit).
static constexpr float SPIN_UP_ACCELERATION = 0.006f; // Angular acceleration during spin-up.
static constexpr float RADIUS_GROWTH_RATE = 1.5f;     // Pixels per frame during spin-up.
static constexpr float FLY_OUT_SPEED = 3.5f;          // Radial pixels per frame when flying.
static constexpr float FLY_OUT_ANGULAR_MULT = 1.5f;   // Angular velocity multiplier when flying.
static constexpr float FADE_RATE = 3.0f;              // Opacity units per frame when flying out.
static constexpr float MAX_FLY_RADIUS = 250.0f;       // Reset sparkle when it gets this far.

SparklingDuckButton::SparklingDuckButton(lv_obj_t* parent, ClickCallback onClick)
    : onClick_(std::move(onClick))
{
    createButton(parent);
    createSparkles(parent);

    // Initialize all sparkle states.
    for (int i = 0; i < NUM_SPARKLES; i++) {
        initSparkleState(i);
    }

    LOG_INFO(Controls, "SparklingDuckButton created with {} sparkles", NUM_SPARKLES);
}

SparklingDuckButton::~SparklingDuckButton()
{
    // LVGL objects are cleaned up when parent is deleted.
    LOG_INFO(Controls, "SparklingDuckButton destroyed");
}

void SparklingDuckButton::initSparkleState(int index)
{
    auto& state = sparkleStates_[index];

    // Each sparkle has a different phase offset around the orbit.
    float phaseOffset = (2.0f * PI * index) / NUM_SPARKLES;

    // Base orbit radius with slight variation per sparkle for visual depth.
    float baseRadius = (BUTTON_WIDTH / 2.0f) - 8.0f;
    float radiusVariation = static_cast<float>(index % 4) * 2.0f;

    state.targetRadius = baseRadius + radiusVariation;
    state.radius = state.targetRadius;
    state.angle = phaseOffset;
    state.targetAngularVelocity = ORBIT_SPEED;
    state.angularVelocity = ORBIT_SPEED;
    state.radialVelocity = 0.0f;
    state.phase = SparklePhase::ORBITING;
    state.opacity = 200.0f;
}

void SparklingDuckButton::createButton(lv_obj_t* parent)
{
    // Create the main button.
    button_ = lv_btn_create(parent);
    lv_obj_set_size(button_, BUTTON_WIDTH, BUTTON_HEIGHT);
    lv_obj_center(button_);

    // Invisible button - duck floats directly on fractal background.
    lv_obj_set_style_bg_opa(button_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button_, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button_, 0, LV_PART_MAIN);

    // Pressed state: subtle shrink effect on the duck.
    lv_obj_set_style_transform_width(button_, -4, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(button_, -4, LV_STATE_PRESSED);

    // Dark circle background for contrast against fractal.
    static constexpr int BG_CIRCLE_SIZE = 145;
    duckBackground_ = lv_obj_create(button_);
    lv_obj_set_size(duckBackground_, BG_CIRCLE_SIZE, BG_CIRCLE_SIZE);
    lv_obj_center(duckBackground_);
    lv_obj_set_style_radius(duckBackground_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(duckBackground_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(duckBackground_, LV_OPA_70, 0); // 70% opacity.
    lv_obj_set_style_border_width(duckBackground_, 0, 0);
    lv_obj_remove_flag(duckBackground_, LV_OBJ_FLAG_CLICKABLE);

    // Duck image (centered, overlaps background circle).
    duckImage_ = lv_image_create(button_);
    lv_image_set_src(duckImage_, &duck_img);
    lv_image_set_scale(duckImage_, 307); // 120% of original size (256 = 100%).
    lv_obj_center(duckImage_);

    // Event handlers.
    lv_obj_set_user_data(button_, this);
    lv_obj_add_event_cb(button_, onButtonClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(button_, onButtonPressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(button_, onButtonReleased, LV_EVENT_RELEASED, nullptr);
}

void SparklingDuckButton::createSparkles(lv_obj_t* parent)
{
    // Create sparkle labels that orbit around the button.
    // Using simple star characters for sparkles.
    const char* sparkleChars[] = { "*", "+", ".", "*", "+", ".", "*", "+",
                                   "*", "+", ".", "*", "+", ".", "*", "+" };

    for (int i = 0; i < NUM_SPARKLES; i++) {
        sparkles_[i] = lv_label_create(parent);
        lv_label_set_text(sparkles_[i], sparkleChars[i % 8]);

        // Cycle through white, yellow, and gold sparkles.
        lv_color_t color;
        switch (i % 3) {
            case 0:
                color = lv_color_hex(0xFFFFFF);
                break; // White.
            case 1:
                color = lv_color_hex(0xFFFF00);
                break; // Yellow.
            default:
                color = lv_color_hex(0xFFD700);
                break; // Gold.
        }
        lv_obj_set_style_text_color(sparkles_[i], color, 0);

        // Vary the font size for depth effect.
        const lv_font_t* font = (i % 4 == 0) ? &lv_font_montserrat_20
            : (i % 4 == 1)                   ? &lv_font_montserrat_18
            : (i % 4 == 2)                   ? &lv_font_montserrat_16
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
    updateDuckColorCycle();
}

void SparklingDuckButton::updateSparklePositions()
{
    if (!button_) return;

    // Get button center position.
    lv_coord_t btnX = lv_obj_get_x(button_) + BUTTON_WIDTH / 2;
    lv_coord_t btnY = lv_obj_get_y(button_) + BUTTON_HEIGHT / 2;

    for (int i = 0; i < NUM_SPARKLES; i++) {
        if (!sparkles_[i]) continue;

        switch (sparkleStates_[i].phase) {
            case SparklePhase::ORBITING:
                updateSparkleOrbiting(i, btnX, btnY);
                break;
            case SparklePhase::SPINNING_UP:
                updateSparkleSpinningUp(i, btnX, btnY);
                break;
            case SparklePhase::FLYING_OUT:
                updateSparkleFlyingOut(i, btnX, btnY);
                break;
        }
    }
}

void SparklingDuckButton::updateSparkleOrbiting(int i, lv_coord_t btnX, lv_coord_t btnY)
{
    auto& state = sparkleStates_[i];

    // Add slight wobble to radius.
    float wobble = std::sin(frameCount_ * 0.06f + i) * 3.0f;
    float currentRadius = state.targetRadius + wobble;

    // Update angle.
    state.angle += state.angularVelocity;

    // Calculate position on ellipse (slightly flattened for visual interest).
    float x = btnX + currentRadius * std::cos(state.angle);
    float y = btnY + (currentRadius * 0.85f) * std::sin(state.angle);

    lv_obj_set_pos(sparkles_[i], static_cast<lv_coord_t>(x) - 8, static_cast<lv_coord_t>(y) - 8);

    // Pulsing opacity for twinkle effect.
    float opacityPhase = std::sin(frameCount_ * 0.1f + i * 0.7f);
    lv_opa_t opacity = static_cast<lv_opa_t>(180 + 75 * opacityPhase);
    lv_obj_set_style_text_opa(sparkles_[i], opacity, 0);
}

void SparklingDuckButton::updateSparkleSpinningUp(int i, lv_coord_t btnX, lv_coord_t btnY)
{
    auto& state = sparkleStates_[i];

    // Accelerate angular velocity toward target.
    if (state.angularVelocity < state.targetAngularVelocity) {
        state.angularVelocity += SPIN_UP_ACCELERATION;
        if (state.angularVelocity > state.targetAngularVelocity) {
            state.angularVelocity = state.targetAngularVelocity;
        }
    }

    // Grow radius toward target.
    if (state.radius < state.targetRadius) {
        state.radius += RADIUS_GROWTH_RATE;
        if (state.radius > state.targetRadius) {
            state.radius = state.targetRadius;
        }
    }

    // Update angle.
    state.angle += state.angularVelocity;

    // Calculate position.
    float x = btnX + state.radius * std::cos(state.angle);
    float y = btnY + (state.radius * 0.85f) * std::sin(state.angle);

    lv_obj_set_pos(sparkles_[i], static_cast<lv_coord_t>(x) - 8, static_cast<lv_coord_t>(y) - 8);

    // Opacity increases as it spins up.
    float progress = state.radius / state.targetRadius;
    lv_opa_t opacity = static_cast<lv_opa_t>(50 + 200 * progress);
    lv_obj_set_style_text_opa(sparkles_[i], opacity, 0);
    state.opacity = opacity;

    // Check if ready to fly out (only if still pressed).
    if (isPressed_ && state.radius >= state.targetRadius
        && state.angularVelocity >= state.targetAngularVelocity) {
        state.phase = SparklePhase::FLYING_OUT;
        state.radialVelocity = FLY_OUT_SPEED;
        state.angularVelocity *= FLY_OUT_ANGULAR_MULT;
        state.opacity = 255.0f; // Full brightness on release.
    }
}

void SparklingDuckButton::updateSparkleFlyingOut(int i, lv_coord_t btnX, lv_coord_t btnY)
{
    auto& state = sparkleStates_[i];

    // Update position (spiral outward).
    state.radius += state.radialVelocity;
    state.angle += state.angularVelocity;

    // Gradually slow angular velocity as it flies out (looks more natural).
    state.angularVelocity *= 0.995f;

    // Calculate position.
    float x = btnX + state.radius * std::cos(state.angle);
    float y = btnY + (state.radius * 0.85f) * std::sin(state.angle);

    lv_obj_set_pos(sparkles_[i], static_cast<lv_coord_t>(x) - 8, static_cast<lv_coord_t>(y) - 8);

    // Fade out as it flies.
    state.opacity -= FADE_RATE;
    if (state.opacity < 0) state.opacity = 0;
    lv_obj_set_style_text_opa(sparkles_[i], static_cast<lv_opa_t>(state.opacity), 0);

    // Reset when faded out or too far.
    if (state.opacity <= 0 || state.radius > MAX_FLY_RADIUS) {
        if (isPressed_) {
            // Respawn at center for another wave.
            state.radius = 0;
            state.angularVelocity = 0;
            state.radialVelocity = 0;
            state.opacity = 50;
            state.phase = SparklePhase::SPINNING_UP;
            // Keep the angle to maintain orbital position continuity.
        }
        else {
            // Return to normal orbit.
            initSparkleState(i);
        }
    }
}

void SparklingDuckButton::updateDuckColorCycle()
{
    if (!duckImage_) return;

    if (isPressed_) {
        // Cycle through palette colors while pressed ("disco duck" effect).
        static constexpr int FRAMES_PER_COLOR = 4; // Change color every N frames.

        colorCycleCounter_++;
        if (colorCycleCounter_ >= FRAMES_PER_COLOR) {
            colorCycleCounter_ = 0;
            paletteIndex_ = (paletteIndex_ + 1) % NUM_PALETTE_COLORS;

            // Apply the recolor tint.
            lv_color_t color = lv_color_hex(DUCK_PALETTE[paletteIndex_]);
            lv_obj_set_style_img_recolor(duckImage_, color, 0);
            lv_obj_set_style_img_recolor_opa(duckImage_, LV_OPA_60, 0); // 60% tint intensity.
        }
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

void SparklingDuckButton::onButtonPressed(lv_event_t* e)
{
    auto* self = static_cast<SparklingDuckButton*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!self) return;

    LOG_DEBUG(Controls, "SparklingDuckButton pressed - starting starburst");
    self->isPressed_ = true;

    // Transition all orbiting sparkles to flying out.
    for (int i = 0; i < NUM_SPARKLES; i++) {
        auto& state = self->sparkleStates_[i];
        if (state.phase == SparklePhase::ORBITING) {
            state.phase = SparklePhase::FLYING_OUT;
            state.radialVelocity = FLY_OUT_SPEED;
            state.angularVelocity *= FLY_OUT_ANGULAR_MULT;
            state.opacity = 255.0f;
        }
    }
}

void SparklingDuckButton::onButtonReleased(lv_event_t* e)
{
    auto* self = static_cast<SparklingDuckButton*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!self) return;

    LOG_DEBUG(Controls, "SparklingDuckButton released - ending starburst");
    self->isPressed_ = false;

    // Reset duck color to normal (remove recolor tint).
    if (self->duckImage_) {
        lv_obj_set_style_img_recolor_opa(self->duckImage_, LV_OPA_TRANSP, 0);
    }
    self->colorCycleCounter_ = 0;
    self->paletteIndex_ = 0;

    // Sparkles will return to orbit when they complete their current animation.
}

} // namespace Ui
} // namespace DirtSim
