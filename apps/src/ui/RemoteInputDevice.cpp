#include "RemoteInputDevice.h"
#include "core/LoggingChannels.h"

namespace DirtSim {
namespace Ui {

RemoteInputDevice::RemoteInputDevice(_lv_display_t* display) : display_(display)
{
    // Create LVGL input device.
    indev_ = lv_indev_create();
    if (!indev_) {
        SLOG_ERROR("Failed to create lv_indev_t");
        return;
    }

    // Configure as pointer device (mouse/touchpad).
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);

    // Set read callback.
    lv_indev_set_read_cb(indev_, readCallback);

    // Pass 'this' pointer through user_data so callback can access instance.
    lv_indev_set_user_data(indev_, this);

    // Associate with display.
    lv_indev_set_display(indev_, display);

    SLOG_INFO("Initialized remote pointer input device");
}

RemoteInputDevice::~RemoteInputDevice()
{
    // LVGL handles indev cleanup automatically when display is destroyed.
    SLOG_INFO("Destroyed");
}

void RemoteInputDevice::updatePosition(int x, int y)
{
    std::lock_guard<std::mutex> lock(stateMutex_);

    // Remote coordinates arrive in logical (rotated) space from the video stream.
    // LVGL's indev_pointer_proc() expects raw (physical) coordinates and will
    // apply the rotation transform. We must apply the INVERSE transform here
    // so the coordinates end up correct after LVGL's processing.
    //
    // See: lvgl/src/indev/lv_indev.c:indev_pointer_proc()
    if (display_ != nullptr) {
        lv_display_rotation_t rotation = lv_display_get_rotation(display_);

        // LVGL's indev_pointer_proc uses disp->hor_res and disp->ver_res (native/physical).
        // The API functions return logical (post-rotation) dimensions, so we reverse the swap.
        int32_t hor_res;
        int32_t ver_res;
        if (rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270) {
            // For 90/270, API swaps them: logical_hor = native_ver, logical_ver = native_hor.
            hor_res = lv_display_get_vertical_resolution(display_);
            ver_res = lv_display_get_horizontal_resolution(display_);
        }
        else {
            hor_res = lv_display_get_horizontal_resolution(display_);
            ver_res = lv_display_get_vertical_resolution(display_);
        }

        int32_t rawX = x;
        int32_t rawY = y;

        switch (rotation) {
            case LV_DISPLAY_ROTATION_90:
                // LVGL forward: (x,y) -> (ver_res - y - 1, x)
                // Inverse: (lx, ly) -> (ly, ver_res - lx - 1)
                rawX = y;
                rawY = ver_res - x - 1;
                break;

            case LV_DISPLAY_ROTATION_180:
                // LVGL forward: (x,y) -> (hor_res - x - 1, ver_res - y - 1)
                // Inverse is the same (self-inverse).
                rawX = hor_res - x - 1;
                rawY = ver_res - y - 1;
                break;

            case LV_DISPLAY_ROTATION_270:
                // LVGL forward: (x,y) -> (y, hor_res - x - 1)
                // Inverse: (lx, ly) -> (hor_res - ly - 1, lx)
                rawX = hor_res - y - 1;
                rawY = x;
                break;

            case LV_DISPLAY_ROTATION_0:
            default:
                // No transformation needed.
                break;
        }

        mouseX_ = rawX;
        mouseY_ = rawY;
        // Treat pointer movement as activity so auto-shrink does not trigger.
        lv_display_trigger_activity(display_);
        spdlog::trace(
            "RemoteInputDevice: logical({}, {}) -> raw({}, {}) [rot={}]",
            x,
            y,
            rawX,
            rawY,
            static_cast<int>(rotation));
    }
    else {
        mouseX_ = static_cast<int32_t>(x);
        mouseY_ = static_cast<int32_t>(y);
        spdlog::trace("RemoteInputDevice: Position updated to ({}, {})", mouseX_, mouseY_);
    }
}

void RemoteInputDevice::updatePressed(bool pressed)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    mousePressed_ = pressed;
    spdlog::trace("RemoteInputDevice: Pressed state updated to {}", pressed);
}

void RemoteInputDevice::getPosition(int32_t* x, int32_t* y) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    *x = mouseX_;
    *y = mouseY_;
}

bool RemoteInputDevice::isPressed() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return mousePressed_;
}

void RemoteInputDevice::readCallback(lv_indev_t* indev, lv_indev_data_t* data)
{
    // Extract 'this' pointer from user_data.
    auto* self = static_cast<RemoteInputDevice*>(lv_indev_get_user_data(indev));
    if (!self) {
        spdlog::error("RemoteInputDevice::readCallback: null user_data");
        return;
    }

    // Read current mouse state (thread-safe).
    self->getPosition(&data->point.x, &data->point.y);
    data->state = self->isPressed() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace Ui
} // namespace DirtSim
