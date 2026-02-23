#include "TrainingPopulationPanel.h"
#include "core/network/WebSocketServiceInterface.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/reflect.h"
#include "server/api/GenomeGet.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kAddCountMin = 1;
constexpr int kAddCountMax = 9999;
constexpr int kAddCountStep = 1;
constexpr int kColumnGap = 12;
constexpr int kListColumnWidthPercent = 55;
constexpr int kMainColumnWidthPercent = 45;
constexpr int kScenarioColumnWidthPercent = 55;
constexpr int kEntryRowHeight = 60;
constexpr int kListHeight = 240;

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
                { TrainingBrainKind::DuckNeuralNetRecurrent, true },
                { TrainingBrainKind::NeuralNet, true },
                { TrainingBrainKind::Random, false },
                { TrainingBrainKind::WallBouncing, false },
                { TrainingBrainKind::DuckBrain2, false },
            };
        case OrganismType::NES_FLAPPY_BIRD:
            return {
                { TrainingBrainKind::DuckNeuralNetRecurrent, true },
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
        case OrganismType::NES_FLAPPY_BIRD:
            return "Nes Flappy Bird";
        case OrganismType::GOOSE:
            return "Goose";
        default:
            return "Unknown";
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
    Network::WebSocketServiceInterface* wsService,
    bool evolutionStarted,
    EvolutionConfig& evolutionConfig,
    TrainingSpec& trainingSpec)
    : container_(container),
      eventSink_(eventSink),
      wsService_(wsService),
      evolutionStarted_(evolutionStarted),
      evolutionConfig_(evolutionConfig),
      trainingSpec_(trainingSpec)
{
    scenarioOptions_ = {
        Scenario::EnumType::Benchmark,
        Scenario::EnumType::Clock,
        Scenario::EnumType::DamBreak,
        Scenario::EnumType::Empty,
        Scenario::EnumType::GooseTest,
        Scenario::EnumType::Lights,
        Scenario::EnumType::NesFlappyParatroopa,
        Scenario::EnumType::Raining,
        Scenario::EnumType::Sandbox,
        Scenario::EnumType::TreeGermination,
        Scenario::EnumType::WaterEqualization,
    };
    scenarioLabels_.reserve(scenarioOptions_.size());
    for (const auto& scenarioId : scenarioOptions_) {
        scenarioLabels_.push_back(Scenario::toString(scenarioId));
    }

    organismOptions_ = {
        OrganismType::TREE,
        OrganismType::DUCK,
        OrganismType::NES_FLAPPY_BIRD,
        OrganismType::GOOSE,
    };
    organismLabels_ = { "Tree", "Duck", "Nes Flappy Bird", "Goose" };

    selectedScenario_ = trainingSpec_.scenarioId;
    selectedOrganism_ = trainingSpec_.organismType;
    if (selectedOrganism_ == OrganismType::NES_FLAPPY_BIRD) {
        selectedScenario_ = Scenario::EnumType::NesFlappyParatroopa;
    }
    setBrainOptionsForOrganism(selectedOrganism_);

    createLayout();
    refreshFromSpec();

    spdlog::info("TrainingPopulationPanel: Initialized (started={})", evolutionStarted_);
}

TrainingPopulationPanel::~TrainingPopulationPanel()
{
    closeDetailModal();
    spdlog::info("TrainingPopulationPanel: Destroyed");
}

void TrainingPopulationPanel::createLayout()
{
    lv_obj_t* columns = lv_obj_create(container_);
    lv_obj_set_size(columns, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(columns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(columns, 0, 0);
    lv_obj_set_style_pad_all(columns, 0, 0);
    lv_obj_set_style_pad_column(columns, kColumnGap, 0);
    lv_obj_set_style_pad_row(columns, 0, 0);
    lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(columns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(columns, LV_OBJ_FLAG_SCROLLABLE);

    mainColumn_ = lv_obj_create(columns);
    lv_obj_set_width(mainColumn_, LV_PCT(100));
    lv_obj_set_height(mainColumn_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(mainColumn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mainColumn_, 0, 0);
    lv_obj_set_style_pad_all(mainColumn_, 0, 0);
    lv_obj_set_style_pad_row(mainColumn_, 6, 0);
    lv_obj_set_flex_flow(mainColumn_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        mainColumn_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(mainColumn_, LV_OBJ_FLAG_SCROLLABLE);

    listColumn_ = lv_obj_create(columns);
    lv_obj_set_width(listColumn_, LV_PCT(kListColumnWidthPercent));
    lv_obj_set_height(listColumn_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(listColumn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listColumn_, 0, 0);
    lv_obj_set_style_pad_all(listColumn_, 0, 0);
    lv_obj_set_style_pad_row(listColumn_, 6, 0);
    lv_obj_set_flex_flow(listColumn_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        listColumn_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(listColumn_, LV_OBJ_FLAG_SCROLLABLE);

    scenarioColumn_ = lv_obj_create(columns);
    lv_obj_set_width(scenarioColumn_, LV_PCT(kScenarioColumnWidthPercent));
    lv_obj_set_height(scenarioColumn_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(scenarioColumn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scenarioColumn_, 0, 0);
    lv_obj_set_style_pad_all(scenarioColumn_, 0, 0);
    lv_obj_set_style_pad_row(scenarioColumn_, 6, 0);
    lv_obj_set_flex_flow(scenarioColumn_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        scenarioColumn_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(scenarioColumn_, LV_OBJ_FLAG_SCROLLABLE);

    createMainColumn(mainColumn_);
    createListColumn(listColumn_);
    createScenarioColumn(scenarioColumn_);

    setScenarioColumnVisible(false);
}

void TrainingPopulationPanel::createMainColumn(lv_obj_t* parent)
{
    lv_obj_t* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, "Population Setup");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xDA70D6), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 8, 0);

    lv_obj_t* scenarioRow = lv_obj_create(parent);
    lv_obj_set_size(scenarioRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(scenarioRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scenarioRow, 0, 0);
    lv_obj_set_style_pad_all(scenarioRow, 0, 0);
    lv_obj_set_style_pad_column(scenarioRow, 6, 0);
    lv_obj_set_flex_flow(scenarioRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        scenarioRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(scenarioRow, LV_OBJ_FLAG_SCROLLABLE);

    scenarioButton_ = LVGLBuilder::actionButton(scenarioRow)
                          .text("Scenario: --")
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(95))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onScenarioButtonClicked, this)
                          .buildOrLog();

    lv_obj_t* organismRow = lv_obj_create(parent);
    lv_obj_set_size(organismRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(organismRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(organismRow, 0, 0);
    lv_obj_set_style_pad_all(organismRow, 0, 0);
    lv_obj_set_style_pad_column(organismRow, 6, 0);
    lv_obj_set_flex_flow(organismRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        organismRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(organismRow, LV_OBJ_FLAG_SCROLLABLE);

    organismButton_ = LVGLBuilder::actionButton(organismRow)
                          .text("Organism Type: --")
                          .icon(LV_SYMBOL_RIGHT)
                          .width(LV_PCT(95))
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .layoutRow()
                          .alignLeft()
                          .callback(onOrganismButtonClicked, this)
                          .buildOrLog();

    organismList_ = lv_obj_create(parent);
    lv_obj_set_width(organismList_, LV_PCT(95));
    lv_obj_set_style_bg_opa(organismList_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(organismList_, 0, 0);
    lv_obj_set_style_pad_all(organismList_, 0, 0);
    lv_obj_set_style_pad_row(organismList_, 6, 0);
    lv_obj_set_flex_flow(organismList_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        organismList_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(organismList_, LV_OBJ_FLAG_SCROLLABLE);

    organismButtonToValue_.clear();
    for (size_t i = 0; i < organismOptions_.size(); ++i) {
        const std::string& label = organismLabels_[i];
        lv_obj_t* container = LVGLBuilder::actionButton(organismList_)
                                  .text(label.c_str())
                                  .width(LV_PCT(100))
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

    setOrganismListVisible(false);

    addCountStepper_ = LVGLBuilder::actionStepper(parent)
                           .label("Add Count")
                           .range(kAddCountMin, kAddCountMax)
                           .step(kAddCountStep)
                           .value(addCount_)
                           .valueFormat("%.0f")
                           .valueScale(1.0)
                           .width(LV_PCT(95))
                           .callback(onAddCountChanged, this)
                           .buildOrLog();

    addButton_ = LVGLBuilder::actionButton(parent)
                     .text("Add")
                     .width(LV_PCT(95))
                     .height(LVGLBuilder::Style::ACTION_SIZE)
                     .backgroundColor(0x00AA66)
                     .layoutRow()
                     .alignLeft()
                     .callback(onAddClicked, this)
                     .buildOrLog();
}

void TrainingPopulationPanel::createListColumn(lv_obj_t* parent)
{
    totalCountLabel_ = lv_label_create(parent);
    lv_label_set_text(totalCountLabel_, "Total: --");
    lv_label_set_long_mode(totalCountLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(totalCountLabel_, LV_PCT(95));
    lv_obj_set_style_text_align(totalCountLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(totalCountLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(totalCountLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_top(totalCountLabel_, 2, 0);
    lv_obj_set_style_pad_bottom(totalCountLabel_, 4, 0);

    lv_obj_t* listLabel = lv_label_create(parent);
    lv_label_set_text(listLabel, "Population List");
    lv_obj_set_style_text_color(listLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(listLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(listLabel, 6, 0);
    lv_obj_set_style_pad_bottom(listLabel, 4, 0);

    populationList_ = lv_obj_create(parent);
    lv_obj_set_width(populationList_, LV_PCT(95));
    lv_obj_set_height(populationList_, kListHeight);
    lv_obj_set_style_bg_opa(populationList_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(populationList_, 0, 0);
    lv_obj_set_style_pad_all(populationList_, 0, 0);
    lv_obj_set_style_pad_row(populationList_, 6, 0);
    lv_obj_set_flex_flow(populationList_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        populationList_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(populationList_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(populationList_, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* clearRow = lv_obj_create(parent);
    lv_obj_set_size(clearRow, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(clearRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clearRow, 0, 0);
    lv_obj_set_style_pad_all(clearRow, 0, 0);
    lv_obj_set_style_pad_column(clearRow, 6, 0);
    lv_obj_set_flex_flow(clearRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        clearRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(clearRow, LV_OBJ_FLAG_SCROLLABLE);

    clearAllButton_ = LVGLBuilder::actionButton(clearRow)
                          .text("Clear All")
                          .mode(LVGLBuilder::ActionMode::Push)
                          .height(LVGLBuilder::Style::ACTION_SIZE)
                          .width(120)
                          .layoutRow()
                          .alignLeft()
                          .backgroundColor(0xCC0000)
                          .callback(onClearAllClicked, this)
                          .buildOrLog();

    clearAllConfirmCheckbox_ = lv_checkbox_create(clearRow);
    lv_checkbox_set_text(clearAllConfirmCheckbox_, "Confirm");
    lv_obj_set_style_text_font(clearAllConfirmCheckbox_, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(
        clearAllConfirmCheckbox_, onClearAllConfirmToggled, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_clear_flag(clearAllConfirmCheckbox_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clearAllConfirmCheckbox_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clearAllConfirmCheckbox_, 0, 0);
    lv_obj_set_style_pad_all(clearAllConfirmCheckbox_, 0, 0);
    lv_obj_set_style_pad_column(clearAllConfirmCheckbox_, 8, 0);
}

void TrainingPopulationPanel::createScenarioColumn(lv_obj_t* parent)
{
    LVGLBuilder::actionButton(parent)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onScenarioBackClicked, this)
        .buildOrLog();

    lv_obj_t* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, "Scenario");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    scenarioButtonToValue_.clear();
    for (size_t i = 0; i < scenarioOptions_.size(); ++i) {
        const std::string& label = scenarioLabels_[i];
        lv_obj_t* container = LVGLBuilder::actionButton(parent)
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

void TrainingPopulationPanel::setScenarioColumnVisible(bool visible)
{
    scenarioColumnVisible_ = visible;
    if (!scenarioColumn_) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(scenarioColumn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scenarioColumn_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        if (listColumn_) {
            lv_obj_add_flag(listColumn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(listColumn_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        }
        if (mainColumn_) {
            lv_obj_set_width(mainColumn_, LV_PCT(kMainColumnWidthPercent));
        }
        lv_obj_set_width(scenarioColumn_, LV_PCT(kScenarioColumnWidthPercent));
    }
    else {
        lv_obj_add_flag(scenarioColumn_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scenarioColumn_, LV_OBJ_FLAG_IGNORE_LAYOUT);
        if (listColumn_) {
            lv_obj_clear_flag(listColumn_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(listColumn_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_width(listColumn_, LV_PCT(kListColumnWidthPercent));
        }
        if (mainColumn_) {
            lv_obj_set_width(mainColumn_, LV_PCT(kMainColumnWidthPercent));
        }
    }
}

void TrainingPopulationPanel::setOrganismListVisible(bool visible)
{
    organismListVisible_ = visible;
    if (!organismList_) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(organismList_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(organismList_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    else {
        lv_obj_add_flag(organismList_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(organismList_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
}

void TrainingPopulationPanel::updateControlsEnabled()
{
    const bool enabled = !evolutionStarted_;
    const bool scenarioEnabled = enabled && selectedOrganism_ != OrganismType::NES_FLAPPY_BIRD;
    setControlEnabled(scenarioButton_, scenarioEnabled);
    setControlEnabled(organismButton_, enabled);
    setControlEnabled(addCountStepper_, enabled);
    setControlEnabled(addButton_, enabled);
    setControlEnabled(clearAllButton_, enabled);
    setControlEnabled(clearAllConfirmCheckbox_, enabled);
    if (!enabled || !scenarioEnabled) {
        setOrganismListVisible(false);
        setScenarioColumnVisible(false);
    }
    updateClearAllState();
}

void TrainingPopulationPanel::setControlEnabled(lv_obj_t* control, bool enabled)
{
    if (!control) {
        return;
    }
    if (enabled) {
        lv_obj_clear_state(control, LV_STATE_DISABLED);
        lv_obj_set_style_opa(control, LV_OPA_COVER, 0);
    }
    else {
        lv_obj_add_state(control, LV_STATE_DISABLED);
        lv_obj_set_style_opa(control, LV_OPA_50, 0);
    }
}

void TrainingPopulationPanel::updateSelectorLabels()
{
    setActionButtonText(
        scenarioButton_, std::string("Scenario: ") + Scenario::toString(selectedScenario_));
    setActionButtonText(
        organismButton_, std::string("Organism Type: ") + organismLabel(selectedOrganism_));
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

void TrainingPopulationPanel::setPopulationTotal(int total)
{
    if (total < 0) {
        return;
    }
    if (total == 0) {
        trainingSpec_.population.clear();
        applySpecUpdates();
        syncUiFromState();
        return;
    }

    int desiredTotal = total;
    const int minTotal = computeSeedCount();
    if (desiredTotal < minTotal) {
        desiredTotal = minTotal;
    }

    int currentTotal = computeTotalPopulation();
    if (desiredTotal == currentTotal) {
        return;
    }

    if (desiredTotal > currentTotal) {
        const int addCount = desiredTotal - currentTotal;
        PopulationSpec& spec = ensurePopulationSpec();
        if (brainRequiresGenome_) {
            spec.randomCount += addCount;
            spec.count = static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
        }
        else {
            spec.count += addCount;
        }
    }
    else {
        int remaining = currentTotal - desiredTotal;
        for (auto it = trainingSpec_.population.rbegin();
             it != trainingSpec_.population.rend() && remaining > 0;
             ++it) {
            if (brainRequiresGenome_) {
                const int removeCount = std::min(remaining, it->randomCount);
                it->randomCount -= removeCount;
                it->count = static_cast<int>(it->seedGenomes.size()) + it->randomCount;
                remaining -= removeCount;
            }
            else {
                const int removeCount = std::min(remaining, it->count);
                it->count -= removeCount;
                remaining -= removeCount;
            }
        }
    }

    pruneEmptySpecs();
    applySpecUpdates();
    syncUiFromState();
}

void TrainingPopulationPanel::setPopulationTotalChangedCallback(
    const PopulationTotalChangedCallback& callback)
{
    populationTotalChangedCallback_ = callback;
    if (populationTotalChangedCallback_) {
        populationTotalChangedCallback_(populationTotal_);
    }
}

void TrainingPopulationPanel::setSpecUpdatedCallback(const SpecUpdatedCallback& callback)
{
    specUpdatedCallback_ = callback;
}

void TrainingPopulationPanel::addSeedGenome(const GenomeId& id)
{
    if (id.isNil()) {
        return;
    }
    if (!brainRequiresGenome_) {
        return;
    }

    PopulationSpec& spec = ensurePopulationSpec();
    if (std::find(spec.seedGenomes.begin(), spec.seedGenomes.end(), id) != spec.seedGenomes.end()) {
        return;
    }

    spec.seedGenomes.push_back(id);
    spec.count = static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
    applySpecUpdates();
    syncUiFromState();
}

void TrainingPopulationPanel::refreshFromSpec()
{
    selectedScenario_ = trainingSpec_.scenarioId;
    selectedOrganism_ = trainingSpec_.organismType;
    if (selectedOrganism_ == OrganismType::NES_FLAPPY_BIRD) {
        selectedScenario_ = Scenario::EnumType::NesFlappyParatroopa;
    }
    setBrainOptionsForOrganism(selectedOrganism_);

    if (trainingSpec_.population.empty() && evolutionConfig_.populationSize > 0) {
        PopulationSpec& spec = ensurePopulationSpec();
        if (brainRequiresGenome_) {
            spec.randomCount = evolutionConfig_.populationSize;
            spec.count = spec.randomCount;
        }
        else {
            spec.count = evolutionConfig_.populationSize;
        }
    }

    for (auto& spec : trainingSpec_.population) {
        const BrainOption resolvedBrain = resolveBrainOptionForScenario(trainingSpec_.scenarioId);
        const bool specRequiresGenome = resolvedBrain.requiresGenome;

        if (specRequiresGenome && spec.seedGenomes.empty() && spec.randomCount == 0
            && spec.count > 0) {
            spec.randomCount = spec.count;
        }
        else if (
            !specRequiresGenome && spec.count == 0
            && (!spec.seedGenomes.empty() || spec.randomCount > 0)) {
            spec.count = static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
        }

        spec.brainKind = resolvedBrain.kind;
        spec.brainVariant.reset();
        if (!specRequiresGenome) {
            spec.seedGenomes.clear();
            spec.randomCount = 0;
        }
        if (specRequiresGenome) {
            const int seedCount = static_cast<int>(spec.seedGenomes.size());
            if (spec.count < seedCount) {
                spec.count = seedCount;
            }
            if (spec.randomCount < 0) {
                spec.randomCount = 0;
            }
            spec.count = seedCount + spec.randomCount;
        }
    }

    pruneEmptySpecs();
    applySpecUpdates();
    syncUiFromState();
}

void TrainingPopulationPanel::applySpecUpdates()
{
    trainingSpec_.organismType = selectedOrganism_;
    if (selectedOrganism_ == OrganismType::NES_FLAPPY_BIRD) {
        selectedScenario_ = Scenario::EnumType::NesFlappyParatroopa;
    }
    trainingSpec_.scenarioId = selectedScenario_;
    for (auto& spec : trainingSpec_.population) {
        const BrainOption resolvedBrain = resolveBrainOptionForScenario(trainingSpec_.scenarioId);
        const bool specRequiresGenome = resolvedBrain.requiresGenome;

        spec.brainKind = resolvedBrain.kind;
        spec.brainVariant.reset();
        if (specRequiresGenome) {
            if (spec.randomCount < 0) {
                spec.randomCount = 0;
            }
            spec.count = static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
        }
        else {
            spec.seedGenomes.clear();
            spec.randomCount = 0;
            if (spec.count < 0) {
                spec.count = 0;
            }
        }
    }

    const BrainOption selectedBrain = resolveBrainOptionForScenario(trainingSpec_.scenarioId);
    brainKind_ = selectedBrain.kind;
    brainRequiresGenome_ = selectedBrain.requiresGenome;

    populationTotal_ = computeTotalPopulation();
    evolutionConfig_.populationSize = populationTotal_;
    if (populationTotalChangedCallback_) {
        populationTotalChangedCallback_(populationTotal_);
    }
    if (specUpdatedCallback_) {
        specUpdatedCallback_();
    }
}

TrainingPopulationPanel::BrainOption TrainingPopulationPanel::resolveBrainOptionForScenario(
    Scenario::EnumType /*scenarioId*/) const
{
    if (brainOptions_.empty()) {
        return {
            .kind = TrainingBrainKind::Random,
            .requiresGenome = false,
        };
    }
    return brainOptions_.front();
}

void TrainingPopulationPanel::setBrainOptionsForOrganism(OrganismType organismType)
{
    brainOptions_ = getBrainOptions(organismType);
    if (brainOptions_.empty()) {
        brainKind_ = TrainingBrainKind::Random;
        brainRequiresGenome_ = false;
        return;
    }

    const BrainOption resolvedBrain = resolveBrainOptionForScenario(selectedScenario_);
    brainKind_ = resolvedBrain.kind;
    brainRequiresGenome_ = resolvedBrain.requiresGenome;
}

void TrainingPopulationPanel::syncUiFromState()
{
    ignoreEvents_ = true;

    updateSelectorLabels();
    updateCountsLabel();

    if (addCountStepper_) {
        LVGLBuilder::ActionStepperBuilder::setValue(addCountStepper_, addCount_);
    }

    rebuildPopulationList();
    updateClearAllState();
    updateControlsEnabled();
    ignoreEvents_ = false;
}

void TrainingPopulationPanel::updateCountsLabel()
{
    if (!totalCountLabel_) {
        return;
    }

    const int seedCount = computeSeedCount();
    const int randomCount = computeRandomCount();
    const int totalCount = computeTotalPopulation();

    std::string text = "Total: " + std::to_string(totalCount);
    if (brainRequiresGenome_) {
        text += "  Seeds: " + std::to_string(seedCount);
        text += "  Random: " + std::to_string(randomCount);
    }

    lv_label_set_text(totalCountLabel_, text.c_str());
}

PopulationSpec* TrainingPopulationPanel::findPopulationSpec()
{
    if (trainingSpec_.population.empty()) {
        return nullptr;
    }
    return &trainingSpec_.population.front();
}

PopulationSpec& TrainingPopulationPanel::ensurePopulationSpec()
{
    if (!trainingSpec_.population.empty()) {
        return trainingSpec_.population.front();
    }
    PopulationSpec spec;
    spec.brainKind = brainKind_;
    spec.brainVariant.reset();
    spec.count = 0;
    spec.randomCount = 0;
    trainingSpec_.population.push_back(spec);
    return trainingSpec_.population.back();
}

void TrainingPopulationPanel::pruneEmptySpecs()
{
    auto isEmpty = [&](const PopulationSpec& spec) {
        if (brainRequiresGenome_) {
            return spec.seedGenomes.empty() && spec.randomCount == 0;
        }
        return spec.count <= 0;
    };

    trainingSpec_.population.erase(
        std::remove_if(trainingSpec_.population.begin(), trainingSpec_.population.end(), isEmpty),
        trainingSpec_.population.end());
}

void TrainingPopulationPanel::removeEntry(size_t index)
{
    if (index >= populationEntries_.size()) {
        return;
    }

    const PopulationEntry& entry = populationEntries_[index];
    PopulationSpec* spec = findPopulationSpec();
    if (!spec) {
        return;
    }

    if (brainRequiresGenome_) {
        if (entry.genomeId.has_value()) {
            auto it = std::find(
                spec->seedGenomes.begin(), spec->seedGenomes.end(), entry.genomeId.value());
            if (it != spec->seedGenomes.end()) {
                spec->seedGenomes.erase(it);
            }
        }
        else if (spec->randomCount > 0) {
            spec->randomCount -= 1;
        }
        spec->count = static_cast<int>(spec->seedGenomes.size()) + spec->randomCount;
    }
    else {
        if (spec->count > 0) {
            spec->count -= 1;
        }
    }

    pruneEmptySpecs();
    applySpecUpdates();
    syncUiFromState();
}

int TrainingPopulationPanel::computeTotalPopulation() const
{
    int total = 0;
    for (const auto& spec : trainingSpec_.population) {
        if (brainRequiresGenome_) {
            total += static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
        }
        else {
            total += spec.count;
        }
    }
    return total;
}

int TrainingPopulationPanel::computeSeedCount() const
{
    int total = 0;
    for (const auto& spec : trainingSpec_.population) {
        total += static_cast<int>(spec.seedGenomes.size());
    }
    return total;
}

int TrainingPopulationPanel::computeRandomCount() const
{
    if (!brainRequiresGenome_) {
        return 0;
    }
    int total = 0;
    for (const auto& spec : trainingSpec_.population) {
        total += spec.randomCount;
    }
    return total;
}

std::string TrainingPopulationPanel::formatEntryLabel(const PopulationEntry& entry, int index) const
{
    std::ostringstream oss;
    if (entry.genomeId.has_value()) {
        oss << "Genome " << entry.genomeId->toShortString();
    }
    else if (brainRequiresGenome_) {
        oss << "Random " << (index + 1);
    }
    else {
        oss << "Individual " << (index + 1);
    }
    oss << "\nScenario: " << Scenario::toString(trainingSpec_.scenarioId);
    return oss.str();
}

std::string TrainingPopulationPanel::formatEntryDetailText(const PopulationEntry& entry) const
{
    std::ostringstream oss;
    if (!entry.genomeId.has_value()) {
        oss << "Random Individual\n";
        oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
        if (brainRequiresGenome_) {
            oss << "Genome: generated at training start\n";
        }
        else {
            oss << "Genome: not required for this brain\n";
        }
        return oss.str();
    }

    const GenomeId genomeId = entry.genomeId.value();
    if (!wsService_) {
        oss << "Genome ID: " << genomeId.toString() << "\n";
        oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
        oss << "Metadata unavailable (no WebSocket service)";
        return oss.str();
    }
    if (!wsService_->isConnected()) {
        oss << "Genome ID: " << genomeId.toString() << "\n";
        oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
        oss << "Metadata unavailable (not connected)";
        return oss.str();
    }

    Api::GenomeGet::Command cmd{ .id = genomeId };
    auto response = wsService_->sendCommandAndGetResponse<Api::GenomeGet::Okay>(cmd, 5000);
    if (response.isError()) {
        oss << "Genome ID: " << genomeId.toString() << "\n";
        oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
        oss << "Metadata unavailable (" << response.errorValue() << ")";
        return oss.str();
    }
    if (response.value().isError()) {
        oss << "Genome ID: " << genomeId.toString() << "\n";
        oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
        oss << "Metadata unavailable (" << response.value().errorValue().message << ")";
        return oss.str();
    }

    const auto& ok = response.value().value();
    if (!ok.found) {
        oss << "Genome ID: " << genomeId.toString() << "\n";
        oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
        oss << "Metadata unavailable (genome not found)";
        return oss.str();
    }

    const GenomeMetadata& meta = ok.metadata;
    oss << "Genome ID: " << genomeId.toString() << "\n";
    if (!meta.name.empty()) {
        oss << "Name: " << meta.name << "\n";
    }
    oss << "Training Scenario: " << Scenario::toString(trainingSpec_.scenarioId) << "\n";
    oss << "Scenario: " << Scenario::toString(meta.scenarioId) << "\n";
    oss << "Fitness: " << std::fixed << std::setprecision(3) << meta.fitness << "\n";
    oss << "Generation: " << meta.generation << "\n";
    oss << "Created: " << meta.createdTimestamp << "\n";
    if (!meta.notes.empty()) {
        oss << "Notes: " << meta.notes << "\n";
    }
    if (meta.organismType.has_value()) {
        oss << "Organism Type: " << reflect::enum_name(meta.organismType.value()) << "\n";
    }
    if (meta.brainKind.has_value()) {
        oss << "Brain Kind: " << meta.brainKind.value() << "\n";
    }
    if (meta.brainVariant.has_value()) {
        oss << "Brain Variant: " << meta.brainVariant.value() << "\n";
    }
    if (meta.trainingSessionId.has_value()) {
        oss << "Training Session: " << meta.trainingSessionId->toShortString() << "\n";
    }
    return oss.str();
}

void TrainingPopulationPanel::rebuildPopulationList()
{
    if (!populationList_) {
        return;
    }

    entryContexts_.clear();
    populationEntries_.clear();
    lv_obj_clean(populationList_);

    for (const auto& spec : trainingSpec_.population) {
        if (brainRequiresGenome_) {
            for (const auto& id : spec.seedGenomes) {
                populationEntries_.push_back(
                    PopulationEntry{
                        .genomeId = id,
                        .isRandom = false,
                    });
            }
            for (int i = 0; i < spec.randomCount; ++i) {
                populationEntries_.push_back(
                    PopulationEntry{
                        .genomeId = std::nullopt,
                        .isRandom = true,
                    });
            }
        }
        else {
            for (int i = 0; i < spec.count; ++i) {
                populationEntries_.push_back(
                    PopulationEntry{
                        .genomeId = std::nullopt,
                        .isRandom = true,
                    });
            }
        }
    }

    if (populationEntries_.empty()) {
        lv_obj_t* emptyLabel = lv_label_create(populationList_);
        lv_label_set_text(emptyLabel, "No individuals yet");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(0x999999), 0);
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_12, 0);
        return;
    }

    entryContexts_.reserve(populationEntries_.size());
    for (size_t i = 0; i < populationEntries_.size(); ++i) {
        auto context = std::make_unique<EntryContext>();
        context->panel = this;
        context->index = i;
        const std::string label = formatEntryLabel(populationEntries_[i], static_cast<int>(i));
        LVGLBuilder::actionButton(populationList_)
            .text(label.c_str())
            .height(kEntryRowHeight)
            .width(LV_PCT(100))
            .layoutColumn()
            .alignLeft()
            .callback(onEntryClicked, context.get())
            .buildOrLog();
        entryContexts_.push_back(std::move(context));
    }
}

void TrainingPopulationPanel::openDetailModal(size_t index)
{
    if (index >= populationEntries_.size()) {
        return;
    }

    closeDetailModal();
    detailEntryIndex_ = index;

    detailOverlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(detailOverlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(detailOverlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(detailOverlay_, LV_OPA_60, 0);
    lv_obj_clear_flag(detailOverlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(detailOverlay_);

    lv_obj_t* modal = lv_obj_create(detailOverlay_);
    lv_obj_set_size(modal, 420, 440);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_80, 0);
    lv_obj_set_style_radius(modal, 12, 0);
    lv_obj_set_style_pad_all(modal, 12, 0);
    lv_obj_set_style_pad_row(modal, 8, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* titleLabel = lv_label_create(modal);
    lv_label_set_text(titleLabel, "Population Entry");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);

    lv_obj_t* detailContainer = lv_obj_create(modal);
    lv_obj_set_width(detailContainer, LV_PCT(100));
    lv_obj_set_height(detailContainer, LV_PCT(100));
    lv_obj_set_flex_grow(detailContainer, 1);
    lv_obj_set_style_bg_opa(detailContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detailContainer, 0, 0);
    lv_obj_set_style_pad_all(detailContainer, 0, 0);
    lv_obj_set_flex_flow(detailContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(detailContainer, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(detailContainer, LV_SCROLLBAR_MODE_AUTO);

    const std::string detailText = formatEntryDetailText(populationEntries_[index]);
    lv_obj_t* detailLabel = lv_label_create(detailContainer);
    lv_label_set_text(detailLabel, detailText.c_str());
    lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detailLabel, LV_PCT(100));
    lv_obj_set_style_text_color(detailLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_12, 0);

    lv_obj_t* bottomRow = lv_obj_create(modal);
    lv_obj_set_size(bottomRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow, 0, 0);
    lv_obj_set_style_pad_all(bottomRow, 0, 0);
    lv_obj_set_style_pad_column(bottomRow, 12, 0);
    lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        bottomRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bottomRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* leftButtons = lv_obj_create(bottomRow);
    lv_obj_set_width(leftButtons, LV_SIZE_CONTENT);
    lv_obj_set_height(leftButtons, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(leftButtons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leftButtons, 0, 0);
    lv_obj_set_style_pad_all(leftButtons, 0, 0);
    lv_obj_set_style_pad_row(leftButtons, 8, 0);
    lv_obj_set_flex_flow(leftButtons, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        leftButtons, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(leftButtons, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* rightButtons = lv_obj_create(bottomRow);
    lv_obj_set_width(rightButtons, LV_SIZE_CONTENT);
    lv_obj_set_height(rightButtons, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rightButtons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightButtons, 0, 0);
    lv_obj_set_style_pad_all(rightButtons, 0, 0);
    lv_obj_set_style_pad_row(rightButtons, 8, 0);
    lv_obj_set_flex_flow(rightButtons, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        rightButtons, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(rightButtons, LV_OBJ_FLAG_SCROLLABLE);

    LVGLBuilder::actionButton(leftButtons)
        .text("OK")
        .mode(LVGLBuilder::ActionMode::Push)
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .width(120)
        .layoutRow()
        .alignLeft()
        .backgroundColor(0x00AA66)
        .callback(onDetailOkClicked, this)
        .buildOrLog();

    lv_obj_t* deleteRow = lv_obj_create(rightButtons);
    lv_obj_set_size(deleteRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(deleteRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(deleteRow, 0, 0);
    lv_obj_set_style_pad_all(deleteRow, 0, 0);
    lv_obj_set_style_pad_column(deleteRow, 6, 0);
    lv_obj_set_flex_flow(deleteRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        deleteRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(deleteRow, LV_OBJ_FLAG_SCROLLABLE);

    detailRemoveButton_ = LVGLBuilder::actionButton(deleteRow)
                              .text("Remove")
                              .mode(LVGLBuilder::ActionMode::Push)
                              .height(LVGLBuilder::Style::ACTION_SIZE)
                              .width(120)
                              .layoutRow()
                              .alignLeft()
                              .backgroundColor(0xCC0000)
                              .callback(onDetailRemoveClicked, this)
                              .buildOrLog();

    detailConfirmCheckbox_ = lv_checkbox_create(deleteRow);
    lv_checkbox_set_text(detailConfirmCheckbox_, "Confirm");
    lv_obj_set_style_text_font(detailConfirmCheckbox_, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(
        detailConfirmCheckbox_, onDetailConfirmToggled, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_clear_flag(detailConfirmCheckbox_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(detailConfirmCheckbox_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detailConfirmCheckbox_, 0, 0);
    lv_obj_set_style_pad_all(detailConfirmCheckbox_, 0, 0);
    lv_obj_set_style_pad_column(detailConfirmCheckbox_, 8, 0);

    updateDetailRemoveState();
}

void TrainingPopulationPanel::closeDetailModal()
{
    if (detailOverlay_) {
        lv_obj_del(detailOverlay_);
        detailOverlay_ = nullptr;
    }
    detailEntryIndex_.reset();
    detailConfirmCheckbox_ = nullptr;
    detailRemoveButton_ = nullptr;
}

void TrainingPopulationPanel::updateDetailRemoveState()
{
    const bool enabled =
        detailConfirmCheckbox_ && lv_obj_has_state(detailConfirmCheckbox_, LV_STATE_CHECKED);
    setControlEnabled(detailRemoveButton_, enabled);
}

void TrainingPopulationPanel::updateClearAllState()
{
    const bool hasPopulation = computeTotalPopulation() > 0;
    const bool confirmed =
        clearAllConfirmCheckbox_ && lv_obj_has_state(clearAllConfirmCheckbox_, LV_STATE_CHECKED);
    if (!hasPopulation && clearAllConfirmCheckbox_) {
        lv_obj_clear_state(clearAllConfirmCheckbox_, LV_STATE_CHECKED);
    }
    setControlEnabled(clearAllButton_, hasPopulation && confirmed && !evolutionStarted_);
}

void TrainingPopulationPanel::onScenarioButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;
    self->setOrganismListVisible(false);
    self->setScenarioColumnVisible(true);
}

void TrainingPopulationPanel::onOrganismButtonClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_) return;
    self->setScenarioColumnVisible(false);
    self->setOrganismListVisible(!self->organismListVisible_);
}

void TrainingPopulationPanel::onScenarioBackClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) return;
    self->setScenarioColumnVisible(false);
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
    if (self->selectedOrganism_ == OrganismType::NES_FLAPPY_BIRD) {
        self->selectedScenario_ = Scenario::EnumType::NesFlappyParatroopa;
    }
    self->applySpecUpdates();
    self->syncUiFromState();
    self->setOrganismListVisible(false);
    self->setScenarioColumnVisible(false);
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
    if (self->selectedOrganism_ == OrganismType::NES_FLAPPY_BIRD) {
        self->selectedScenario_ = Scenario::EnumType::NesFlappyParatroopa;
    }
    self->setBrainOptionsForOrganism(self->selectedOrganism_);
    self->trainingSpec_.population.clear();
    self->populationTotal_ = 0;
    self->applySpecUpdates();
    self->syncUiFromState();
    self->setOrganismListVisible(false);
}

void TrainingPopulationPanel::onAddCountChanged(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || !self->addCountStepper_ || self->ignoreEvents_) {
        return;
    }

    int32_t value = LVGLBuilder::ActionStepperBuilder::getValue(self->addCountStepper_);
    if (value < kAddCountMin) {
        value = kAddCountMin;
    }
    self->addCount_ = value;
}

void TrainingPopulationPanel::onAddClicked(lv_event_t* e)
{
    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self || self->ignoreEvents_ || self->evolutionStarted_) {
        return;
    }

    if (self->addCount_ <= 0) {
        return;
    }

    PopulationSpec& spec = self->ensurePopulationSpec();
    if (self->brainRequiresGenome_) {
        spec.randomCount += self->addCount_;
        spec.count = static_cast<int>(spec.seedGenomes.size()) + spec.randomCount;
    }
    else {
        spec.count += self->addCount_;
    }

    self->applySpecUpdates();
    self->syncUiFromState();
}

void TrainingPopulationPanel::onEntryClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* context = static_cast<EntryContext*>(lv_event_get_user_data(e));
    if (!context || !context->panel) {
        return;
    }

    context->panel->openDetailModal(context->index);
}

void TrainingPopulationPanel::onDetailOkClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->closeDetailModal();
}

void TrainingPopulationPanel::onDetailRemoveClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    if (!self->detailConfirmCheckbox_
        || !lv_obj_has_state(self->detailConfirmCheckbox_, LV_STATE_CHECKED)) {
        return;
    }

    if (self->detailEntryIndex_.has_value()) {
        self->removeEntry(self->detailEntryIndex_.value());
    }
    self->closeDetailModal();
}

void TrainingPopulationPanel::onDetailConfirmToggled(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->updateDetailRemoveState();
}

void TrainingPopulationPanel::onClearAllClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    if (!self->clearAllConfirmCheckbox_
        || !lv_obj_has_state(self->clearAllConfirmCheckbox_, LV_STATE_CHECKED)) {
        return;
    }

    if (self->clearAllConfirmCheckbox_) {
        lv_obj_clear_state(self->clearAllConfirmCheckbox_, LV_STATE_CHECKED);
    }
    self->trainingSpec_.population.clear();
    self->applySpecUpdates();
    self->syncUiFromState();
}

void TrainingPopulationPanel::onClearAllConfirmToggled(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* self = static_cast<TrainingPopulationPanel*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->updateClearAllState();
}

} // namespace Ui
} // namespace DirtSim
