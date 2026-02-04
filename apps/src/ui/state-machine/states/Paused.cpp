#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/SimStop.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {
namespace State {

void Paused::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Simulation paused, creating overlay");

    // Create semi-transparent overlay on top of the frozen world.
    lv_obj_t* screen = lv_scr_act();
    overlay_ = lv_obj_create(screen);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    // Create centered button container.
    lv_obj_t* buttonContainer = lv_obj_create(overlay_);
    lv_obj_set_size(buttonContainer, 200, 240);
    lv_obj_center(buttonContainer);
    lv_obj_set_style_bg_color(buttonContainer, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(buttonContainer, LV_OPA_90, 0);
    lv_obj_set_style_radius(buttonContainer, 10, 0);
    lv_obj_set_style_pad_all(buttonContainer, 15, 0);
    lv_obj_set_flex_flow(buttonContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        buttonContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(buttonContainer, LV_OBJ_FLAG_SCROLLABLE);

    // "PAUSED" label.
    lv_obj_t* pausedLabel = lv_label_create(buttonContainer);
    lv_label_set_text(pausedLabel, "PAUSED");
    lv_obj_set_style_text_font(pausedLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pausedLabel, lv_color_hex(0xFFFFFF), 0);

    // Resume button (green) - continues simulation.
    resumeButton_ = LVGLBuilder::actionButton(buttonContainer)
                        .text("Resume")
                        .icon(LV_SYMBOL_PLAY)
                        .mode(LVGLBuilder::ActionMode::Push)
                        .width(160)
                        .height(50)
                        .backgroundColor(0x00AA00)
                        .callback(onResumeClicked, &sm)
                        .buildOrLog();

    // Stop button (orange) - returns to start menu.
    stopButton_ = LVGLBuilder::actionButton(buttonContainer)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .width(160)
                      .height(50)
                      .backgroundColor(0xFF8800)
                      .callback(onStopClicked, &sm)
                      .buildOrLog();

    // Quit button (red) - exits program.
    quitButton_ = LVGLBuilder::actionButton(buttonContainer)
                      .text("Quit")
                      .icon(LV_SYMBOL_CLOSE)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .width(160)
                      .height(50)
                      .backgroundColor(0xCC0000)
                      .callback(onQuitClicked, &sm)
                      .buildOrLog();

    LOG_INFO(State, "Created overlay with Resume/Stop/Quit buttons");
}

void Paused::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting, cleaning up overlay");

    if (overlay_) {
        lv_obj_del(overlay_);
        overlay_ = nullptr;
        resumeButton_ = nullptr;
        stopButton_ = nullptr;
        quitButton_ = nullptr;
    }
}

void Paused::onResumeClicked(lv_event_t* e)
{
    auto* sm = static_cast<StateMachine*>(lv_event_get_user_data(e));
    if (!sm) return;

    LOG_INFO(State, "Resume button clicked");

    UiApi::SimRun::Cwc cwc;
    cwc.callback = [](auto&&) {};
    sm->queueEvent(cwc);
}

void Paused::onStopClicked(lv_event_t* e)
{
    auto* sm = static_cast<StateMachine*>(lv_event_get_user_data(e));
    if (!sm) return;

    LOG_INFO(State, "Stop button clicked");

    UiApi::SimStop::Cwc cwc;
    cwc.callback = [](auto&&) {};
    sm->queueEvent(cwc);
}

void Paused::onQuitClicked(lv_event_t* e)
{
    auto* sm = static_cast<StateMachine*>(lv_event_get_user_data(e));
    if (!sm) return;

    LOG_INFO(State, "Quit button clicked");

    UiApi::Exit::Cwc cwc;
    cwc.callback = [](auto&&) {};
    sm->queueEvent(cwc);
}

State::Any Paused::onEvent(const UiApi::SimRun::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "SimRun command received, resuming simulation");

    cwc.sendResponse(UiApi::SimRun::Response::okay({ true }));

    SimRunning newState;
    newState.worldData = std::move(worldData);
    return newState;
}

State::Any Paused::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm)
{
    LOG_INFO(State, "SimStop command received, stopping server simulation");

    // Tell the server to stop the simulation.
    auto& wsService = sm.getWebSocketService();
    if (wsService.isConnected()) {
        Api::SimStop::Command cmd;
        const auto result = wsService.sendCommandAndGetResponse<Api::SimStop::OkayType>(cmd, 2000);
        if (result.isError()) {
            LOG_ERROR(State, "Failed to send SimStop to server: {}", result.errorValue());
        }
        else if (result.value().isError()) {
            LOG_ERROR(State, "Server SimStop error: {}", result.value().errorValue().message);
        }
        else {
            LOG_INFO(State, "Server simulation stopped");
        }
    }

    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));

    // Discard world data and return to start menu.
    return StartMenu{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
