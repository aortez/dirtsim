#include "EvolutionConfigPanel.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

EvolutionConfigPanel::EvolutionConfigPanel(
    lv_obj_t* container,
    EventSink& eventSink,
    bool evolutionStarted,
    EvolutionConfig& evolutionConfig,
    MutationConfig& mutationConfig,
    TrainingSpec& trainingSpec)
    : container_(container),
      eventSink_(eventSink),
      evolutionStarted_(evolutionStarted),
      evolutionConfig_(evolutionConfig),
      mutationConfig_(mutationConfig),
      trainingSpec_(trainingSpec)
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
    lv_obj_t* columns = lv_obj_create(view);
    lv_obj_set_size(columns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(columns, 0, 0);
    lv_obj_set_style_pad_all(columns, 0, 0);
    lv_obj_set_style_pad_column(columns, 12, 0);
    lv_obj_set_style_pad_row(columns, 0, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* leftColumn = lv_obj_create(columns);
    lv_obj_set_size(leftColumn, LV_PCT(35), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(leftColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftColumn, 0, 0);
    lv_obj_set_style_pad_all(leftColumn, 0, 0);
    lv_obj_set_style_pad_row(leftColumn, 10, 0);
    lv_obj_set_flex_flow(leftColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        leftColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(leftColumn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* rightColumn = lv_obj_create(columns);
    lv_obj_set_size(rightColumn, LV_PCT(65), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rightColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightColumn, 0, 0);
    lv_obj_set_style_pad_all(rightColumn, 0, 0);
    lv_obj_set_style_pad_row(rightColumn, 8, 0);
    lv_obj_set_flex_flow(rightColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        rightColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rightColumn, LV_OBJ_FLAG_SCROLLABLE);

    startButton_ = LVGLBuilder::actionButton(leftColumn)
                       .text("Start Training")
                       .icon(LV_SYMBOL_PLAY)
                       .mode(LVGLBuilder::ActionMode::Push)
                       .width(140)
                       .height(80)
                       .backgroundColor(0x00AA66)
                       .callback(onStartClicked, this)
                       .buildOrLog();

    stopButton_ = LVGLBuilder::actionButton(leftColumn)
                      .text("Stop")
                      .icon(LV_SYMBOL_STOP)
                      .mode(LVGLBuilder::ActionMode::Push)
                      .width(140)
                      .height(80)
                      .backgroundColor(0xCC0000)
                      .callback(onStopClicked, this)
                      .buildOrLog();

    // Title.
    lv_obj_t* titleLabel = lv_label_create(rightColumn);
    lv_label_set_text(titleLabel, "Evolution Config");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0); // Orchid.
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 8, 0);

    // Population Size stepper (10-200, step 10).
    populationStepper_ = LVGLBuilder::actionStepper(rightColumn)
                             .label("Population")
                             .range(10, 200)
                             .step(10)
                             .value(evolutionConfig_.populationSize)
                             .valueFormat("%.0f")
                             .valueScale(1.0)
                             .width(LV_PCT(95))
                             .callback(onPopulationChanged, this)
                             .buildOrLog();

    // Max Generations stepper (0-1000, step 10). 0 means infinite.
    generationsStepper_ = LVGLBuilder::actionStepper(rightColumn)
                              .label("Generations")
                              .range(0, 1000)
                              .step(10)
                              .value(evolutionConfig_.maxGenerations)
                              .valueFormat("%.0f")
                              .valueScale(1.0)
                              .width(LV_PCT(95))
                              .callback(onGenerationsChanged, this)
                              .buildOrLog();

    // Mutation Rate stepper (0-20% with 0.1% precision).
    // Internal value 0-200, displayed as 0.0-20.0%.
    mutationRateStepper_ = LVGLBuilder::actionStepper(rightColumn)
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
    tournamentSizeStepper_ = LVGLBuilder::actionStepper(rightColumn)
                                 .label("Tournament Size")
                                 .range(2, 10)
                                 .step(1)
                                 .value(evolutionConfig_.tournamentSize)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(95))
                                 .callback(onTournamentSizeChanged, this)
                                 .buildOrLog();

    // Max Sim Time stepper (10-1800 seconds, step 30).
    // Displayed in seconds.
    maxSimTimeStepper_ = LVGLBuilder::actionStepper(rightColumn)
                             .label("Max Sim Time (s)")
                             .range(10, 1800)
                             .step(30)
                             .value(static_cast<int32_t>(evolutionConfig_.maxSimulationTime))
                             .valueFormat("%.0f")
                             .valueScale(1.0)
                             .width(LV_PCT(95))
                             .callback(onMaxSimTimeChanged, this)
                             .buildOrLog();

    // Target CPU % stepper (0-100, step 5). 0 means disabled.
    targetCpuStepper_ = LVGLBuilder::actionStepper(rightColumn)
                            .label("Target CPU %")
                            .range(0, 100)
                            .step(5)
                            .value(evolutionConfig_.targetCpuPercent)
                            .valueFormat("%.0f")
                            .valueScale(1.0)
                            .width(LV_PCT(95))
                            .callback(onTargetCpuChanged, this)
                            .buildOrLog();

    // Status label (shows "Training in progress" when started).
    statusLabel_ = lv_label_create(rightColumn);
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
    setStepperEnabled(targetCpuStepper_, enabled);

    updateButtonVisibility();

    if (statusLabel_) {
        if (evolutionStarted_) {
            lv_label_set_text(statusLabel_, "Training in progress...");
        }
        else {
            lv_label_set_text(statusLabel_, "");
        }
    }
}

void EvolutionConfigPanel::updateButtonVisibility()
{
    if (startButton_) {
        if (evolutionStarted_) {
            lv_obj_add_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (stopButton_) {
        if (evolutionStarted_) {
            lv_obj_clear_flag(stopButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(stopButton_, LV_OBJ_FLAG_HIDDEN);
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
    if (self->trainingSpec_.population.size() > 2) {
        self->trainingSpec_.population.resize(2);
    }

    auto totalPopulation = [](const TrainingSpec& spec) {
        int total = 0;
        for (const auto& entry : spec.population) {
            total += entry.count;
        }
        return total;
    };

    auto normalizeEntry = [](PopulationSpec& spec) {
        const bool requiresGenome = (spec.brainKind == TrainingBrainKind::NeuralNet);
        if (!requiresGenome) {
            spec.seedGenomes.clear();
            spec.randomCount = 0;
            return;
        }
        if (static_cast<int>(spec.seedGenomes.size()) > spec.count) {
            spec.seedGenomes.resize(spec.count);
        }
        spec.randomCount = spec.count - static_cast<int>(spec.seedGenomes.size());
    };

    auto ensureDefaultPopulation = [&](TrainingSpec& spec, int desiredTotal) {
        if (!spec.population.empty()) {
            return;
        }
        PopulationSpec entry;
        entry.scenarioId = spec.scenarioId;
        switch (spec.organismType) {
            case OrganismType::TREE:
                entry.brainKind = TrainingBrainKind::NeuralNet;
                break;
            case OrganismType::DUCK:
                entry.brainKind = TrainingBrainKind::Random;
                break;
            case OrganismType::GOOSE:
                entry.brainKind = TrainingBrainKind::Random;
                break;
            default:
                entry.brainKind = TrainingBrainKind::Random;
                break;
        }
        entry.count = desiredTotal;
        normalizeEntry(entry);
        spec.population.push_back(entry);
    };

    ensureDefaultPopulation(self->trainingSpec_, value);

    int currentTotal = totalPopulation(self->trainingSpec_);
    int delta = value - currentTotal;
    if (delta != 0) {
        PopulationSpec& primary = self->trainingSpec_.population[0];
        if (delta > 0) {
            primary.count += delta;
        }
        else {
            int remaining = -delta;
            const int minPrimary = 10;
            const int reducible = std::max(0, primary.count - minPrimary);
            const int reducePrimary = std::min(reducible, remaining);
            primary.count -= reducePrimary;
            remaining -= reducePrimary;
            if (remaining > 0 && self->trainingSpec_.population.size() > 1) {
                PopulationSpec& secondary = self->trainingSpec_.population[1];
                secondary.count = std::max(0, secondary.count - remaining);
                if (secondary.count == 0) {
                    self->trainingSpec_.population.pop_back();
                }
            }
        }
    }

    for (auto& entry : self->trainingSpec_.population) {
        normalizeEntry(entry);
    }
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

void EvolutionConfigPanel::onStartClicked(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionConfigPanel: Start button clicked");

    StartEvolutionButtonClickedEvent evt;
    evt.evolution = self->evolutionConfig_;
    evt.mutation = self->mutationConfig_;
    evt.training = self->trainingSpec_;
    self->eventSink_.queueEvent(evt);
}

void EvolutionConfigPanel::onTargetCpuChanged(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->targetCpuStepper_) return;

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->targetCpuStepper_);
    self->evolutionConfig_.targetCpuPercent = value;
    spdlog::debug("EvolutionConfigPanel: Target CPU changed to {}%", value);
}

void EvolutionConfigPanel::onStopClicked(lv_event_t* e)
{
    EvolutionConfigPanel* self = static_cast<EvolutionConfigPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    spdlog::info("EvolutionConfigPanel: Stop button clicked");

    self->eventSink_.queueEvent(StopTrainingClickedEvent{});
}

} // namespace Ui
} // namespace DirtSim
