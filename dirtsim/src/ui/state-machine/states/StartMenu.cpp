#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/SimRun.h"
#include "ui/RemoteInputDevice.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/SparklingDuckButton.h"
#include "ui/rendering/JuliaFractal.h"
#include "ui/state-machine/StateMachine.h"
#include <lvgl/lvgl.h>
#include <lvgl/src/misc/lv_timer_private.h>
#include <nlohmann/json.hpp>

namespace DirtSim {
namespace Ui {
namespace State {

void StartMenu::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Connected to server, ready to start simulation");

    // Get main menu container (switches to menu screen).
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) return;

    lv_obj_t* container = uiManager->getMainMenuContainer();

    // Get display dimensions for full-screen fractal.
    lv_disp_t* disp = lv_disp_get_default();
    int windowWidth = lv_disp_get_hor_res(disp);
    int windowHeight = lv_disp_get_ver_res(disp);

    // Create Julia fractal background (allocated on heap, deleted in onExit).
    fractal_ = new JuliaFractal(container, windowWidth, windowHeight);
    LOG_INFO(State, "Created fractal background (event-driven rendering)");

    // Add resize event handler to container (catches window resize events).
    lv_obj_add_event_cb(container, onDisplayResized, LV_EVENT_SIZE_CHANGED, fractal_);
    LOG_INFO(State, "Added resize event handler");

    // Create animated sparkle-duck start button.
    startButton_ = std::make_unique<SparklingDuckButton>(container, [&sm]() {
        // Queue event for state machine to process (keep LVGL callback thin).
        sm.queueEvent(StartButtonClickedEvent{});
    });

    LOG_INFO(State, "Created sparkle-duck start button");

    // Create info panel in bottom-left corner.
    infoPanel_ = lv_obj_create(container);
    lv_obj_set_size(infoPanel_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(infoPanel_, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_set_style_pad_all(infoPanel_, 15, 0);
    lv_obj_set_style_bg_opa(infoPanel_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(infoPanel_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(infoPanel_, 2, 0);
    lv_obj_set_style_border_color(infoPanel_, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(infoPanel_, 8, 0);

    // Set flex layout for horizontal stacking (button left, info right).
    lv_obj_set_layout(infoPanel_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(infoPanel_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        infoPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(infoPanel_, 15, 0);

    // Create "Next Fractal" button (left side).
    nextFractalButton_ = lv_btn_create(infoPanel_);
    lv_obj_set_size(nextFractalButton_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(nextFractalButton_, 10, 0);
    lv_obj_set_user_data(nextFractalButton_, this);
    lv_obj_add_event_cb(nextFractalButton_, onNextFractalClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnLabel = lv_label_create(nextFractalButton_);
    lv_label_set_text(btnLabel, "Next Fractal");
    lv_obj_center(btnLabel);

    // Create info label (right side).
    infoLabel_ = lv_label_create(infoPanel_);
    lv_label_set_text(infoLabel_, "Loading fractal info...");
    lv_obj_set_style_text_color(infoLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(infoLabel_, &lv_font_montserrat_14, 0);

    LOG_INFO(State, "Created fractal info panel");

    // Create Quit button in top-left corner (same style as SimRunning).
    quitButton_ = lv_btn_create(container);
    lv_obj_set_size(quitButton_, 80, 40);
    lv_obj_align(quitButton_, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_set_style_bg_color(quitButton_, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_user_data(quitButton_, &sm);
    lv_obj_add_event_cb(quitButton_, onQuitButtonClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* quitLabel = lv_label_create(quitButton_);
    lv_label_set_text(quitLabel, "Quit");
    lv_obj_center(quitLabel);

    LOG_INFO(State, "Created Quit button");

    // Create touch debug label in top-right corner.
    touchDebugLabel_ = lv_label_create(container);
    lv_label_set_text(touchDebugLabel_, "Touch: ---, ---");
    lv_obj_align(touchDebugLabel_, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_obj_set_style_text_color(touchDebugLabel_, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(touchDebugLabel_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_opa(touchDebugLabel_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(touchDebugLabel_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(touchDebugLabel_, 8, 0);

    // Add touch event handler to container to track all touches.
    lv_obj_add_event_cb(container, onTouchEvent, LV_EVENT_PRESSING, touchDebugLabel_);
    lv_obj_add_event_cb(container, onTouchEvent, LV_EVENT_PRESSED, touchDebugLabel_);
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);

    LOG_INFO(State, "Created touch debug label");
}

void StartMenu::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting");

    // Clean up sparkle button.
    startButton_.reset();

    // Clean up fractal.
    if (fractal_) {
        // IMPORTANT: Remove the resize event handler before deleting the fractal.
        // This prevents use-after-free if a resize event occurs after exit.
        auto* uiManager = sm.getUiComponentManager();
        if (uiManager) {
            lv_obj_t* container = uiManager->getMainMenuContainer();
            if (container) {
                lv_obj_remove_event_cb(container, onDisplayResized);
                LOG_INFO(State, "Removed resize event handler");
            }
        }

        delete fractal_;
        fractal_ = nullptr;
        LOG_INFO(State, "Cleaned up fractal");
    }

    // Screen switch will clean up other widgets automatically.
}

void StartMenu::updateAnimations()
{
    // Update sparkle button animation.
    if (startButton_) {
        startButton_->update();
    }

    if (fractal_) {
        fractal_->update();

        // Update info label with current fractal parameters (~1/sec to reduce overhead).
        if (infoLabel_) {
            labelUpdateCounter_++;
            if (labelUpdateCounter_ >= 60) { // Update ~1/sec at 60fps.
                labelUpdateCounter_ = 0;

                const char* regionName = fractal_->getRegionName();

                // Get all iteration values atomically to prevent race conditions.
                int minIter, currentIter, maxIter;
                fractal_->getIterationInfo(minIter, currentIter, maxIter);

                double fps = fractal_->getDisplayFps();

                // Periodic logging every 100 frames to track iteration values.
                updateFrameCount_++;
                if (updateFrameCount_ >= 100) {
                    LOG_INFO(
                        State,
                        "Fractal info - Region: {}, Iterations: [{}-{}], current: {}, FPS: {:.1f}",
                        regionName,
                        minIter,
                        maxIter,
                        currentIter,
                        fps);
                    updateFrameCount_ = 0;
                }

                // Build simple info text: region name and FPS.
                char infoText[128];
                snprintf(infoText, sizeof(infoText), "%s\nFPS: %.1f", regionName, fps);

                lv_label_set_text(infoLabel_, infoText);
            }
        }
    }
}

void StartMenu::onNextFractalClicked(lv_event_t* e)
{
    auto* startMenu = static_cast<StartMenu*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!startMenu || !startMenu->fractal_) return;

    LOG_INFO(State, "Next fractal button clicked");
    startMenu->fractal_->advanceToNextFractal();
}

void StartMenu::onQuitButtonClicked(lv_event_t* e)
{
    auto* sm = static_cast<StateMachine*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!sm) return;

    LOG_INFO(State, "Quit button clicked");

    // Queue UI-local exit event (works in all states).
    UiApi::Exit::Cwc cwc;
    cwc.callback = [](auto&&) {}; // No response needed.
    sm->queueEvent(cwc);
}

void StartMenu::onTouchEvent(lv_event_t* e)
{
    auto* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (!label) return;

    // Get touch point from input device.
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Update the debug label with coordinates.
    char buf[64];
    snprintf(buf, sizeof(buf), "Touch: %d, %d", point.x, point.y);
    lv_label_set_text(label, buf);

    LOG_DEBUG(State, "Touch event at ({}, {})", point.x, point.y);
}

void StartMenu::onDisplayResized(lv_event_t* e)
{
    auto* fractal = static_cast<JuliaFractal*>(lv_event_get_user_data(e));
    if (!fractal) return;

    // Get new display dimensions.
    lv_disp_t* disp = lv_disp_get_default();
    int newWidth = lv_disp_get_hor_res(disp);
    int newHeight = lv_disp_get_ver_res(disp);

    LOG_INFO(State, "Display resized to {}x{}, updating fractal", newWidth, newHeight);

    // Resize the fractal to match.
    fractal->resize(newWidth, newHeight);
}

State::Any StartMenu::onEvent(const StartButtonClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Start button clicked, sending SimRun to server");

    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_ERROR(State, "Cannot start simulation, not connected to server");
        return StartMenu{};
    }

    const Api::SimRun::Command cmd{
        .timestep = 0.016, .max_steps = -1, .scenario_id = "sandbox", .max_frame_ms = 16
    };

    const auto result = wsService.sendCommand<Api::SimRun::Okay>(cmd, 2000);
    if (result.isError()) {
        LOG_ERROR(State, "SimRun failed: {}", result.errorValue());
        return StartMenu{};
    }

    const auto& response = result.value();
    if (response.isError()) {
        LOG_ERROR(State, "SimRun error: {}", response.errorValue().message);
        return StartMenu{};
    }

    if (!response.value().running) {
        LOG_WARN(State, "Server not running after SimRun");
        return StartMenu{};
    }

    LOG_INFO(State, "Server confirmed running, transitioning to SimRunning");
    return SimRunning{};
}

State::Any StartMenu::onEvent(const ServerDisconnectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_WARN(State, "Server disconnected (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning back to Disconnected");

    // Lost connection - go back to Disconnected state.
    return Disconnected{};
}

State::Any StartMenu::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any StartMenu::onEvent(const UiApi::SimRun::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "SimRun command received");

    // Get WebSocketService to send command to DSSM (binary protocol).
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_ERROR(State, "Not connected to DSSM server");
        cwc.sendResponse(UiApi::SimRun::Response::error(ApiError("Not connected to DSSM server")));
        return StartMenu{};
    }

    // Send sim_run command to DSSM server via binary protocol.
    const DirtSim::Api::SimRun::Command cmd{
        .timestep = 0.016,
        .max_steps = -1,
        .scenario_id = "sandbox",
        .max_frame_ms = 16 // Cap at 60 FPS for UI visualization.
    };

    const auto result = wsService.sendCommand<DirtSim::Api::SimRun::Okay>(cmd, 1000);
    if (result.isError()) {
        LOG_ERROR(State, "SimRun failed: {}", result.errorValue());
        cwc.sendResponse(UiApi::SimRun::Response::error(ApiError(result.errorValue())));
        return StartMenu{};
    }

    const auto& response = result.value();
    if (response.isError()) {
        LOG_ERROR(State, "SimRun error: {}", response.errorValue().message);
        cwc.sendResponse(UiApi::SimRun::Response::error(response.errorValue()));
        return StartMenu{};
    }

    LOG_INFO(State, "Server confirmed running, transitioning to SimRunning");

    // Send OK response.
    cwc.sendResponse(UiApi::SimRun::Response::okay({ true }));

    // Transition to SimRunning state.
    return SimRunning{};
}

State::Any StartMenu::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    // Update remote input device state (enables LVGL widget interaction in StartMenu).
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(true);
    }

    cwc.sendResponse(UiApi::MouseDown::Response::okay({}));
    return std::move(*this);
}

State::Any StartMenu::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    // Update remote input device position (enables LVGL widget interaction in StartMenu).
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
    }

    cwc.sendResponse(UiApi::MouseMove::Response::okay({}));
    return std::move(*this);
}

State::Any StartMenu::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    // Update remote input device state (enables LVGL widget interaction in StartMenu).
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(false);
    }

    cwc.sendResponse(UiApi::MouseUp::Response::okay({}));
    return std::move(*this);
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
