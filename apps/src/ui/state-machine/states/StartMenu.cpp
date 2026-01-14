#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/ScenarioListGet.h"
#include "server/api/SimRun.h"
#include "ui/RemoteInputDevice.h"
#include "ui/ScenarioMetadataCache.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/ExpandablePanel.h"
#include "ui/controls/IconRail.h"
#include "ui/controls/SparklingDuckButton.h"
#include "ui/rendering/JuliaFractal.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <chrono>
#include <lvgl/lvgl.h>
#include <lvgl/src/misc/lv_timer_private.h>
#include <nlohmann/json.hpp>
#include <thread>

namespace DirtSim {
namespace Ui {
namespace State {

void StartMenu::onEnter(StateMachine& sm)
{
    sm_ = &sm; // Store for callbacks.
    LOG_INFO(State, "Connected to server, ready to start simulation");

    // Request scenario list from server and cache it (needed even for autoRun).
    auto& wsService = sm.getWebSocketService();
    if (wsService.isConnected()) {
        const Api::ScenarioListGet::Command cmd{};
        const auto result = wsService.sendCommand<Api::ScenarioListGet::Okay>(cmd, 2000);
        if (result.isValue()) {
            const auto& response = result.value();
            if (response.isValue()) {
                ScenarioMetadataCache::load(response.value().scenarios);
                LOG_INFO(
                    State, "Loaded {} scenarios from server", response.value().scenarios.size());
            }
            else {
                LOG_ERROR(State, "ScenarioListGet failed: {}", response.errorValue().message);
            }
        }
        else {
            LOG_ERROR(State, "Failed to request scenario list: {}", result.errorValue());
        }
    }

    // Auto-run is a one-shot feature for startup.
    if (sm.uiConfig && sm.uiConfig->autoRun) {
        LOG_INFO(State, "autoRun enabled, starting simulation immediately");
        sm.uiConfig->autoRun = false;
        sm.queueEvent(StartButtonClickedEvent{});
        return;
    }

    // Get main menu container (switches to menu screen with IconRail).
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) return;

    // Trigger layout creation and get content area (to the right of IconRail).
    uiManager->getMainMenuContainer();
    lv_obj_t* container = uiManager->getMenuContentArea();

    // Configure IconRail to show CORE, NETWORK, SCENARIO, and EVOLUTION icons.
    if (IconRail* iconRail = uiManager->getMenuIconRail()) {
        iconRail->setVisibleIcons(
            { IconId::CORE, IconId::NETWORK, IconId::SCENARIO, IconId::EVOLUTION });
        LOG_INFO(State, "Configured IconRail with CORE, NETWORK, SCENARIO, and EVOLUTION icons");
    }

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

    // Create animated dirtsim start button.
    startButton_ = std::make_unique<SparklingDuckButton>(container, [&sm]() {
        // Queue event for state machine to process (keep LVGL callback thin).
        sm.queueEvent(StartButtonClickedEvent{});
    });

    LOG_INFO(State, "Created dirtsim start button");

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

    // Clean up panels.
    networkPanel_.reset();
    corePanel_.reset();

    // Clean up sparkle button.
    startButton_.reset();

    // Clean up fractal.
    if (fractal_) {
        // IMPORTANT: Remove the resize event handler before deleting the fractal.
        // This prevents use-after-free if a resize event occurs after exit.
        auto* uiManager = sm.getUiComponentManager();
        if (uiManager) {
            lv_obj_t* container = uiManager->getMenuContentArea();
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

State::Any StartMenu::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* uiManager = sm.getUiComponentManager();

    // Handle CORE icon - opens core panel with quit button.
    if (evt.selectedId == IconId::CORE) {
        LOG_INFO(State, "Core icon selected, showing core panel");

        if (auto* panel = uiManager->getMenuExpandablePanel()) {
            panel->clearContent();
            corePanel_ = std::make_unique<StartMenuCorePanel>(panel->getContentArea(), sm);
            panel->show();
        }
        return std::move(*this); // Don't deselect - panel should stay open.
    }

    // Handle deselection of CORE.
    if (evt.previousId == IconId::CORE) {
        LOG_INFO(State, "Core icon deselected, hiding panel");
        if (auto* panel = uiManager->getMenuExpandablePanel()) {
            panel->hide();
            panel->clearContent();
        }
        corePanel_.reset();
    }

    // Handle NETWORK icon - opens network diagnostics panel.
    if (evt.selectedId == IconId::NETWORK) {
        LOG_INFO(State, "Network icon selected, showing diagnostics panel");

        if (auto* panel = uiManager->getMenuExpandablePanel()) {
            panel->clearContent();
            networkPanel_ = std::make_unique<NetworkDiagnosticsPanel>(panel->getContentArea());
            panel->show();
        }
        return std::move(*this); // Don't deselect - panel should stay open.
    }

    // Handle deselection of NETWORK.
    if (evt.previousId == IconId::NETWORK) {
        LOG_INFO(State, "Network icon deselected, hiding panel");
        if (auto* panel = uiManager->getMenuExpandablePanel()) {
            panel->hide();
            panel->clearContent();
        }
        networkPanel_.reset();
    }

    // SCENARIO and EVOLUTION are action triggers - fire and deselect.
    if (evt.selectedId == IconId::SCENARIO) {
        LOG_INFO(State, "Scenario icon clicked, starting simulation");
        sm.queueEvent(StartButtonClickedEvent{});
        // Deselect action icons after firing.
        if (auto* iconRail = uiManager->getMenuIconRail()) {
            iconRail->deselectAll();
        }
    }
    else if (evt.selectedId == IconId::EVOLUTION) {
        LOG_INFO(State, "Evolution icon clicked, starting training");
        sm.queueEvent(TrainButtonClickedEvent{});
        // Deselect action icons after firing.
        if (auto* iconRail = uiManager->getMenuIconRail()) {
            iconRail->deselectAll();
        }
    }

    return std::move(*this);
}

State::Any StartMenu::onEvent(const RailAutoShrinkRequestEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Auto-shrink requested, minimizing menu IconRail");

    // Process auto-shrink in main thread (safe to modify LVGL objects).
    if (auto* iconRail = sm.getUiComponentManager()->getMenuIconRail()) {
        iconRail->setMode(RailMode::Minimized);
    }

    return std::move(*this);
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
        .timestep = 0.016, .max_steps = -1, .max_frame_ms = 16, .scenario_id = std::nullopt
    };

    // Retry logic for autoRun to handle server startup race condition.
    const int maxRetries = sm.getUiConfig().autoRun ? 3 : 1;
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        if (attempt > 1) {
            LOG_INFO(State, "Retrying SimRun (attempt {}/{})", attempt, maxRetries);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        const auto result = wsService.sendCommand<Api::SimRun::Okay>(cmd, 2000);
        if (result.isError()) {
            LOG_ERROR(State, "SimRun failed: {}", result.errorValue());
            continue; // Retry.
        }

        const auto& response = result.value();
        if (response.isError()) {
            const auto& errMsg = response.errorValue().message;
            // Retry if server is still starting up.
            if (errMsg.find("not supported in state") != std::string::npos
                && attempt < maxRetries) {
                LOG_WARN(State, "Server not ready ({}), retrying...", errMsg);
                continue;
            }
            LOG_ERROR(State, "SimRun error: {}", errMsg);
            return StartMenu{};
        }

        if (!response.value().running) {
            LOG_WARN(State, "Server not running after SimRun");
            return StartMenu{};
        }

        LOG_INFO(State, "Server confirmed running, transitioning to SimRunning");
        return SimRunning{};
    }

    LOG_ERROR(State, "SimRun failed after {} attempts", maxRetries);
    return StartMenu{};
}

State::Any StartMenu::onEvent(const TrainButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Train button clicked, transitioning to Training");

    return Training{};
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
        .timestep = 0.016, .max_steps = -1, .max_frame_ms = 16, .scenario_id = std::nullopt
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
