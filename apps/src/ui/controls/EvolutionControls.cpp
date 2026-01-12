#include "EvolutionControls.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

EvolutionControls::EvolutionControls(lv_obj_t* container, EventSink& eventSink)
    : container_(container), eventSink_(eventSink)
{
    viewController_ = std::make_unique<PanelViewController>(container_);

    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);
    viewController_->showView("main");

    spdlog::info("EvolutionControls: Initialized");
}

EvolutionControls::~EvolutionControls()
{
    spdlog::info("EvolutionControls: Destroyed");
}

void EvolutionControls::createMainView(lv_obj_t* view)
{
    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Evolution Controls");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0); // Orchid to match icon.
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 12, 0);

    stopButton_ = LVGLBuilder::actionButton(view)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .size(80)
                      .backgroundColor(0xCC0000)
                      .callback(onStopClicked, this)
                      .buildOrLog();

    liveDisplayToggle_ = LVGLBuilder::actionButton(view)
                             .text("Show Live Sim")
                             .mode(LVGLBuilder::ActionMode::Toggle)
                             .size(80)
                             .checked(liveDisplayEnabled_)
                             .glowColor(0x00CC00)
                             .callback(onLiveDisplayToggled, this)
                             .buildOrLog();
}

void EvolutionControls::setLiveDisplayEnabled(bool enabled)
{
    liveDisplayEnabled_ = enabled;
    if (liveDisplayToggle_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(liveDisplayToggle_, enabled);
    }
}

void EvolutionControls::onStopClicked(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionControls: Stop button clicked");

    self->eventSink_.queueEvent(StopButtonClickedEvent{});
}

void EvolutionControls::onLiveDisplayToggled(lv_event_t* e)
{
    EvolutionControls* self = static_cast<EvolutionControls*>(lv_event_get_user_data(e));
    if (!self) return;

    bool enabled = LVGLBuilder::ActionButtonBuilder::isChecked(self->liveDisplayToggle_);

    spdlog::info("EvolutionControls: Live display toggled to {}", enabled ? "ON" : "OFF");

    self->liveDisplayEnabled_ = enabled;
}

} // namespace Ui
} // namespace DirtSim
