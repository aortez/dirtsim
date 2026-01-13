#include "EvolutionConfigPanel.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

EvolutionConfigPanel::EvolutionConfigPanel(
    lv_obj_t* container, EventSink& eventSink, bool evolutionStarted)
    : container_(container), eventSink_(eventSink), evolutionStarted_(evolutionStarted)
{
    viewController_ = std::make_unique<PanelViewController>(container_);

    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);
    viewController_->showView("main");

    spdlog::info("EvolutionConfigPanel: Initialized (started={})", evolutionStarted_);
}

EvolutionConfigPanel::~EvolutionConfigPanel()
{
    spdlog::info("EvolutionConfigPanel: Destroyed");
}

void EvolutionConfigPanel::createMainView(lv_obj_t* view)
{
    // Title.
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Evolution Config");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0); // Orchid.
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 12, 0);

    // Status label showing current config summary.
    statusLabel_ = lv_label_create(view);
    char buf[128];
    snprintf(
        buf,
        sizeof(buf),
        "Population: %d\nGenerations: %d\nMutation rate: %.1f%%",
        evolutionConfig_.populationSize,
        evolutionConfig_.maxGenerations,
        mutationConfig_.rate * 100.0);
    lv_label_set_text(statusLabel_, buf);
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_bottom(statusLabel_, 16, 0);

    // Start button.
    startButton_ = LVGLBuilder::actionButton(view)
                       .text("Start Training")
                       .icon(LV_SYMBOL_PLAY)
                       .mode(LVGLBuilder::ActionMode::Push)
                       .size(80)
                       .backgroundColor(0x00AA66)
                       .callback(onStartClicked, this)
                       .buildOrLog();

    updateStartButtonVisibility();
}

void EvolutionConfigPanel::updateStartButtonVisibility()
{
    if (startButton_) {
        if (evolutionStarted_) {
            lv_obj_add_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (statusLabel_) {
        if (evolutionStarted_) {
            lv_label_set_text(statusLabel_, "Training in progress...");
            lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x00CC66), 0);
        }
        else {
            char buf[128];
            snprintf(
                buf,
                sizeof(buf),
                "Population: %d\nGenerations: %d\nMutation rate: %.1f%%",
                evolutionConfig_.populationSize,
                evolutionConfig_.maxGenerations,
                mutationConfig_.rate * 100.0);
            lv_label_set_text(statusLabel_, buf);
            lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xAAAAAA), 0);
        }
    }
}

void EvolutionConfigPanel::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    updateStartButtonVisibility();
}

void EvolutionConfigPanel::onStartClicked(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionConfigPanel: Start button clicked");

    // Fire event with current config.
    StartEvolutionButtonClickedEvent evt;
    evt.evolution = self->evolutionConfig_;
    evt.mutation = self->mutationConfig_;
    self->eventSink_.queueEvent(evt);
}

} // namespace Ui
} // namespace DirtSim
