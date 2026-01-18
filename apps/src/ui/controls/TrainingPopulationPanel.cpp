#include "TrainingPopulationPanel.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kPopulationMin = 10;
constexpr int kPopulationMax = 200;
constexpr int kPopulationStep = 10;

std::vector<TrainingPopulationPanel::BrainOption> getBrainOptions(OrganismType organismType)
{
    switch (organismType) {
        case OrganismType::TREE:
            return {
                { TrainingBrainKind::NeuralNet, true },
                { TrainingBrainKind::RuleBased, false },
                { TrainingBrainKind::RuleBased2, false },
            };
        case OrganismType::DUCK:
            return {
                { TrainingBrainKind::Random, false },
                { TrainingBrainKind::WallBouncing, false },
                { TrainingBrainKind::DuckBrain2, false },
            };
        case OrganismType::GOOSE:
            return {
                { TrainingBrainKind::Random, false },
            };
        default:
            return { { TrainingBrainKind::Random, false } };
    }
}

const char* organismLabel(OrganismType organismType)
{
    switch (organismType) {
        case OrganismType::TREE:
            return "Tree";
        case OrganismType::DUCK:
            return "Duck";
        case OrganismType::GOOSE:
            return "Goose";
        default:
            return "Unknown";
    }
}

void applyCounts(PopulationSpec& spec, int count, bool requiresGenome)
{
    spec.count = count;
    if (!requiresGenome) {
        spec.seedGenomes.clear();
        spec.randomCount = 0;
        return;
    }

    if (static_cast<int>(spec.seedGenomes.size()) > spec.count) {
        spec.seedGenomes.resize(spec.count);
    }
    spec.randomCount = spec.count - static_cast<int>(spec.seedGenomes.size());
}

bool requiresGenome(
    const std::vector<TrainingPopulationPanel::BrainOption>& options, const std::string& kind)
{
    for (const auto& option : options) {
        if (option.kind == kind) {
            return option.requiresGenome;
        }
    }
    return false;
}

void clampCounts(int& countA, int& countB)
{
    countA = std::max(kPopulationStep, countA);
    countB = std::max(0, countB);

    int total = countA + countB;
    if (total > kPopulationMax) {
        int excess = total - kPopulationMax;
        if (countB >= excess) {
            countB -= excess;
        }
        else {
            countA = std::max(kPopulationStep, countA - (excess - countB));
            countB = 0;
        }
    }

    total = countA + countB;
    if (total < kPopulationMin) {
        countA = std::max(kPopulationStep, kPopulationMin - countB);
    }
}

lv_obj_t* getActionButtonLabel(lv_obj_t* container)
{
    if (!container) {
        return nullptr;
    }
    lv_obj_t* button = lv_obj_get_child(container, 0);
    if (!button) {
        return nullptr;
    }
    const uint32_t count = lv_obj_get_child_cnt(button);
    if (count == 0) {
        return nullptr;
    }
    return lv_obj_get_child(button, count - 1);
}

void setActionButtonText(lv_obj_t* container, const std::string& text)
{
    lv_obj_t* label = getActionButtonLabel(container);
    if (label) {
        lv_label_set_text(label, text.c_str());
    }
}
} // namespace

TrainingPopulationPanel::TrainingPopulationPanel(
    lv_obj_t* container,
    EventSink& eventSink,
    bool evolutionStarted,
    EvolutionConfig& evolutionConfig,
    TrainingSpec& trainingSpec)
    : container_(container),
      eventSink_(eventSink),
      evolutionStarted_(evolutionStarted),
      evolutionConfig_(evolutionConfig),
      trainingSpec_(trainingSpec)
{
    scenarioOptions_ = {
        Scenario::EnumType::Benchmark,       Scenario::EnumType::Clock,
        Scenario::EnumType::DamBreak,        Scenario::EnumType::Empty,
        Scenario::EnumType::GooseTest,       Scenario::EnumType::Lights,
        Scenario::EnumType::Raining,         Scenario::EnumType::Sandbox,
        Scenario::EnumType::TreeGermination, Scenario::EnumType::WaterEqualization,
    };
    scenarioLabels_.reserve(scenarioOptions_.size());
    for (const auto& scenarioId : scenarioOptions_) {
        scenarioLabels_.push_back(Scenario::toString(scenarioId));
    }

    organismOptions_ = { OrganismType::TREE, OrganismType::DUCK, OrganismType::GOOSE };
    organismLabels_ = { "Tree", "Duck", "Goose" };

    selectedScenario_ = trainingSpec_.scenarioId;
    selectedOrganism_ = trainingSpec_.organismType;
    brainOptions_ = getBrainOptions(selectedOrganism_);

    viewController_ = std::make_unique<PanelViewController>(container_);
    lv_obj_t* mainView = viewController_->createView("main");
    createMainView(mainView);

    lv_obj_t* scenarioView = viewController_->createView("scenario_select");
    createScenarioSelectView(scenarioView);

    lv_obj_t* organismView = viewController_->createView("organism_select");
    createOrganismSelectView(organismView);

    lv_obj_t* brainAView = viewController_->createView("brain_a_select");
    createBrainSelectView(
        brainAView, "Brain Type A", false, brainAButtonToValue_, onBrainASelected);

    lv_obj_t* brainBView = viewController_->createView("brain_b_select");
    createBrainSelectView(brainBView, "Brain Type B", true, brainBButtonToValue_, onBrainBSelected);

    viewController_->showView("main");

    refreshFromSpec();

    spdlog::info("TrainingPopulationPanel: Initialized (started={})", evolutionStarted_);
}

TrainingPopulationPanel::~TrainingPopulationPanel()
{
    spdlog::info("TrainingPopulationPanel: Destroyed");
}

void TrainingPopulationPanel::createMainView(lv_obj_t* view)
{
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Population Setup");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 8, 0);

    auto createColumn = [&](lv_obj_t* parent) {
        lv_obj_t* column = lv_obj_create(parent);
        lv_obj_set_size(column, LV_PCT(48), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(column, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(column, 0, 0);
        lv_obj_set_style_pad_all(column, 0, 0);
        lv_obj_set_style_pad_row(column, 6, 0);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);
        return column;
    };

    lv_obj_t* columns = lv_obj_create(view);
    lv_obj_set_size(columns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(columns, 0, 0);
    lv_obj_set_style_pad_all(columns, 0, 0);
    lv_obj_set_style_pad_gap(columns, 8, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        columns, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* leftColumn = createColumn(columns);
    lv_obj_t* rightColumn = createColumn(columns);

    scenarioButton_ = LVGLBuilder::actionButton(leftColumn)
                          .text("Scenario: --")
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(100))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onScenarioButtonClicked, this)
                          .buildOrLog();

    organismButton_ = LVGLBuilder::actionButton(leftColumn)
                          .text("Organism Type: --")
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(100))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onOrganismButtonClicked, this)
                          .buildOrLog();

    brainAButton_ = LVGLBuilder::actionButton(leftColumn)
                        .text("Brain Type A: --")
                        .icon(LV_SYMBOL_RIGHT)
                        .width(LV_PCT(100))
                        .height(LVGLBuilder::Style::ACTION_SIZE)
                        .layoutRow()
                        .alignLeft()
                        .callback(onBrainAButtonClicked, this)
                        .buildOrLog();

    brainBButton_ = LVGLBuilder::actionButton(leftColumn)
                        .text("Brain Type B: None")
                        .icon(LV_SYMBOL_RIGHT)
                        .width(LV_PCT(100))
                        .height(LVGLBuilder::Style::ACTION_SIZE)
                        .layoutRow()
                        .alignLeft()
                        .callback(onBrainBButtonClicked, this)
                        .buildOrLog();

    totalCountLabel_ = lv_label_create(rightColumn);
    lv_label_set_text(totalCountLabel_, "Total: --");
    lv_obj_set_width(totalCountLabel_, LV_PCT(100));
    lv_obj_set_style_text_align(totalCountLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(totalCountLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(totalCountLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(totalCountLabel_, 2, 0);
    lv_obj_set_style_pad_bottom(totalCountLabel_, 4, 0);

    countAStepper_ = LVGLBuilder::actionStepper(rightColumn)
                         .label("Count A")
                         .range(kPopulationMin, kPopulationMax)
                         .step(kPopulationStep)
                         .value(evolutionConfig_.populationSize)
                         .valueFormat("%.0f")
                         .valueScale(1.0)
                         .width(LV_PCT(100))
                         .callback(onCountAChanged, this)
                         .buildOrLog();
    countBStepper_ = LVGLBuilder::actionStepper(rightColumn)
                         .label("Count B")
                         .range(0, kPopulationMax)
                         .step(kPopulationStep)
                         .value(0)
                         .valueFormat("%.0f")
                         .valueScale(1.0)
                         .width(LV_PCT(100))
                         .callback(onCountBChanged, this)
                         .buildOrLog();

    updateControlsEnabled();
}

void TrainingPopulationPanel::createScenarioSelectView(lv_obj_t* view)
{
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onSelectionBackClicked, this)
        .buildOrLog();

    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Scenario");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    scenarioButtonToValue_.clear();
    for (size_t i = 0; i < scenarioOptions_.size(); ++i) {
        const std::string& label = scenarioLabels_[i];
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(label.c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                scenarioButtonToValue_[button] = scenarioOptions_[i];
                lv_obj_add_event_cb(button, onScenarioSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void TrainingPopulationPanel::createOrganismSelectView(lv_obj_t* view)
{
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onSelectionBackClicked, this)
        .buildOrLog();

    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, "Organism Type");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    organismButtonToValue_.clear();
    for (size_t i = 0; i < organismOptions_.size(); ++i) {
        const std::string& label = organismLabels_[i];
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(label.c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                organismButtonToValue_[button] = organismOptions_[i];
                lv_obj_add_event_cb(button, onOrganismSelected, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void TrainingPopulationPanel::createBrainSelectView(
    lv_obj_t* view,
    const char* title,
    bool includeNone,
    std::unordered_map<lv_obj_t*, std::string>& buttonMap,
    lv_event_cb_t callback)
{
    lv_obj_clean(view);

    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onSelectionBackClicked, this)
        .buildOrLog();

    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    buttonMap.clear();
    if (includeNone) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text("None")
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonMap[button] = "";
                lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
            }
        }
    }

    for (const auto& option : brainOptions_) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(option.kind.c_str())
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutColumn()
                                  .buildOrLog();
        if (container) {
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                buttonMap[button] = option.kind;
                lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void TrainingPopulationPanel::updateControlsEnabled()
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
    setEnabled(scenarioButton_, enabled);
    setEnabled(organismButton_, enabled);
    setEnabled(brainAButton_, enabled);
    setEnabled(brainBButton_, enabled);
    setEnabled(countAStepper_, enabled);
    setEnabled(countBStepper_, enabled && !brainB_.empty());
}

void TrainingPopulationPanel::updateSelectorLabels()
{
    setActionButtonText(
        scenarioButton_, std::string("Scenario: ") + Scenario::toString(selectedScenario_));
    setActionButtonText(
        organismButton_, std::string("Organism Type: ") + organismLabel(selectedOrganism_));
    setActionButtonText(brainAButton_, std::string("Brain Type A: ") + brainA_);
    const std::string brainBText = brainB_.empty() ? "None" : brainB_;
    setActionButtonText(brainBButton_, std::string("Brain Type B: ") + brainBText);
}

void TrainingPopulationPanel::setEvolutionStarted(bool started)
{
    evolutionStarted_ = started;
    updateControlsEnabled();
}

void TrainingPopulationPanel::setEvolutionCompleted()
{
    evolutionStarted_ = false;
    updateControlsEnabled();
}

void TrainingPopulationPanel::refreshFromSpec()
{
    if (trainingSpec_.population.size() > 2) {
        trainingSpec_.population.resize(2);
    }

    selectedScenario_ = trainingSpec_.scenarioId;
    selectedOrganism_ = trainingSpec_.organismType;
    setBrainOptionsForOrganism(selectedOrganism_);

    if (trainingSpec_.population.empty()) {
        brainA_ = brainOptions_.front().kind;
        brainB_.clear();
        countA_ = evolutionConfig_.populationSize;
        countB_ = 0;
    }
    else {
        brainA_ = trainingSpec_.population[0].brainKind;
        countA_ = trainingSpec_.population[0].count;
        if (trainingSpec_.population.size() > 1) {
            brainB_ = trainingSpec_.population[1].brainKind;
            countB_ = trainingSpec_.population[1].count;
        }
        else {
            brainB_.clear();
            countB_ = 0;
        }
    }

    bool brainAValid = false;
    for (const auto& option : brainOptions_) {
        if (option.kind == brainA_) {
            brainAValid = true;
            break;
        }
    }
    if (!brainAValid) {
        brainA_ = brainOptions_.front().kind;
    }

    if (!brainB_.empty()) {
        bool found = false;
        for (const auto& option : brainOptions_) {
            if (option.kind == brainB_) {
                found = true;
                break;
            }
        }
        if (!found) {
            brainB_.clear();
            countB_ = 0;
        }
    }

    clampCounts(countA_, countB_);
    if (countB_ == 0) {
        brainB_.clear();
    }
    updatePopulationSpec();
    syncUiFromState();
}

void TrainingPopulationPanel::applySpec()
{
    clampCounts(countA_, countB_);
    if (countB_ == 0) {
        brainB_.clear();
    }
    updatePopulationSpec();
    syncUiFromState();
}

void TrainingPopulationPanel::setBrainOptionsForOrganism(OrganismType organismType)
{
    brainOptions_ = getBrainOptions(organismType);

    if (viewController_->hasView("brain_a_select")) {
        createBrainSelectView(
            viewController_->getView("brain_a_select"),
            "Brain Type A",
            false,
            brainAButtonToValue_,
            onBrainASelected);
    }
    if (viewController_->hasView("brain_b_select")) {
        createBrainSelectView(
            viewController_->getView("brain_b_select"),
            "Brain Type B",
            true,
            brainBButtonToValue_,
            onBrainBSelected);
    }
}

void TrainingPopulationPanel::syncUiFromState()
{
    ignoreEvents_ = true;

    updateSelectorLabels();
    if (totalCountLabel_) {
        const std::string text = "Total: " + std::to_string(countA_ + countB_);
        lv_label_set_text(totalCountLabel_, text.c_str());
    }

    if (countAStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(countAStepper_, countA_);
    }
    if (countBStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(countBStepper_, countB_);
    }

    updateControlsEnabled();
    ignoreEvents_ = false;
}

void TrainingPopulationPanel::updatePopulationSpec()
{
    trainingSpec_.scenarioId = selectedScenario_;
    trainingSpec_.organismType = selectedOrganism_;

    std::vector<PopulationSpec> existing = trainingSpec_.population;
    trainingSpec_.population.clear();
    PopulationSpec primary;
    for (const auto& entry : existing) {
        if (entry.brainKind == brainA_) {
            primary = entry;
            break;
        }
    }
    primary.brainKind = brainA_;
    const bool primaryGenome = requiresGenome(brainOptions_, brainA_);
    applyCounts(primary, countA_, primaryGenome);
    trainingSpec_.population.push_back(primary);

    if (!brainB_.empty() && countB_ > 0) {
        PopulationSpec secondary;
        for (const auto& entry : existing) {
            if (entry.brainKind == brainB_) {
                secondary = entry;
                break;
            }
        }
        secondary.brainKind = brainB_;
        const bool secondaryGenome = requiresGenome(brainOptions_, brainB_);
        applyCounts(secondary, countB_, secondaryGenome);
        trainingSpec_.population.push_back(secondary);
    }

    evolutionConfig_.populationSize = countA_ + countB_;
}

void TrainingPopulationPanel::onScenarioButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;
    self->viewController_->showView("scenario_select");
}

void TrainingPopulationPanel::onOrganismButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;
    self->viewController_->showView("organism_select");
}

void TrainingPopulationPanel::onBrainAButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;
    self->viewController_->showView("brain_a_select");
}

void TrainingPopulationPanel::onBrainBButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;
    self->viewController_->showView("brain_b_select");
}

void TrainingPopulationPanel::onSelectionBackClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) return;
    self->viewController_->showView("main");
}

void TrainingPopulationPanel::onScenarioSelected(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;

    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto it = self->scenarioButtonToValue_.find(button);
    if (it == self->scenarioButtonToValue_.end()) {
        return;
    }
    self->selectedScenario_ = it->second;
    self->applySpec();
    self->viewController_->showView("main");
}

void TrainingPopulationPanel::onOrganismSelected(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;

    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto it = self->organismButtonToValue_.find(button);
    if (it == self->organismButtonToValue_.end()) {
        return;
    }
    self->selectedOrganism_ = it->second;
    self->setBrainOptionsForOrganism(self->selectedOrganism_);

    int total = self->countA_ + self->countB_;
    if (total <= 0) {
        total = self->evolutionConfig_.populationSize;
    }
    self->brainA_ = self->brainOptions_.front().kind;
    self->brainB_.clear();
    self->countA_ = total;
    self->countB_ = 0;
    self->applySpec();
    self->viewController_->showView("main");
}

void TrainingPopulationPanel::onBrainASelected(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;

    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto it = self->brainAButtonToValue_.find(button);
    if (it == self->brainAButtonToValue_.end()) {
        return;
    }
    self->brainA_ = it->second;
    self->applySpec();
    self->viewController_->showView("main");
}

void TrainingPopulationPanel::onBrainBSelected(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;

    lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto it = self->brainBButtonToValue_.find(button);
    if (it == self->brainBButtonToValue_.end()) {
        return;
    }
    if (it->second.empty()) {
        self->brainB_.clear();
        self->countB_ = 0;
    }
    else {
        self->brainB_ = it->second;
        if (self->countB_ == 0) {
            self->countB_ = kPopulationStep;
        }
    }
    self->applySpec();
    self->viewController_->showView("main");
}

void TrainingPopulationPanel::onCountAChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_ || !self->countAStepper_) return;

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->countAStepper_);
    self->countA_ = value;
    self->applySpec();
}

void TrainingPopulationPanel::onCountBChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_ || !self->countBStepper_) return;

    if (self->brainB_.empty()) {
        LVGLBuilder::ActionStepperBuilder::setValue(self->countBStepper_, 0);
        return;
    }

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->countBStepper_);
    self->countB_ = value;
    self->applySpec();
}

} // namespace Ui
} // namespace DirtSim
