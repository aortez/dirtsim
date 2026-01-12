#include "State.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {
namespace State {

void Training::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Training state, starting evolution on server");
    sm_ = &sm;

    // Send EvolutionStart to server.
    if (sm.hasWebSocketService()) {
        auto& wsService = sm.getWebSocketService();
        if (wsService.isConnected()) {
            Api::EvolutionStart::Command cmd;
            cmd.scenarioId = "TreeGermination";

            const auto result = wsService.sendCommand<Api::EvolutionStart::OkayType>(cmd, 5000);
            if (result.isError()) {
                LOG_ERROR(State, "Failed to send EvolutionStart: {}", result.errorValue());
            }
            else if (result.value().isError()) {
                LOG_ERROR(
                    State, "Server EvolutionStart error: {}", result.value().errorValue().message);
            }
            else {
                LOG_INFO(State, "Evolution started on server");
            }
        }
        else {
            LOG_WARN(State, "Not connected to server, cannot start evolution");
        }
    }

    // Get main menu container for Training UI.
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    container_ = uiManager->getMainMenuContainer();
    if (!container_) {
        LOG_ERROR(State, "Failed to get main menu container");
        return;
    }

    // Create centered content container.
    lv_obj_t* content = lv_obj_create(container_);
    lv_obj_set_size(content, 400, 350);
    lv_obj_center(content);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_90, 0);
    lv_obj_set_style_radius(content, 12, 0);
    lv_obj_set_style_border_width(content, 2, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Title: "EVOLUTION".
    lv_obj_t* title = lv_label_create(content);
    lv_label_set_text(title, "EVOLUTION");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
    lv_obj_set_style_pad_bottom(title, 20, 0);

    // Generation progress section.
    genLabel_ = lv_label_create(content);
    lv_label_set_text(genLabel_, "Generation: 0 / 0");
    lv_obj_set_style_text_color(genLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_bottom(genLabel_, 5, 0);

    generationBar_ = lv_bar_create(content);
    lv_obj_set_size(generationBar_, 340, 20);
    lv_bar_set_range(generationBar_, 0, 100);
    lv_bar_set_value(generationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(generationBar_, lv_color_hex(0x00AA66), LV_PART_INDICATOR);
    lv_obj_set_style_radius(generationBar_, 5, 0);
    lv_obj_set_style_radius(generationBar_, 5, LV_PART_INDICATOR);
    lv_obj_set_style_pad_bottom(generationBar_, 15, 0);

    // Evaluation progress section.
    evalLabel_ = lv_label_create(content);
    lv_label_set_text(evalLabel_, "Evaluating: 0 / 0");
    lv_obj_set_style_text_color(evalLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_bottom(evalLabel_, 5, 0);

    evaluationBar_ = lv_bar_create(content);
    lv_obj_set_size(evaluationBar_, 340, 20);
    lv_bar_set_range(evaluationBar_, 0, 100);
    lv_bar_set_value(evaluationBar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_color(evaluationBar_, lv_color_hex(0x6688CC), LV_PART_INDICATOR);
    lv_obj_set_style_radius(evaluationBar_, 5, 0);
    lv_obj_set_style_radius(evaluationBar_, 5, LV_PART_INDICATOR);
    lv_obj_set_style_pad_bottom(evaluationBar_, 20, 0);

    // Fitness statistics section.
    lv_obj_t* statsContainer = lv_obj_create(content);
    lv_obj_set_size(statsContainer, 340, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(statsContainer, 0, 0);
    lv_obj_set_style_pad_all(statsContainer, 0, 0);
    lv_obj_set_flex_flow(statsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        statsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(statsContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(statsContainer, 20, 0);

    bestThisGenLabel_ = lv_label_create(statsContainer);
    lv_label_set_text(bestThisGenLabel_, "Best (this gen):  --");
    lv_obj_set_style_text_color(bestThisGenLabel_, lv_color_hex(0xAAAACC), 0);

    bestAllTimeLabel_ = lv_label_create(statsContainer);
    lv_label_set_text(bestAllTimeLabel_, "Best (all time):  --");
    lv_obj_set_style_text_color(bestAllTimeLabel_, lv_color_hex(0xFFDD66), 0);

    averageLabel_ = lv_label_create(statsContainer);
    lv_label_set_text(averageLabel_, "Average:          --");
    lv_obj_set_style_text_color(averageLabel_, lv_color_hex(0xAAAACC), 0);

    // Stop button using ActionButton style.
    stopButton_ = LVGLBuilder::ActionButtonBuilder(content)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .width(120)
                      .height(50)
                      .backgroundColor(0x882222)
                      .callback(onStopClicked, this)
                      .buildOrLog();

    LOG_INFO(State, "Training UI created");
}

void Training::onExit(StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exiting Training state");

    // Clear container children (UiComponentManager may handle this, but be safe).
    if (container_) {
        // Clear all children we created.
        lv_obj_clean(container_);
    }

    // Reset pointers.
    container_ = nullptr;
    generationBar_ = nullptr;
    evaluationBar_ = nullptr;
    genLabel_ = nullptr;
    evalLabel_ = nullptr;
    bestThisGenLabel_ = nullptr;
    bestAllTimeLabel_ = nullptr;
    averageLabel_ = nullptr;
    stopButton_ = nullptr;
    sm_ = nullptr;
}

void Training::onStopClicked(lv_event_t* e)
{
    auto* self = static_cast<Training*>(lv_event_get_user_data(e));
    if (!self || !self->sm_) return;

    LOG_INFO(State, "Stop button clicked, sending EvolutionStop");

    // Send EvolutionStop to server.
    if (self->sm_->hasWebSocketService()) {
        auto& wsService = self->sm_->getWebSocketService();
        if (wsService.isConnected()) {
            Api::EvolutionStop::Command cmd;
            const auto result = wsService.sendCommand<Api::EvolutionStop::OkayType>(cmd, 2000);
            if (result.isError()) {
                LOG_ERROR(State, "Failed to send EvolutionStop: {}", result.errorValue());
            }
            else if (result.value().isError()) {
                LOG_ERROR(
                    State, "Server EvolutionStop error: {}", result.value().errorValue().message);
            }
            else {
                LOG_INFO(State, "Evolution stopped on server");
            }
        }
    }

    // Queue transition back to StartMenu.
    self->sm_->queueEvent(StopButtonClickedEvent{});
}

void Training::updateProgressDisplay()
{
    if (!genLabel_ || !evalLabel_ || !generationBar_ || !evaluationBar_) return;

    // Update generation progress.
    char buf[64];
    snprintf(buf, sizeof(buf), "Generation: %d / %d", progress.generation, progress.maxGenerations);
    lv_label_set_text(genLabel_, buf);

    int genPercent =
        progress.maxGenerations > 0 ? (progress.generation * 100) / progress.maxGenerations : 0;
    lv_bar_set_value(generationBar_, genPercent, LV_ANIM_ON);

    // Update evaluation progress.
    snprintf(
        buf, sizeof(buf), "Evaluating: %d / %d", progress.currentEval, progress.populationSize);
    lv_label_set_text(evalLabel_, buf);

    int evalPercent =
        progress.populationSize > 0 ? (progress.currentEval * 100) / progress.populationSize : 0;
    lv_bar_set_value(evaluationBar_, evalPercent, LV_ANIM_ON);

    // Update fitness labels.
    if (bestThisGenLabel_) {
        snprintf(buf, sizeof(buf), "Best (this gen):  %.2f", progress.bestFitnessThisGen);
        lv_label_set_text(bestThisGenLabel_, buf);
    }

    if (bestAllTimeLabel_) {
        snprintf(buf, sizeof(buf), "Best (all time):  %.2f", progress.bestFitnessAllTime);
        lv_label_set_text(bestAllTimeLabel_, buf);
    }

    if (averageLabel_) {
        snprintf(buf, sizeof(buf), "Average:          %.2f", progress.averageFitness);
        lv_label_set_text(averageLabel_, buf);
    }
}

State::Any Training::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state.
    return Shutdown{};
}

State::Any Training::onEvent(const ServerDisconnectedEvent& evt, StateMachine& /*sm*/)
{
    LOG_WARN(State, "Server disconnected during training (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning to Disconnected");

    // Lost connection - go back to Disconnected state.
    return Disconnected{};
}

State::Any Training::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked, returning to StartMenu");

    // Transition back to start menu.
    return StartMenu{};
}

State::Any Training::onEvent(const EvolutionProgressReceivedEvent& evt, StateMachine& /*sm*/)
{
    // Update progress from server broadcast.
    progress = evt.progress;

    LOG_DEBUG(
        State,
        "Evolution progress: gen {}/{}, eval {}/{}, best fitness {:.2f}",
        progress.generation,
        progress.maxGenerations,
        progress.currentEval,
        progress.populationSize,
        progress.bestFitnessAllTime);

    // Update UI progress bars and labels.
    updateProgressDisplay();

    // Stay in Training state.
    return Training{ *this };
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
