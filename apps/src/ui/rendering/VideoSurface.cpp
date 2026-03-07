#include "VideoSurface.h"

#include "core/LoggingChannels.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

VideoSurface::~VideoSurface()
{
    cleanup();
}

void VideoSurface::setCanvasCreatedCallback(CanvasCreatedCallback callback)
{
    canvasCreatedCallback_ = callback;
}

void VideoSurface::present(const ScenarioVideoFrame& frame, lv_obj_t* parent)
{
    if (frame.width == 0 || frame.height == 0) {
        return;
    }

    const size_t expectedBytes = static_cast<size_t>(frame.width) * frame.height * 2u;
    if (frame.pixels.size() != expectedBytes) {
        return;
    }

    // Detect container resize.
    lv_obj_update_layout(parent);
    const int32_t containerWidth = lv_obj_get_width(parent);
    const int32_t containerHeight = lv_obj_get_height(parent);

    const bool dimensionsChanged = (frame.width != canvasWidth_ || frame.height != canvasHeight_);
    const bool containerResized =
        (std::abs(containerWidth - lastContainerWidth_) > 2
         || std::abs(containerHeight - lastContainerHeight_) > 2);

    if (!canvas_ || dimensionsChanged || containerResized) {
        cleanup();
        initializeCanvas(parent, frame.width, frame.height);
        if (!canvas_) {
            return;
        }
    }

    // 1:1 RGB565 -> ARGB8888 blit at native resolution.
    uint32_t* pixels = reinterpret_cast<uint32_t*>(canvasBuffer_.data());
    const size_t pixelCount = static_cast<size_t>(canvasWidth_) * canvasHeight_;
    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t byteOffset = i * 2;
        const uint16_t rgb565 = static_cast<uint16_t>(
            std::to_integer<uint8_t>(frame.pixels[byteOffset])
            | (static_cast<uint16_t>(std::to_integer<uint8_t>(frame.pixels[byteOffset + 1])) << 8));
        pixels[i] = rgb565ToArgb8888(rgb565);
    }

    lv_obj_invalidate(canvas_);
}

void VideoSurface::cleanup()
{
    if (canvas_) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }
    canvasBuffer_.clear();
    canvasWidth_ = 0;
    canvasHeight_ = 0;
    lastContainerWidth_ = 0;
    lastContainerHeight_ = 0;
}

lv_obj_t* VideoSurface::getCanvas() const
{
    return canvas_;
}

uint32_t VideoSurface::getCanvasWidth() const
{
    return canvasWidth_;
}

uint16_t VideoSurface::getCanvasHeight() const
{
    return canvasHeight_;
}

const uint8_t* VideoSurface::getCanvasBuffer() const
{
    return canvasBuffer_.data();
}

void VideoSurface::initializeCanvas(lv_obj_t* parent, uint16_t frameWidth, uint16_t frameHeight)
{
    lv_obj_update_layout(parent);
    const int32_t containerWidth = lv_obj_get_width(parent);
    const int32_t containerHeight = lv_obj_get_height(parent);

    if (containerWidth <= 0 || containerHeight <= 0) {
        return;
    }

    lastContainerWidth_ = containerWidth;
    lastContainerHeight_ = containerHeight;
    canvasWidth_ = frameWidth;
    canvasHeight_ = frameHeight;

    // Allocate buffer at native frame resolution (ARGB8888).
    const size_t bufferSize = static_cast<size_t>(canvasWidth_) * canvasHeight_ * 4;
    canvasBuffer_.resize(bufferSize, 0);

    canvas_ = lv_canvas_create(parent);
    if (!canvas_) {
        canvasBuffer_.clear();
        canvasWidth_ = 0;
        canvasHeight_ = 0;
        return;
    }

    lv_canvas_set_buffer(
        canvas_, canvasBuffer_.data(), canvasWidth_, canvasHeight_, LV_COLOR_FORMAT_ARGB8888);

    // LVGL transform scaling to fit canvas to container (same pattern as CellRenderer).
    const double scaleX = static_cast<double>(containerWidth) / canvasWidth_;
    const double scaleY = static_cast<double>(containerHeight) / canvasHeight_;
    const double scale = std::min(scaleX, scaleY);

    const int lvglScaleX = static_cast<int>(scale * 256);
    const int lvglScaleY = static_cast<int>(scale * 256);

    lv_obj_set_style_transform_pivot_x(canvas_, 0, 0);
    lv_obj_set_style_transform_pivot_y(canvas_, 0, 0);
    lv_obj_set_style_transform_scale_x(canvas_, lvglScaleX, 0);
    lv_obj_set_style_transform_scale_y(canvas_, lvglScaleY, 0);

    // Center in container.
    const double scaledWidth = canvasWidth_ * scale;
    const double scaledHeight = canvasHeight_ * scale;
    const int32_t offsetX = static_cast<int32_t>((containerWidth - scaledWidth) / 2);
    const int32_t offsetY = static_cast<int32_t>((containerHeight - scaledHeight) / 2);
    lv_obj_set_pos(canvas_, offsetX, offsetY);

    LOG_INFO(
        Render,
        "VideoSurface: Initialized canvas {}x{} (native), scaling {:.2f}x, offset ({}, {})",
        canvasWidth_,
        canvasHeight_,
        scale,
        offsetX,
        offsetY);

    if (canvasCreatedCallback_) {
        canvasCreatedCallback_(canvas_);
    }
}

uint32_t VideoSurface::rgb565ToArgb8888(uint16_t value)
{
    const uint8_t red5 = static_cast<uint8_t>((value >> 11) & 0x1F);
    const uint8_t green6 = static_cast<uint8_t>((value >> 5) & 0x3F);
    const uint8_t blue5 = static_cast<uint8_t>(value & 0x1F);

    const uint8_t red8 = static_cast<uint8_t>((red5 << 3) | (red5 >> 2));
    const uint8_t green8 = static_cast<uint8_t>((green6 << 2) | (green6 >> 4));
    const uint8_t blue8 = static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2));

    return 0xFF000000u | (static_cast<uint32_t>(red8) << 16) | (static_cast<uint32_t>(green8) << 8)
        | static_cast<uint32_t>(blue8);
}

} // namespace Ui
} // namespace DirtSim
