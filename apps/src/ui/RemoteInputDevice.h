#pragma once

#include <lvgl/lvgl.h>
#include <mutex>

namespace DirtSim {
namespace Ui {

/**
 * @brief Remote input device for WebSocket-forwarded mouse events.
 *
 * This class bridges remote mouse events (from WebRTC browser clients) to LVGL's
 * input device system, allowing remote control of all LVGL widgets (buttons,
 * sliders, toggles, etc.) as if the mouse were local.
 *
 * Architecture:
 * - Browser captures mouse events on video stream
 * - WebSocket sends MouseDown/Move/Up commands to UI
 * - State machine handlers call updatePosition() and updatePressed()
 * - LVGL polls readCallback() during lv_timer_handler()
 * - Widgets receive events as if from local mouse
 *
 * Ownership:
 * - StateMachine owns the RemoteInputDevice instance
 * - Created during StateMachine construction
 * - Lives until StateMachine destruction
 *
 * Thread safety: Mutex protects state updates from concurrent access.
 */
class RemoteInputDevice {
public:
    /**
     * @brief Construct and register the remote input device with LVGL.
     * Creates an lv_indev_t of type POINTER and registers the read callback.
     * @param display The LVGL display to associate the input device with.
     */
    explicit RemoteInputDevice(_lv_display_t* display);

    ~RemoteInputDevice();

    void updatePosition(int x, int y);

    void updatePressed(bool pressed);

    void getPosition(int32_t* x, int32_t* y) const;

    bool isPressed() const;

    lv_indev_t* getIndev() const { return indev_; }

private:
    /**
     * @brief LVGL read callback (C function pointer required by LVGL).
     *
     * LVGL calls this during lv_timer_handler() to poll input state.
     * We use lv_indev_set_user_data() to pass 'this' pointer through.
     */
    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

    // Mouse state (protected by mutex).
    mutable std::mutex stateMutex_;
    int32_t mouseX_ = 0;
    int32_t mouseY_ = 0;
    bool mousePressed_ = false;

    // LVGL input device handle.
    lv_indev_t* indev_ = nullptr;

    // Display reference for coordinate transformation.
    _lv_display_t* display_ = nullptr;
};

} // namespace Ui
} // namespace DirtSim
