#include "EvolutionControls.h"
#include "core/Assert.h"
#include "ui/controls/DuckStopButton.h"
#include "ui/rendering/FractalAnimator.h"
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
    TrainingSpec& trainingSpec,
    FractalAnimator* fractalAnimator)
    : container_(container),
      eventSink_(eventSink),
      fractalAnimator_(fractalAnimator),
      evolutionStarted_(evolutionStarted),
      trainingSpec_(trainingSpec)
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
    DIRTSIM_ASSERT(fractalAnimator_, "EvolutionControls requires FractalAnimator for Quit button");
    quitButton_ = std::make_unique<DuckStopButton>(view, *fractalAnimator_, 108, 108, "Quit");
    if (quitButton_ && quitButton_->getButton()) {
        lv_obj_add_event_cb(quitButton_->getButton(), onQuitClicked, LV_EVENT_CLICKED, this);
    }
    else {
        spdlog::error("EvolutionControls: Failed to create Quit button");
    }

    // View Best button - only visible when evolution is complete.
    viewBestButton_ = LVGLBuilder::actionButton(view)
                          .text("View Best")
                          .icon(LV_SYMBOL_EYE_OPEN)
                          .mode(LVGLBuilder::ActionMode::Push)
                          .size(80)
                          .backgroundColor(0x0066CC)
                          .callback(onViewBestClicked, this)
                          .buildOrLog();

    updateButtonVisibility();
}

void EvolutionControls::updateButtonVisibility()
{
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
    bestGenomeId_ = bestGenomeId;
    evolutionCompleted_ = !bestGenomeId.isNil() && trainingSpec_.organismType == OrganismType::TREE;
    updateButtonVisibility();
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

    self->eventSink_.queueEvent(QuitTrainingClickedEvent{});
}

} // namespace Ui
} // namespace DirtSim
