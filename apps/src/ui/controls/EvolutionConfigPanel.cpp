#include "EvolutionConfigPanel.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

EvolutionConfigPanel::EvolutionConfigPanel(
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
    lv_obj_set_style_pad_bottom(titleLabel, 8, 0);

    // Population Size stepper (10-200, step 10).
    populationStepper_ = LVGLBuilder::actionStepper(view)
                             .label("Population")
                             .range(10, 200)
                             .step(10)
                             .value(evolutionConfig_.populationSize)
                             .valueFormat("%.0f")
                             .valueScale(1.0)
                             .width(LV_PCT(95))
                             .callback(onPopulationChanged, this)
                             .buildOrLog();

    // Max Generations stepper (1-1000, step 10).
    generationsStepper_ = LVGLBuilder::actionStepper(view)
                              .label("Generations")
                              .range(1, 1000)
                              .step(10)
                              .value(evolutionConfig_.maxGenerations)
                              .valueFormat("%.0f")
                              .valueScale(1.0)
                              .width(LV_PCT(95))
                              .callback(onGenerationsChanged, this)
                              .buildOrLog();

    // Mutation Rate stepper (0-20% with 0.1% precision).
    // Internal value 0-200, displayed as 0.0-20.0%.
    mutationRateStepper_ = LVGLBuilder::actionStepper(view)
                               .label("Mutation Rate")
                               .range(0, 200)
                               .step(1)
                               .value(static_cast<int32_t>(mutationConfig_.rate * 1000.0))
                               .valueFormat("%.1f%%")
                               .valueScale(0.1)
                               .width(LV_PCT(95))
                               .callback(onMutationRateChanged, this)
                               .buildOrLog();

    // Tournament Size stepper (2-10, step 1).
    tournamentSizeStepper_ = LVGLBuilder::actionStepper(view)
                                 .label("Tournament Size")
                                 .range(2, 10)
                                 .step(1)
                                 .value(evolutionConfig_.tournamentSize)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(95))
                                 .callback(onTournamentSizeChanged, this)
                                 .buildOrLog();

    // Max Sim Time stepper (60-1800 seconds, step 30).
    // Displayed in seconds.
    maxSimTimeStepper_ = LVGLBuilder::actionStepper(view)
                             .label("Max Sim Time (s)")
                             .range(60, 1800)
                             .step(30)
                             .value(static_cast<int32_t>(evolutionConfig_.maxSimulationTime))
                             .valueFormat("%.0f")
                             .valueScale(1.0)
                             .width(LV_PCT(95))
                             .callback(onMaxSimTimeChanged, this)
                             .buildOrLog();

    // Status label (shows "Training in progress" when started).
    statusLabel_ = lv_label_create(view);
    lv_label_set_text(statusLabel_, "");
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x00CC66), 0);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(statusLabel_, 8, 0);

    updateControlsEnabled();
}

void EvolutionConfigPanel::updateControlsEnabled()
{
    // Disable steppers during training.
    auto setStepperEnabled = [](lv_obj_t* stepper, bool enabled) {
        if (!stepper) return;
        if (enabled) {
            lv_obj_clear_state(stepper, LV_STATE_DISABLED);
            lv_obj_set_style_opa(stepper, LV_OPA_COVER, 0);
        }
        else {
            lv_obj_add_state(stepper, LV_STATE_DISABLED);
            lv_obj_set_style_opa(stepper, LV_OPA_50, 0);
        }
    };

    bool enabled = !evolutionStarted_;
    setStepperEnabled(populationStepper_, enabled);
    setStepperEnabled(generationsStepper_, enabled);
    setStepperEnabled(mutationRateStepper_, enabled);
    setStepperEnabled(tournamentSizeStepper_, enabled);
    setStepperEnabled(maxSimTimeStepper_, enabled);

    if (statusLabel_) {
        if (evolutionStarted_) {
            lv_label_set_text(statusLabel_, "Training in progress...");
        }
        else {
            lv_label_set_text(statusLabel_, "");
        }
    }
}

void EvolutionConfigPanel::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    updateControlsEnabled();
}

void EvolutionConfigPanel::setEvolutionCompleted()
{
    evolutionStarted_ = false;

    // Enable controls (same as setEvolutionStarted(false)).
    updateControlsEnabled();

    // But show "Complete!" instead of empty status.
    if (statusLabel_) {
        lv_label_set_text(statusLabel_, "Complete!");
        lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xFFDD66), 0);
    }
}

void EvolutionConfigPanel::onPopulationChanged(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->populationStepper_) return;

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->populationStepper_);
    self->evolutionConfig_.populationSize = value;
    spdlog::debug("EvolutionConfigPanel: Population changed to {}", value);
}

void EvolutionConfigPanel::onGenerationsChanged(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->generationsStepper_) return;

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->generationsStepper_);
    self->evolutionConfig_.maxGenerations = value;
    spdlog::debug("EvolutionConfigPanel: Generations changed to {}", value);
}

void EvolutionConfigPanel::onMutationRateChanged(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->mutationRateStepper_) return;

    // Internal value is 0-200 representing 0.0-20.0%.
    // Convert to rate: value / 1000.0 (e.g., 15 -> 0.015 = 1.5%).
    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->mutationRateStepper_);
    self->mutationConfig_.rate = value / 1000.0;
    spdlog::debug("EvolutionConfigPanel: Mutation rate changed to {:.1f}%", value * 0.1);
}

void EvolutionConfigPanel::onTournamentSizeChanged(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->tournamentSizeStepper_) return;

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->tournamentSizeStepper_);
    self->evolutionConfig_.tournamentSize = value;
    spdlog::debug("EvolutionConfigPanel: Tournament size changed to {}", value);
}

void EvolutionConfigPanel::onMaxSimTimeChanged(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->maxSimTimeStepper_) return;

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->maxSimTimeStepper_);
    self->evolutionConfig_.maxSimulationTime = static_cast<double>(value);
    spdlog::debug("EvolutionConfigPanel: Max sim time changed to {}s", value);
}

} // namespace Ui
} // namespace DirtSim
