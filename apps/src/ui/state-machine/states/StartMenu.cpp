#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "server/api/ScenarioListGet.h"
#include "server/api/SimRun.h"
#include "ui/ScenarioMetadataCache.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/ExpandablePanel.h"
#include "ui/controls/IconRail.h"
#include "ui/controls/SparklingDuckButton.h"
#include "ui/rendering/FractalAnimator.h"
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
        const auto result =
            wsService.sendCommandAndGetResponse<Api::ScenarioListGet::Okay>(cmd, 2000);
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
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");

    // Trigger layout creation and get content area (to the right of IconRail).
    uiManager->getMainMenuContainer();
    lv_obj_t* container = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(container, "StartMenu requires a menu content area");
    lv_obj_clean(container);

    // Configure IconRail to show StartMenu icons in two columns.
    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "StartMenu requires an IconRail");
    iconRail->setVisibleIcons(
        { IconId::CORE, IconId::MUSIC, IconId::EVOLUTION, IconId::NETWORK, IconId::SCENARIO });
    iconRail->setLayout(RailLayout::TwoColumn);
    iconRail->deselectAll();
    LOG_INFO(State, "Configured IconRail with CORE, MUSIC, EVOLUTION, NETWORK, SCENARIO icons");

    // Get display dimensions for full-screen fractal.
    lv_disp_t* disp = lv_disp_get_default();
    DIRTSIM_ASSERT(disp, "StartMenu requires an LVGL display");
    int windowWidth = lv_disp_get_hor_res(disp);
    int windowHeight = lv_disp_get_ver_res(disp);

    // Attach shared fractal background.
    sm.getFractalAnimator().attachTo(container, windowWidth, windowHeight);
    LOG_INFO(State, "Attached fractal background (event-driven rendering)");

    // Add resize event handler to container (catches window resize events).
    lv_obj_add_event_cb(container, onDisplayResized, LV_EVENT_SIZE_CHANGED, &sm);
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

    // Set flex layout for the info label.
    lv_obj_set_layout(infoPanel_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(infoPanel_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        infoPanel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(infoPanel_, 15, 0);

    // Create info label.
    infoLabel_ = lv_label_create(infoPanel_);
    lv_label_set_text(infoLabel_, "Loading fractal info...");
    lv_obj_set_style_text_color(infoLabel_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(infoLabel_, &lv_font_montserrat_14, 0);

    LOG_INFO(State, "Created fractal info panel");
    updateInfoPanelVisibility(iconRail->getMode());

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
    corePanel_.reset();

    // Clean up sparkle button.
    startButton_.reset();

    // IMPORTANT: Remove the resize event handler before detaching the fractal.
    // This prevents use-after-free if a resize event occurs after exit.
    auto* uiManager = sm.getUiComponentManager();
    DIRTSIM_ASSERT(uiManager, "UiComponentManager must exist");
    lv_obj_t* container = uiManager->getMenuContentArea();
    DIRTSIM_ASSERT(container, "StartMenu requires a menu content area");
    lv_obj_remove_event_cb(container, onDisplayResized);
    lv_obj_remove_event_cb(container, onTouchEvent);
    sm.getFractalAnimator().parkIfParent(container);
    LOG_INFO(State, "Removed resize and touch event handlers");

    // Screen switch will clean up other widgets automatically.
    touchDebugLabel_ = nullptr;
}

void StartMenu::updateAnimations()
{
    // Update sparkle button animation.
    if (startButton_) {
        startButton_->update();
    }

    DIRTSIM_ASSERT(sm_, "StartMenu requires a valid StateMachine");

    if (auto* fractal = sm_->getFractalAnimator().getFractal()) {

        // Update info label with current fractal parameters (~1/sec to reduce overhead).
        if (infoLabel_) {
            labelUpdateCounter_++;
            if (labelUpdateCounter_ >= 60) { // Update ~1/sec at 60fps.
                labelUpdateCounter_ = 0;

                const char* regionName = fractal->getRegionName();

                // Get all iteration values atomically to prevent race conditions.
                int minIter, currentIter, maxIter;
                fractal->getIterationInfo(minIter, currentIter, maxIter);

                double fps = fractal->getDisplayFps();

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

void StartMenu::onTouchEvent(lv_event_t* e)
{
    auto* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(label, "StartMenu touch handler requires label user_data");

    // Get touch point from input device.
    lv_indev_t* indev = lv_indev_active();
    DIRTSIM_ASSERT(indev, "StartMenu touch handler requires an active input device");

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
    auto* sm = static_cast<StateMachine*>(lv_event_get_user_data(e));
    DIRTSIM_ASSERT(sm, "StartMenu resize handler requires StateMachine user_data");

    auto* container = static_cast<lv_obj_t*>(lv_event_get_target(e));
    DIRTSIM_ASSERT(container, "StartMenu resize handler requires LVGL target");

    // Get new display dimensions.
    lv_disp_t* disp = lv_disp_get_default();
    DIRTSIM_ASSERT(disp, "StartMenu requires an LVGL display");
    int newWidth = lv_disp_get_hor_res(disp);
    int newHeight = lv_disp_get_ver_res(disp);

    LOG_INFO(State, "Display resized to {}x{}, updating fractal", newWidth, newHeight);

    // Update the fractal view to match.
    sm->getFractalAnimator().attachTo(container, newWidth, newHeight);
}

void StartMenu::updateInfoPanelVisibility(RailMode mode)
{
    DIRTSIM_ASSERT(infoPanel_, "StartMenu requires infoPanel_");

    if (mode == RailMode::Minimized) {
        lv_obj_clear_flag(infoPanel_, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(infoPanel_, LV_OBJ_FLAG_HIDDEN);
    }
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

        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->resetWidth();
            corePanel_ = std::make_unique<StartMenuCorePanel>(panel->getContentArea(), sm);
            panel->show();
        }
        return std::move(*this); // Don't deselect - panel should stay open.
    }

    // Handle deselection of CORE.
    if (evt.previousId == IconId::CORE) {
        LOG_INFO(State, "Core icon deselected, hiding panel");
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->hide();
            panel->clearContent();
            panel->resetWidth();
        }
        corePanel_.reset();
    }

    if (evt.selectedId == IconId::MUSIC) {
        LOG_INFO(State, "Music icon clicked, transitioning to Synth");
        return Synth{};
    }

    if (evt.selectedId == IconId::NETWORK) {
        LOG_INFO(State, "Network icon clicked, transitioning to Network");
        return Network{};
    }

    // SCENARIO and EVOLUTION are action triggers - fire and deselect.
    if (evt.selectedId == IconId::SCENARIO) {
        LOG_INFO(State, "Scenario icon clicked, starting simulation");
        sm.queueEvent(StartButtonClickedEvent{});
        // Deselect action icons after firing.
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->deselectAll();
        }
    }
    else if (evt.selectedId == IconId::EVOLUTION) {
        LOG_INFO(State, "Evolution icon clicked, starting training");
        sm.queueEvent(TrainButtonClickedEvent{});
        // Deselect action icons after firing.
        if (auto* iconRail = uiManager->getIconRail()) {
            iconRail->deselectAll();
        }
    }

    return std::move(*this);
}

State::Any StartMenu::onEvent(const RailModeChangedEvent& evt, StateMachine& /*sm*/)
{
    updateInfoPanelVisibility(evt.newMode);
    return std::move(*this);
}

State::Any StartMenu::onEvent(const StartButtonClickedEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Start button clicked, sending SimRun to server");
    return startSimulation(sm, std::nullopt);
}

State::Any StartMenu::onEvent(const StartMenuIdleTimeoutEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "StartMenu idle timeout reached, launching clock scenario");
    return startSimulation(sm, Scenario::EnumType::Clock);
}

State::Any StartMenu::onEvent(const TrainButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Train button clicked, transitioning to Training");

    return TrainingIdle{};
}

State::Any StartMenu::onEvent(const NextFractalClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    DIRTSIM_ASSERT(sm_, "StartMenu requires a valid StateMachine");

    auto* fractal = sm_->getFractalAnimator().getFractal();
    DIRTSIM_ASSERT(fractal, "StartMenu requires an active fractal");

    LOG_INFO(State, "Next fractal requested from core panel");
    fractal->advanceToNextFractal();
    return std::move(*this);
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

    lv_disp_t* disp = lv_disp_get_default();
    int16_t dispWidth = static_cast<int16_t>(lv_disp_get_hor_res(disp));
    int16_t dispHeight = static_cast<int16_t>(lv_disp_get_ver_res(disp));
    Vector2s containerSize{ static_cast<int16_t>(dispWidth - IconRail::MINIMIZED_RAIL_WIDTH),
                            dispHeight };

    const DirtSim::Api::SimRun::Command cmd{ .timestep = 0.016,
                                             .max_steps = -1,
                                             .max_frame_ms = 16,
                                             .scenario_id = cwc.command.scenario_id,
                                             .start_paused = false,
                                             .container_size = containerSize };

    const auto result = wsService.sendCommandAndGetResponse<DirtSim::Api::SimRun::Okay>(cmd, 1000);
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

State::Any StartMenu::startSimulation(
    StateMachine& sm, std::optional<Scenario::EnumType> scenarioId)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_ERROR(State, "Cannot start simulation, not connected to server");
        return StartMenu{};
    }

    lv_disp_t* disp = lv_disp_get_default();
    int16_t dispWidth = static_cast<int16_t>(lv_disp_get_hor_res(disp));
    int16_t dispHeight = static_cast<int16_t>(lv_disp_get_ver_res(disp));
    Vector2s containerSize{ static_cast<int16_t>(dispWidth - IconRail::MINIMIZED_RAIL_WIDTH),
                            dispHeight };
    LOG_INFO(State, "Container size for SimRun: {}x{}", containerSize.x, containerSize.y);

    const Api::SimRun::Command cmd{ .timestep = 0.016,
                                    .max_steps = -1,
                                    .max_frame_ms = 16,
                                    .scenario_id = scenarioId,
                                    .start_paused = false,
                                    .container_size = containerSize };

    // Retry logic for autoRun to handle server startup race condition.
    const int maxRetries = sm.getUiConfig().autoRun ? 3 : 1;
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        if (attempt > 1) {
            LOG_INFO(State, "Retrying SimRun (attempt {}/{})", attempt, maxRetries);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        const auto result = wsService.sendCommandAndGetResponse<Api::SimRun::Okay>(cmd, 2000);
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

State::Any StartMenu::onEvent(const UiApi::TrainingStart::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "TrainingStart command received, transitioning to Training");

    StartEvolutionButtonClickedEvent evt{
        .evolution = cwc.command.evolution,
        .mutation = cwc.command.mutation,
        .training = cwc.command.training,
    };
    sm.queueEvent(evt);

    cwc.sendResponse(UiApi::TrainingStart::Response::okay({ .queued = true }));
    return TrainingIdle{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
