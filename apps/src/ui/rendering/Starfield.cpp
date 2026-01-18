#include "Starfield.h"
#include "core/LoggingChannels.h"
#include <algorithm>
#include <cmath>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kMaxStars = 400;
constexpr int kMinStars = 80;
constexpr int kPixelsPerStar = 1800;
constexpr float kMaxSpeed = 70.0f;
constexpr float kMinSpeed = 15.0f;
constexpr float kMaxTwinkleSpeed = 3.5f;
constexpr float kMinTwinkleSpeed = 0.6f;
constexpr float kTwoPi = 6.2831853f;
constexpr double kFrameIntervalSeconds = 1.0 / 30.0;
constexpr double kMaxDeltaSeconds = 0.1;

float randomFloat(std::mt19937& rng, float minValue, float maxValue)
{
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(rng);
}

int randomInt(std::mt19937& rng, int minValue, int maxValue)
{
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
}
} // namespace

Starfield::Starfield(lv_obj_t* parent, int width, int height)
    : parent_(parent), width_(width), height_(height), rng_(std::random_device{}())
{
    if (!parent_ || width_ <= 0 || height_ <= 0) {
        LOG_ERROR(Render, "Starfield requires a valid parent and size");
        return;
    }

    canvas_ = lv_canvas_create(parent_);
    if (!canvas_) {
        LOG_ERROR(Render, "Starfield failed to create canvas");
        return;
    }

    lv_obj_set_size(canvas_, width_, height_);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_set_style_bg_opa(canvas_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas_, 0, 0);
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);

    const size_t bufferSize = LV_CANVAS_BUF_SIZE(width, height, 32, 64);
    canvasBuffer_ = static_cast<lv_color_t*>(lv_malloc(bufferSize));
    if (!canvasBuffer_) {
        LOG_ERROR(Render, "Starfield failed to allocate canvas buffer");
        lv_obj_del(canvas_);
        canvas_ = nullptr;
        return;
    }

    lv_canvas_set_buffer(canvas_, canvasBuffer_, width_, height_, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(canvas_, lv_color_hex(0x000000), LV_OPA_COVER);
    initStars();
}

Starfield::~Starfield()
{
    if (canvas_) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }

    if (canvasBuffer_) {
        lv_free(canvasBuffer_);
        canvasBuffer_ = nullptr;
    }
}

void Starfield::setVisible(bool visible)
{
    if (!canvas_) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool Starfield::isVisible() const
{
    if (!canvas_) {
        return false;
    }

    return !lv_obj_has_flag(canvas_, LV_OBJ_FLAG_HIDDEN);
}

void Starfield::update()
{
    if (!canvas_ || !canvasBuffer_ || !isVisible()) {
        return;
    }

    maybeResize();

    const auto now = std::chrono::steady_clock::now();
    if (lastUpdate_.time_since_epoch().count() == 0) {
        lastUpdate_ = now;
        return;
    }

    double deltaSeconds = std::chrono::duration<double>(now - lastUpdate_).count();
    if (deltaSeconds < kFrameIntervalSeconds) {
        return;
    }

    lastUpdate_ = now;
    deltaSeconds = std::min(deltaSeconds, kMaxDeltaSeconds);

    updateStars(static_cast<float>(deltaSeconds));
    drawStars();
}

void Starfield::resize(int width, int height)
{
    if (!canvas_ || width <= 0 || height <= 0) {
        return;
    }

    const size_t bufferSize = LV_CANVAS_BUF_SIZE(width, height, 32, 64);
    auto* newBuffer = static_cast<lv_color_t*>(lv_malloc(bufferSize));
    if (!newBuffer) {
        LOG_ERROR(Render, "Starfield failed to allocate resize buffer");
        return;
    }

    if (canvasBuffer_) {
        lv_free(canvasBuffer_);
    }
    canvasBuffer_ = newBuffer;

    width_ = width;
    height_ = height;
    lv_obj_set_size(canvas_, width_, height_);

    lv_canvas_set_buffer(canvas_, canvasBuffer_, width_, height_, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(canvas_, lv_color_hex(0x000000), LV_OPA_COVER);
    initStars();
}

void Starfield::maybeResize()
{
    if (!parent_) {
        return;
    }

    const int newWidth = lv_obj_get_width(parent_);
    const int newHeight = lv_obj_get_height(parent_);
    if (newWidth <= 0 || newHeight <= 0) {
        return;
    }

    if (newWidth != width_ || newHeight != height_) {
        resize(newWidth, newHeight);
    }
}

void Starfield::initStars()
{
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    const int targetCount = (width_ * height_) / kPixelsPerStar;
    starCount_ = std::clamp(targetCount, kMinStars, kMaxStars);
    stars_.clear();
    stars_.reserve(starCount_);

    for (int i = 0; i < starCount_; ++i) {
        Star star;
        resetStar(star, true);
        stars_.push_back(star);
    }
}

void Starfield::resetStar(Star& star, bool randomY)
{
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    star.x = randomFloat(rng_, 0.0f, static_cast<float>(width_ - 1));
    if (randomY) {
        star.y = randomFloat(rng_, 0.0f, static_cast<float>(height_ - 1));
    }
    else {
        const float maxOffset = std::max(1.0f, static_cast<float>(height_) * 0.2f);
        star.y = -randomFloat(rng_, 1.0f, maxOffset);
    }
    star.speed = randomFloat(rng_, kMinSpeed, kMaxSpeed);
    star.twinklePhase = randomFloat(rng_, 0.0f, kTwoPi);
    star.twinkleSpeed = randomFloat(rng_, kMinTwinkleSpeed, kMaxTwinkleSpeed);
    star.brightness = static_cast<uint8_t>(randomInt(rng_, 160, 255));
    star.size = static_cast<uint8_t>(randomInt(rng_, 1, 2));
}

void Starfield::updateStars(float deltaSeconds)
{
    for (auto& star : stars_) {
        star.y += star.speed * deltaSeconds;
        star.twinklePhase += star.twinkleSpeed * deltaSeconds;
        if (star.twinklePhase > kTwoPi) {
            star.twinklePhase -= kTwoPi;
        }

        if (star.y >= static_cast<float>(height_)) {
            resetStar(star, false);
        }
    }
}

void Starfield::drawStars()
{
    lv_canvas_fill_bg(canvas_, lv_color_hex(0x000000), LV_OPA_COVER);

    for (const auto& star : stars_) {
        const int x = static_cast<int>(star.x);
        const int y = static_cast<int>(star.y);
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            continue;
        }

        const float twinkle = 0.7f + 0.3f * std::sin(star.twinklePhase);
        int intensity = static_cast<int>(star.brightness * twinkle);
        intensity = std::clamp(intensity, 0, 255);
        lv_color_t color = lv_color_make(
            static_cast<uint8_t>(intensity),
            static_cast<uint8_t>(intensity),
            static_cast<uint8_t>(intensity));

        const int size = std::max(1, static_cast<int>(star.size));
        for (int dy = 0; dy < size; ++dy) {
            for (int dx = 0; dx < size; ++dx) {
                const int px = x + dx;
                const int py = y + dy;
                if (px >= 0 && py >= 0 && px < width_ && py < height_) {
                    lv_canvas_set_px(canvas_, px, py, color, LV_OPA_COVER);
                }
            }
        }
    }
}

} // namespace Ui
} // namespace DirtSim
