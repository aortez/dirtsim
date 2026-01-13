#include "EvolutionControls.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

EvolutionControls::EvolutionControls(
    lv_obj_t* container, EventSink& eventSink, bool evolutionStarted)
    : container_(container), eventSink_(eventSink), evolutionStarted_(evolutionStarted)
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
    lv_label_set_text(titleLabel, "Training Controls");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0); // Orchid.
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 12, 0);

    // Stop button - only visible when evolution is running.
    stopButton_ = LVGLBuilder::actionButton(view)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onStopClicked, this)
                      .buildOrLog();

    // Quit button - always visible, returns to start menu.
    quitButton_ = LVGLBuilder::actionButton(view)
                      .text("Quit")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onQuitClicked, this)
                      .buildOrLog();

    updateButtonVisibility();
}

void EvolutionControls::updateButtonVisibility()
{
    if (stopButton_) {
        if (evolutionStarted_) {
            lv_obj_clear_flag(stopButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(stopButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void EvolutionControls::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    updateButtonVisibility();
}

void EvolutionControls::onStopClicked(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionControls: Stop button clicked");

    self->eventSink_.queueEvent(StopButtonClickedEvent{});
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
