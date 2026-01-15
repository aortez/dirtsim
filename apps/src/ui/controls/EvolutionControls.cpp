#include "EvolutionControls.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

EvolutionControls::EvolutionControls(
    lv_obj_t* container,
    EventSink& eventSink,
    bool evolutionStarted,
    EvolutionConfig& evolutionConfig,
    MutationConfig& mutationConfig)
    : container_(container),
      eventSink_(eventSink),
      evolutionStarted_(evolutionStarted),
      evolutionConfig_(evolutionConfig),
      mutationConfig_(mutationConfig)
{
    viewController_ = std::make_unique<PanelViewController>(container_);

    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);
    viewController_->showView("main");

    spdlog::info("EvolutionControls: Initialized (started={})", evolutionStarted_);
}

EvolutionControls::~EvolutionControls()
{
    spdlog::info("EvolutionControls: Destroyed");
}

void EvolutionControls::createMainView(lv_obj_t* view)
{
    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Training Home");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0); // Orchid.
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 12, 0);

    // Quit button - always visible, returns to start menu.
    quitButton_ = LVGLBuilder::actionButton(view)
                      .text("Quit")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onQuitClicked, this)
                      .buildOrLog();

    // Stop button - only visible when evolution is running.
    stopButton_ = LVGLBuilder::actionButton(view)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onStopClicked, this)
                      .buildOrLog();

    // View Best button - only visible when evolution is complete.
    viewBestButton_ = LVGLBuilder::actionButton(view)
                          .text("View Best")
                          .icon(LV_SYMBOL_EYE_OPEN)
                          .mode(LVGLBuilder::ActionMode::Push)
                          .size(80)
                          .backgroundColor(0x0066CC)
                          .callback(onViewBestClicked, this)
                          .buildOrLog();

    // Start button - only visible when evolution is NOT running.
    startButton_ = LVGLBuilder::actionButton(view)
                       .text("Start Training")
                       .icon(LV_SYMBOL_PLAY)
                       .mode(LVGLBuilder::ActionMode::Push)
                       .size(80)
                       .backgroundColor(0x00AA66)
                       .callback(onStartClicked, this)
                       .buildOrLog();

    updateButtonVisibility();
}

void EvolutionControls::updateButtonVisibility()
{
    // Start button visible when NOT running and NOT completed.
    if (startButton_) {
        if (evolutionStarted_ || evolutionCompleted_) {
            lv_obj_add_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Stop button visible when running.
    if (stopButton_) {
        if (evolutionStarted_) {
            lv_obj_clear_flag(stopButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(stopButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // View Best button visible only when completed.
    if (viewBestButton_) {
        if (evolutionCompleted_) {
            lv_obj_clear_flag(viewBestButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(viewBestButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void EvolutionControls::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    updateButtonVisibility();
}

void EvolutionControls::setEvolutionCompleted(GenomeId bestGenomeId)
{
    evolutionStarted_ = false;
    evolutionCompleted_ = true;
    bestGenomeId_ = bestGenomeId;
    updateButtonVisibility();
}

void EvolutionControls::onStartClicked(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionControls: Start button clicked");

    // Fire event with current config.
    StartEvolutionButtonClickedEvent evt;
    evt.evolution = self->evolutionConfig_;
    evt.mutation = self->mutationConfig_;
    self->eventSink_.queueEvent(evt);
}

void EvolutionControls::onStopClicked(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionControls: Stop button clicked");

    self->eventSink_.queueEvent(StopButtonClickedEvent{});
}

void EvolutionControls::onViewBestClicked(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionControls: View Best button clicked");

    self->eventSink_.queueEvent(ViewBestButtonClickedEvent{ self->bestGenomeId_ });
}

void EvolutionControls::onQuitClicked(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionControls: Quit button clicked");

    // Quit also sends StopButtonClickedEvent to return to start menu.
    self->eventSink_.queueEvent(StopButtonClickedEvent{});
}

} // namespace Ui
} // namespace DirtSim
