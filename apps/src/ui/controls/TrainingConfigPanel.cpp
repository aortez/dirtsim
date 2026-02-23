#include "TrainingConfigPanel.h"
#include "ExpandablePanel.h"
#include "TrainingPopulationPanel.h"
#include "core/LoggingChannels.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "ui/controls/IconRail.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {

namespace {
constexpr uint32_t kStatusReadyColor = 0x00CC66;
constexpr uint32_t kStatusCompleteColor = 0xFFDD66;
constexpr int kMinLeftColumnWidth = 140;
constexpr int kMinRightColumnWidth = 120;
} // namespace

TrainingConfigPanel::TrainingConfigPanel(
    lv_obj_t* container,
    EventSink& eventSink,
    ExpandablePanel* panel,
    Network::WebSocketServiceInterface* wsService,
    bool evolutionStarted,
    EvolutionConfig& evolutionConfig,
    MutationConfig& mutationConfig,
    TrainingSpec& trainingSpec,
    int& streamIntervalMs,
    bool& bestPlaybackEnabled,
    int& bestPlaybackIntervalMs)
    : container_(container),
      eventSink_(eventSink),
      panel_(panel),
      wsService_(wsService),
      evolutionStarted_(evolutionStarted),
      evolutionConfig_(evolutionConfig),
      mutationConfig_(mutationConfig),
      trainingSpec_(trainingSpec),
      streamIntervalMs_(streamIntervalMs),
      bestPlaybackEnabled_(bestPlaybackEnabled),
      bestPlaybackIntervalMs_(bestPlaybackIntervalMs)
{
    collapsedWidth_ = ExpandablePanel::DefaultWidth;
    const int displayWidth = lv_disp_get_hor_res(lv_disp_get_default());
    const int maxWidth = displayWidth > 0 ? displayWidth - IconRail::RAIL_WIDTH : 0;
    int panelWidth = ExpandablePanel::DefaultWidth * 3;
    if (panelWidth > maxWidth && maxWidth > 0) {
        panelWidth = maxWidth;
    }
    if (panelWidth < ExpandablePanel::DefaultWidth) {
        panelWidth = ExpandablePanel::DefaultWidth;
    }
    expandedWidth_ = panelWidth;
    const int maxLeftWidth = std::max(0, expandedWidth_ - kMinRightColumnWidth);
    leftColumnWidth_ = std::min(ExpandablePanel::DefaultWidth, maxLeftWidth);
    leftColumnWidth_ = std::max(kMinLeftColumnWidth, leftColumnWidth_);
    if (maxLeftWidth < kMinLeftColumnWidth) {
        leftColumnWidth_ = maxLeftWidth;
    }
    leftColumnWidth_ = static_cast<int>(leftColumnWidth_ * 0.6f);
    leftColumnWidth_ = std::max(kMinLeftColumnWidth, leftColumnWidth_);
    leftColumnWidth_ = std::min(leftColumnWidth_, maxLeftWidth);

    createLayout();
    setRightColumnVisible(false);
    updateControlsEnabled();

    LOG_INFO(Controls, "TrainingConfigPanel: Initialized (started={})", evolutionStarted_);
}

TrainingConfigPanel::~TrainingConfigPanel()
{
    LOG_INFO(Controls, "TrainingConfigPanel: Destroyed");
}

void TrainingConfigPanel::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    if (started) {
        evolutionCompleted_ = false;
    }
    updateControlsEnabled();

    if (trainingPopulationPanel_) {
        trainingPopulationPanel_->setEvolutionStarted(started);
    }
}

void TrainingConfigPanel::setEvolutionCompleted()
{
    evolutionStarted_ = false;
    evolutionCompleted_ = true;
    updateControlsEnabled();

    if (trainingPopulationPanel_) {
        trainingPopulationPanel_->setEvolutionCompleted();
    }
}

void TrainingConfigPanel::setStreamIntervalMs(int value)
{
    streamIntervalMs_ = value;
    if (streamIntervalStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(streamIntervalStepper_, value);
    }
}

void TrainingConfigPanel::setBestPlaybackEnabled(bool enabled)
{
    bestPlaybackEnabled_ = enabled;
    if (bestPlaybackToggle_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(bestPlaybackToggle_, enabled);
    }
    updateControlsEnabled();
}

void TrainingConfigPanel::setBestPlaybackIntervalMs(int value)
{
    bestPlaybackIntervalMs_ = std::max(1, value);
    if (bestPlaybackIntervalStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(
            bestPlaybackIntervalStepper_, bestPlaybackIntervalMs_);
    }
}

void TrainingConfigPanel::addSeedGenome(const GenomeId& genomeId)
{
    if (!trainingPopulationPanel_) {
        return;
    }

    trainingPopulationPanel_->addSeedGenome(genomeId);
}

void TrainingConfigPanel::showView(View view)
{
    if (view == View::None) {
        currentView_ = View::None;
        setRightColumnVisible(false);
        updateToggleLabels();
        return;
    }

    const bool rightVisible = rightColumn_ && !lv_obj_has_flag(rightColumn_, LV_OBJ_FLAG_HIDDEN);
    if (view == currentView_ && rightVisible) {
        currentView_ = View::None;
        setRightColumnVisible(false);
        updateToggleLabels();
        return;
    }

    currentView_ = view;
    setRightColumnVisible(true);

    if (evolutionView_) {
        if (view == View::Evolution) {
            lv_obj_clear_flag(evolutionView_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(evolutionView_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
        else {
            lv_obj_add_flag(evolutionView_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(evolutionView_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    }

    if (populationView_) {
        if (view == View::Population) {
            lv_obj_clear_flag(populationView_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(populationView_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
        else {
            lv_obj_add_flag(populationView_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(populationView_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
    }

    updateToggleLabels();
}

void TrainingConfigPanel::createLayout()
{
    lv_obj_t* columns = lv_obj_create(container_);
    lv_obj_set_size(columns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(columns, 0, 0);
    lv_obj_set_style_pad_all(columns, 0, 0);
    lv_obj_set_style_pad_column(columns, 12, 0);
    lv_obj_set_style_pad_row(columns, 0, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);

    createLeftColumn(columns);
    createRightColumn(columns);
}

void TrainingConfigPanel::createLeftColumn(lv_obj_t* parent)
{
    leftColumn_ = lv_obj_create(parent);
    lv_obj_set_size(leftColumn_, leftColumnWidth_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(leftColumn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftColumn_, 0, 0);
    lv_obj_set_style_pad_all(leftColumn_, 0, 0);
    lv_obj_set_style_pad_row(leftColumn_, 10, 0);
    lv_obj_set_flex_flow(leftColumn_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        leftColumn_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(leftColumn_, LV_OBJ_FLAG_SCROLLABLE);

    startButton_ = LVGLBuilder::actionButton(leftColumn_)
                       .text("Start")
                       .icon(LV_SYMBOL_PLAY)
                       .mode(LVGLBuilder::ActionMode::Push)
                       .width(LV_PCT(95))
                       .height(80)
                       .backgroundColor(0x00AA66)
                       .callback(onStartClicked, this)
                       .buildOrLog();

    lv_obj_t* configsLabel = lv_label_create(leftColumn_);
    lv_label_set_text(configsLabel, "Configs");
    lv_obj_set_style_text_color(configsLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(configsLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(configsLabel, 6, 0);
    lv_obj_set_style_pad_bottom(configsLabel, 2, 0);

    evolutionButton_ = LVGLBuilder::actionButton(leftColumn_)
                           .text("Evolution")
                           .icon(LV_SYMBOL_RIGHT)
                           .iconPositionRight()
                           .width(LV_PCT(95))
                           .height(LVGLBuilder::Style::ACTION_SIZE)
                           .layoutRow()
                           .alignLeft()
                           .callback(onEvolutionSelected, this)
                           .buildOrLog();

    populationButton_ = LVGLBuilder::actionButton(leftColumn_)
                            .text("Population")
                            .icon(LV_SYMBOL_RIGHT)
                            .iconPositionRight()
                            .width(LV_PCT(95))
                            .height(LVGLBuilder::Style::ACTION_SIZE)
                            .layoutRow()
                            .alignLeft()
                            .callback(onPopulationSelected, this)
                            .buildOrLog();

    updateToggleLabels();
}

void TrainingConfigPanel::createRightColumn(lv_obj_t* parent)
{
    rightColumn_ = lv_obj_create(parent);
    lv_obj_set_size(rightColumn_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(rightColumn_, 1);
    lv_obj_set_style_bg_opa(rightColumn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightColumn_, 0, 0);
    lv_obj_set_style_pad_all(rightColumn_, 0, 0);
    lv_obj_set_style_pad_row(rightColumn_, 8, 0);
    lv_obj_set_flex_flow(rightColumn_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        rightColumn_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rightColumn_, LV_OBJ_FLAG_SCROLLABLE);

    evolutionView_ = lv_obj_create(rightColumn_);
    lv_obj_set_size(evolutionView_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(evolutionView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(evolutionView_, 0, 0);
    lv_obj_set_style_pad_all(evolutionView_, 0, 0);
    lv_obj_set_style_pad_row(evolutionView_, 8, 0);
    lv_obj_set_flex_flow(evolutionView_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        evolutionView_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(evolutionView_, LV_OBJ_FLAG_SCROLLABLE);

    populationView_ = lv_obj_create(rightColumn_);
    lv_obj_set_size(populationView_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(populationView_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(populationView_, 0, 0);
    lv_obj_set_style_pad_all(populationView_, 0, 0);
    lv_obj_set_style_pad_row(populationView_, 8, 0);
    lv_obj_set_flex_flow(populationView_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        populationView_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(populationView_, LV_OBJ_FLAG_SCROLLABLE);

    createEvolutionView(evolutionView_);
    createPopulationView(populationView_);
}

void TrainingConfigPanel::createEvolutionView(lv_obj_t* parent)
{
    lv_obj_t* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, "Evolution Config");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 8, 0);

    populationStepper_ = LVGLBuilder::actionStepper(parent)
                             .label("Population")
                             .range(0, 9999)
                             .step(1)
                             .value(evolutionConfig_.populationSize)
                             .valueFormat("%.0f")
                             .valueScale(1.0)
                             .width(LV_PCT(95))
                             .callback(onPopulationChanged, this)
                             .buildOrLog();

    const int32_t initialGenerations = evolutionConfig_.maxGenerations;
    generationsStepper_ = LVGLBuilder::actionStepper(parent)
                              .label("Generations")
                              .range(0, 1000)
                              .step(initialGenerations <= 10 ? 1 : 10)
                              .value(initialGenerations)
                              .valueFormat("%.0f")
                              .valueScale(1.0)
                              .width(LV_PCT(95))
                              .callback(onGenerationsChanged, this)
                              .buildOrLog();

    mutationBudgetToggle_ = LVGLBuilder::actionButton(parent)
                                .text("Budgeted Mutation")
                                .mode(LVGLBuilder::ActionMode::Toggle)
                                .checked(mutationConfig_.useBudget)
                                .width(LV_PCT(95))
                                .height(LVGLBuilder::Style::ACTION_SIZE)
                                .layoutRow()
                                .alignLeft()
                                .callback(onMutationBudgetToggled, this)
                                .buildOrLog();

    mutationPerturbationsStepper_ = LVGLBuilder::actionStepper(parent)
                                        .label("Perturbations/Offspring")
                                        .range(0, 5000)
                                        .step(10)
                                        .value(mutationConfig_.perturbationsPerOffspring)
                                        .valueFormat("%.0f")
                                        .valueScale(1.0)
                                        .width(LV_PCT(95))
                                        .callback(onMutationPerturbationsChanged, this)
                                        .buildOrLog();

    mutationResetsStepper_ = LVGLBuilder::actionStepper(parent)
                                 .label("Resets/Offspring")
                                 .range(0, 200)
                                 .step(1)
                                 .value(mutationConfig_.resetsPerOffspring)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(95))
                                 .callback(onMutationResetsChanged, this)
                                 .buildOrLog();

    sigmaStepper_ = LVGLBuilder::actionStepper(parent)
                        .label("Mutation Sigma")
                        .range(0, 300)
                        .step(1)
                        .value(static_cast<int32_t>(mutationConfig_.sigma * 1000.0))
                        .valueFormat("%.3f")
                        .valueScale(0.001)
                        .width(LV_PCT(95))
                        .callback(onSigmaChanged, this)
                        .buildOrLog();

    mutationRateStepper_ = LVGLBuilder::actionStepper(parent)
                               .label("Mutation Rate (legacy)")
                               .range(0, 200)
                               .step(1)
                               .value(static_cast<int32_t>(mutationConfig_.rate * 1000.0))
                               .valueFormat("%.1f%%")
                               .valueScale(0.1)
                               .width(LV_PCT(95))
                               .callback(onMutationRateChanged, this)
                               .buildOrLog();

    resetRateStepper_ = LVGLBuilder::actionStepper(parent)
                            .label("Reset Rate (legacy)")
                            .range(0, 10000)
                            .step(1)
                            .value(static_cast<int32_t>(mutationConfig_.resetRate * 1'000'000.0))
                            .valueFormat("%.4f%%")
                            .valueScale(0.0001)
                            .width(LV_PCT(95))
                            .callback(onResetRateChanged, this)
                            .buildOrLog();

    tournamentSizeStepper_ = LVGLBuilder::actionStepper(parent)
                                 .label("Tournament Size")
                                 .range(2, 10)
                                 .step(1)
                                 .value(evolutionConfig_.tournamentSize)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(95))
                                 .callback(onTournamentSizeChanged, this)
                                 .buildOrLog();

    const int32_t maxSimTimeValue = static_cast<int32_t>(evolutionConfig_.maxSimulationTime);
    const int32_t maxSimTimeStep = maxSimTimeValue <= 60 ? 10 : 30;
    maxSimTimeStepper_ = LVGLBuilder::actionStepper(parent)
                             .label("Max Sim Time (s)")
                             .range(10, 1800)
                             .step(maxSimTimeStep)
                             .value(maxSimTimeValue)
                             .valueFormat("%.0f")
                             .valueScale(1.0)
                             .width(LV_PCT(95))
                             .callback(onMaxSimTimeChanged, this)
                             .buildOrLog();

    streamIntervalStepper_ = LVGLBuilder::actionStepper(parent)
                                 .label("Stream Interval (ms)")
                                 .range(0, 5000)
                                 .step(100)
                                 .value(streamIntervalMs_)
                                 .valueFormat("%.0f")
                                 .valueScale(1.0)
                                 .width(LV_PCT(95))
                                 .callback(onStreamIntervalChanged, this)
                                 .buildOrLog();

    bestPlaybackToggle_ = LVGLBuilder::actionButton(parent)
                              .text("Best Playback")
                              .mode(LVGLBuilder::ActionMode::Toggle)
                              .checked(bestPlaybackEnabled_)
                              .width(LV_PCT(95))
                              .height(LVGLBuilder::Style::ACTION_SIZE)
                              .layoutRow()
                              .alignLeft()
                              .callback(onBestPlaybackToggled, this)
                              .buildOrLog();

    bestPlaybackIntervalStepper_ = LVGLBuilder::actionStepper(parent)
                                       .label("Best Playback (ms)")
                                       .range(1, 5000)
                                       .step(1)
                                       .value(bestPlaybackIntervalMs_)
                                       .valueFormat("%.0f")
                                       .valueScale(1.0)
                                       .width(LV_PCT(95))
                                       .callback(onBestPlaybackIntervalChanged, this)
                                       .buildOrLog();

    statusLabel_ = lv_label_create(parent);
    lv_label_set_text(statusLabel_, "");
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(kStatusReadyColor), 0);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(statusLabel_, 8, 0);
}

void TrainingConfigPanel::createPopulationView(lv_obj_t* parent)
{
    trainingPopulationPanel_ = std::make_unique<TrainingPopulationPanel>(
        parent, eventSink_, wsService_, evolutionStarted_, evolutionConfig_, trainingSpec_);
    trainingPopulationPanel_->setPopulationTotalChangedCallback([this](int total) {
        if (populationStepper_) {
            LVGLBuilder::ActionStepperBuilder::setValue(populationStepper_, total);
        }
    });
    trainingPopulationPanel_->setSpecUpdatedCallback(
        [this]() { queueTrainingConfigUpdatedEvent(); });
}

void TrainingConfigPanel::setRightColumnVisible(bool visible)
{
    if (!rightColumn_) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(rightColumn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rightColumn_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        if (panel_) {
            panel_->setWidth(expandedWidth_);
        }
        if (leftColumn_) {
            lv_obj_set_width(leftColumn_, leftColumnWidth_);
        }
    }
    else {
        lv_obj_add_flag(rightColumn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rightColumn_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        if (panel_) {
            panel_->setWidth(collapsedWidth_);
        }
        if (leftColumn_) {
            lv_obj_set_width(leftColumn_, leftColumnWidth_);
        }
    }
}

void TrainingConfigPanel::updateControlsEnabled()
{
    auto setEnabled = [](lv_obj_t* control, bool enabled) {
        if (!control) return;
        if (enabled) {
            lv_obj_clear_state(control, LV_STATE_DISABLED);
            lv_obj_set_style_opa(control, LV_OPA_COVER, 0);
        }
        else {
            lv_obj_add_state(control, LV_STATE_DISABLED);
            lv_obj_set_style_opa(control, LV_OPA_50, 0);
        }
    };

    const bool enabled = !evolutionStarted_;
    setEnabled(evolutionButton_, enabled);
    setEnabled(populationButton_, enabled);
    setEnabled(populationStepper_, enabled);
    setEnabled(generationsStepper_, enabled);
    setEnabled(tournamentSizeStepper_, enabled);
    setEnabled(maxSimTimeStepper_, enabled);
    setEnabled(streamIntervalStepper_, true);
    setEnabled(bestPlaybackToggle_, true);
    setEnabled(bestPlaybackIntervalStepper_, bestPlaybackEnabled_);

    setEnabled(mutationBudgetToggle_, enabled);
    if (enabled) {
        setEnabled(sigmaStepper_, true);
        if (mutationConfig_.useBudget) {
            setEnabled(mutationPerturbationsStepper_, true);
            setEnabled(mutationResetsStepper_, true);
            setEnabled(mutationRateStepper_, false);
            setEnabled(resetRateStepper_, false);
        }
        else {
            setEnabled(mutationPerturbationsStepper_, false);
            setEnabled(mutationResetsStepper_, false);
            setEnabled(mutationRateStepper_, true);
            setEnabled(resetRateStepper_, true);
        }
    }
    else {
        setEnabled(mutationPerturbationsStepper_, false);
        setEnabled(mutationResetsStepper_, false);
        setEnabled(sigmaStepper_, false);
        setEnabled(mutationRateStepper_, false);
        setEnabled(resetRateStepper_, false);
    }

    if (startButton_) {
        if (evolutionStarted_) {
            lv_obj_add_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(startButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (evolutionStarted_) {
        updateStatusLabel("Training in progress...", lv_color_hex(kStatusReadyColor));
    }
    else if (evolutionCompleted_) {
        updateStatusLabel("Complete!", lv_color_hex(kStatusCompleteColor));
    }
    else {
        updateStatusLabel("", lv_color_hex(kStatusReadyColor));
    }
}

void TrainingConfigPanel::updateToggleLabels()
{
    const bool rightVisible = rightColumn_ && !lv_obj_has_flag(rightColumn_, LV_OBJ_FLAG_HIDDEN);
    const View activeView = rightVisible ? currentView_ : View::None;

    if (evolutionButton_) {
        const char* symbol = activeView == View::Evolution ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT;
        LVGLBuilder::ActionButtonBuilder::setIcon(evolutionButton_, symbol);
    }

    if (populationButton_) {
        const char* symbol = activeView == View::Population ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT;
        LVGLBuilder::ActionButtonBuilder::setIcon(populationButton_, symbol);
    }
}

void TrainingConfigPanel::updateGenerationsStep(int32_t value)
{
    const int32_t step = value <= 10 ? 1 : 10;
    LVGLBuilder::ActionStepperBuilder::setStep(generationsStepper_, step);
}

void TrainingConfigPanel::updateMaxSimTimeStep(int32_t value)
{
    if (!maxSimTimeStepper_) {
        return;
    }

    const int32_t step = value <= 60 ? 10 : 30;
    LVGLBuilder::ActionStepperBuilder::setStep(maxSimTimeStepper_, step);
}

void TrainingConfigPanel::updateStatusLabel(const char* text, lv_color_t color)
{
    if (!statusLabel_) {
        return;
    }

    lv_label_set_text(statusLabel_, text);
    lv_obj_set_style_text_color(statusLabel_, color, 0);
}

void TrainingConfigPanel::queueTrainingConfigUpdatedEvent() const
{
    eventSink_.queueEvent(
        TrainingConfigUpdatedEvent{
            .evolution = evolutionConfig_,
            .mutation = mutationConfig_,
            .training = trainingSpec_,
        });
}

void TrainingConfigPanel::onStartClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    StartEvolutionButtonClickedEvent evt;
    evt.evolution = self->evolutionConfig_;
    evt.mutation = self->mutationConfig_;
    evt.training = self->trainingSpec_;
    self->eventSink_.queueEvent(evt);
}

void TrainingConfigPanel::onEvolutionSelected(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self) return;
    self->showView(View::Evolution);
}

void TrainingConfigPanel::onPopulationSelected(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self) return;
    self->showView(View::Population);
}

void TrainingConfigPanel::onPopulationChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->populationStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->populationStepper_);
    if (self->trainingPopulationPanel_) {
        self->trainingPopulationPanel_->setPopulationTotal(value);
    }
    else {
        self->evolutionConfig_.populationSize = value;
        self->queueTrainingConfigUpdatedEvent();
    }
}

void TrainingConfigPanel::onGenerationsChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->generationsStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->generationsStepper_);
    self->evolutionConfig_.maxGenerations = value;
    self->updateGenerationsStep(value);
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onMutationBudgetToggled(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->mutationBudgetToggle_) return;

    self->mutationConfig_.useBudget =
        LVGLBuilder::ActionButtonBuilder::isChecked(self->mutationBudgetToggle_);
    self->updateControlsEnabled();
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onMutationPerturbationsChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->mutationPerturbationsStepper_) return;

    const int32_t value =
        LVGLBuilder::ActionStepperBuilder::getValue(self->mutationPerturbationsStepper_);
    self->mutationConfig_.perturbationsPerOffspring = value;
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onMutationResetsChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->mutationResetsStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->mutationResetsStepper_);
    self->mutationConfig_.resetsPerOffspring = value;
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onMutationRateChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->mutationRateStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->mutationRateStepper_);
    self->mutationConfig_.rate = value / 1000.0;
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onResetRateChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->resetRateStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->resetRateStepper_);
    self->mutationConfig_.resetRate = value / 1'000'000.0;
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onSigmaChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->sigmaStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->sigmaStepper_);
    self->mutationConfig_.sigma = value / 1000.0;
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onTournamentSizeChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->tournamentSizeStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->tournamentSizeStepper_);
    self->evolutionConfig_.tournamentSize = value;
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onMaxSimTimeChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->maxSimTimeStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->maxSimTimeStepper_);
    self->evolutionConfig_.maxSimulationTime = static_cast<double>(value);
    self->updateMaxSimTimeStep(value);
    self->queueTrainingConfigUpdatedEvent();
}

void TrainingConfigPanel::onStreamIntervalChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->streamIntervalStepper_) return;

    const int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->streamIntervalStepper_);
    self->streamIntervalMs_ = value;
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = value,
            .bestPlaybackEnabled = self->bestPlaybackEnabled_,
            .bestPlaybackIntervalMs = self->bestPlaybackIntervalMs_,
        });
}

void TrainingConfigPanel::onBestPlaybackToggled(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->bestPlaybackToggle_) return;

    self->bestPlaybackEnabled_ =
        LVGLBuilder::ActionButtonBuilder::isChecked(self->bestPlaybackToggle_);
    self->updateControlsEnabled();
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = self->streamIntervalMs_,
            .bestPlaybackEnabled = self->bestPlaybackEnabled_,
            .bestPlaybackIntervalMs = self->bestPlaybackIntervalMs_,
        });
}

void TrainingConfigPanel::onBestPlaybackIntervalChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingConfigPanel*>(lv_event_get_user_data(e));
    if (!self || !self->bestPlaybackIntervalStepper_) return;

    const int32_t value =
        LVGLBuilder::ActionStepperBuilder::getValue(self->bestPlaybackIntervalStepper_);
    self->bestPlaybackIntervalMs_ = std::max(1, value);
    self->eventSink_.queueEvent(
        TrainingStreamConfigChangedEvent{
            .intervalMs = self->streamIntervalMs_,
            .bestPlaybackEnabled = self->bestPlaybackEnabled_,
            .bestPlaybackIntervalMs = self->bestPlaybackIntervalMs_,
        });
}

} // namespace Ui
} // namespace DirtSim
