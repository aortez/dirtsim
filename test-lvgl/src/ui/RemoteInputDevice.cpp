#include "RemoteInputDevice.h"
#include "core/LoggingChannels.h"

namespace DirtSim {
namespace Ui {

RemoteInputDevice::RemoteInputDevice(_lv_display_t* display)
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
    mouseX_ = static_cast<int32_t>(x);
    mouseY_ = static_cast<int32_t>(y);
    spdlog::trace("RemoteInputDevice: Position updated to ({}, {})", mouseX_, mouseY_);
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
