#include "GenomeBrowserPanel.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioId.h"
#include "core/network/WebSocketServiceInterface.h"
#include "core/reflect.h"
#include "server/api/GenomeDelete.h"
#include "server/api/GenomeList.h"
#include "ui/ScenarioMetadataCache.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace DirtSim {
namespace Ui {

namespace {
constexpr int kSortArrowWidth = LVGLBuilder::Style::ACTION_SIZE;
constexpr int kSortRowGap = 8;
constexpr int kSortRowHeight = LVGLBuilder::Style::ACTION_SIZE;
constexpr const char* kSortArrowDown = "↓";
constexpr const char* kSortArrowUp = "↑";
} // namespace

GenomeBrowserPanel::GenomeBrowserPanel(
    lv_obj_t* parent, Network::WebSocketServiceInterface* wsService, EventSink* eventSink)
    : wsService_(wsService),
      eventSink_(eventSink),
      browser_(
          parent,
          "Genome Browser",
          [this]() { return fetchList(); },
          [this](const BrowserPanel::Item& item) { return fetchDetail(item); },
          [this](const BrowserPanel::Item& item) { return deleteItem(item); },
          std::vector<BrowserPanel::DetailAction>{
              BrowserPanel::DetailAction{
                  .label = "Load",
                  .handler = [this](const BrowserPanel::Item& item) { return loadItem(item); },
                  .color = 0x2A7FDB,
                  .column = BrowserPanel::DetailActionColumn::Left,
              },
              BrowserPanel::DetailAction{
                  .label = "Add to Training",
                  .handler =
                      [this](const BrowserPanel::Item& item) { return addItemToTraining(item); },
                  .color = 0x00AA66,
                  .column = BrowserPanel::DetailActionColumn::Right,
                  .shareRowWithSidePanel = true,
              },
          },
          BrowserPanel::DetailSidePanel{
              .label = "Scenario",
              .builder =
                  [this](lv_obj_t* parent, const BrowserPanel::Item& item) {
                      buildScenarioPanel(parent, item);
                  },
              .color = 0x4B6EAF,
          },
          BrowserPanel::ListActionPanel{
              .label = "Sort Order",
              .builder = [this](lv_obj_t* parent) { buildSortControls(parent); },
          },
          BrowserPanel::ModalStyle(420, 440, 90, 0, LV_OPA_60, LV_OPA_80))
{
    refresh();
}

void GenomeBrowserPanel::refresh()
{
    browser_.refreshList();
}

Result<GenomeId, std::string> GenomeBrowserPanel::openDetailByIndex(size_t index)
{
    return browser_.openDetailByIndex(index);
}

Result<GenomeId, std::string> GenomeBrowserPanel::openDetailById(const GenomeId& id)
{
    return browser_.openDetailById(id);
}

Result<std::monostate, std::string> GenomeBrowserPanel::loadDetailForId(const GenomeId& id)
{
    return browser_.triggerDetailActionForModalId(id);
}

Result<std::vector<BrowserPanel::Item>, std::string> GenomeBrowserPanel::fetchList()
{
    if (!wsService_) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            "No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error("Server not connected");
    }

    Api::GenomeList::Command cmd{
        .sortKey = sortKey_,
        .sortDirection = sortDirections_[sortKeyIndex(sortKey_)],
    };
    auto response = wsService_->sendCommandAndGetResponse<Api::GenomeList::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<std::vector<BrowserPanel::Item>, std::string>::error(
            response.value().errorValue().message);
    }

    metadataById_.clear();
    std::vector<BrowserPanel::Item> items;
    const auto& ok = response.value().value();
    items.reserve(ok.genomes.size());
    for (const auto& entry : ok.genomes) {
        metadataById_[entry.id] = entry.metadata;
        BrowserPanel::Item item;
        item.id = entry.id;
        item.label = formatListLabel(entry.id, entry.metadata);
        items.push_back(std::move(item));
    }

    return Result<std::vector<BrowserPanel::Item>, std::string>::okay(std::move(items));
}

Result<BrowserPanel::DetailText, std::string> GenomeBrowserPanel::fetchDetail(
    const BrowserPanel::Item& item)
{
    auto it = metadataById_.find(item.id);
    if (it == metadataById_.end()) {
        return Result<BrowserPanel::DetailText, std::string>::error("Genome metadata not found");
    }

    return Result<BrowserPanel::DetailText, std::string>::okay(
        BrowserPanel::DetailText{ .text = formatDetailText(item.id, it->second) });
}

Result<bool, std::string> GenomeBrowserPanel::deleteItem(const BrowserPanel::Item& item)
{
    if (!wsService_) {
        return Result<bool, std::string>::error("No WebSocketService available");
    }
    if (!wsService_->isConnected()) {
        return Result<bool, std::string>::error("Server not connected");
    }

    Api::GenomeDelete::Command cmd{ .id = item.id };
    auto response = wsService_->sendCommandAndGetResponse<Api::GenomeDelete::Okay>(cmd, 5000);
    if (response.isError()) {
        return Result<bool, std::string>::error(response.errorValue());
    }
    if (response.value().isError()) {
        return Result<bool, std::string>::error(response.value().errorValue().message);
    }

    const bool success = response.value().value().success;
    if (!success) {
        LOG_WARN(Controls, "GenomeBrowser: Delete returned false for {}", item.id.toShortString());
    }
    return Result<bool, std::string>::okay(success);
}

Result<std::monostate, std::string> GenomeBrowserPanel::loadItem(const BrowserPanel::Item& item)
{
    if (!eventSink_) {
        return Result<std::monostate, std::string>::error("No EventSink available");
    }

    Scenario::EnumType scenarioId = Scenario::EnumType::Sandbox;
    auto metaIt = metadataById_.find(item.id);
    if (metaIt != metadataById_.end()) {
        scenarioId = metaIt->second.scenarioId;
    }
    if (selectedScenarioId_.has_value()) {
        scenarioId = selectedScenarioId_.value();
    }

    eventSink_->queueEvent(GenomeLoadClickedEvent{ item.id, scenarioId });
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> GenomeBrowserPanel::addItemToTraining(
    const BrowserPanel::Item& item)
{
    if (!eventSink_) {
        return Result<std::monostate, std::string>::error("No EventSink available");
    }

    Scenario::EnumType scenarioId = Scenario::EnumType::Sandbox;
    auto metaIt = metadataById_.find(item.id);
    if (metaIt != metadataById_.end()) {
        scenarioId = metaIt->second.scenarioId;
    }
    if (selectedScenarioId_.has_value()) {
        scenarioId = selectedScenarioId_.value();
    }

    eventSink_->queueEvent(GenomeAddToTrainingClickedEvent{ item.id, scenarioId });
    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void GenomeBrowserPanel::buildSortControls(lv_obj_t* parent)
{
    if (!parent) {
        return;
    }

    sortRows_.clear();

    lv_obj_update_layout(parent);
    int columnWidth = lv_obj_get_width(parent);
    if (columnWidth <= 0) {
        columnWidth = LVGLBuilder::Style::ACTION_SIZE * 4;
    }

    const int labelWidth = std::max(0, columnWidth - kSortArrowWidth - kSortRowGap);

    const std::array<std::pair<GenomeSortKey, const char*>, kSortKeyCount> sortOptions{
        std::make_pair(GenomeSortKey::Fitness, "Fitness"),
        std::make_pair(GenomeSortKey::Generation, "Generation"),
        std::make_pair(GenomeSortKey::CreatedTimestamp, "Created"),
    };

    sortRows_.reserve(sortOptions.size());
    for (const auto& [key, label] : sortOptions) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, LV_PCT(100), kSortRowHeight);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, kSortRowGap, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* keyContext = new SortButtonContext{ this, key };
        lv_obj_t* keyButton = LVGLBuilder::actionButton(row)
                                  .text(label)
                                  .mode(LVGLBuilder::ActionMode::Toggle)
                                  .height(kSortRowHeight)
                                  .width(labelWidth)
                                  .layoutRow()
                                  .alignLeft()
                                  .callback(onSortKeyClicked, keyContext)
                                  .buildOrLog();
        if (keyButton) {
            lv_obj_add_event_cb(keyButton, onSortButtonDeleted, LV_EVENT_DELETE, keyContext);
        }
        else {
            delete keyContext;
        }

        auto* dirContext = new SortButtonContext{ this, key };
        const auto arrow = sortDirections_[sortKeyIndex(key)] == GenomeSortDirection::Asc
            ? kSortArrowUp
            : kSortArrowDown;
        lv_obj_t* directionButton = LVGLBuilder::actionButton(row)
                                        .icon(arrow)
                                        .mode(LVGLBuilder::ActionMode::Toggle)
                                        .height(kSortRowHeight)
                                        .width(kSortArrowWidth)
                                        .layoutRow()
                                        .alignCenter()
                                        .callback(onSortDirectionClicked, dirContext)
                                        .buildOrLog();
        if (directionButton) {
            lv_obj_add_event_cb(directionButton, onSortButtonDeleted, LV_EVENT_DELETE, dirContext);
        }
        else {
            delete dirContext;
        }

        sortRows_.push_back(
            SortRowWidgets{
                .sortKey = key,
                .keyButton = keyButton,
                .directionButton = directionButton,
            });
    }

    updateSortButtons();
}

void GenomeBrowserPanel::buildScenarioPanel(lv_obj_t* parent, const BrowserPanel::Item& item)
{
    if (!parent) {
        return;
    }

    clearScenarioPanelState();
    scenarioPanelGenomeId_ = item.id;

    lv_obj_t* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, "Scenario");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_14, 0);

    auto metaIt = metadataById_.find(item.id);
    if (metaIt == metadataById_.end()) {
        lv_obj_t* missingLabel = lv_label_create(parent);
        lv_label_set_text(missingLabel, "Scenario metadata missing.");
        lv_obj_set_style_text_color(missingLabel, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(missingLabel, &lv_font_montserrat_12, 0);
        return;
    }

    if (!ScenarioMetadataCache::hasScenarios()) {
        lv_obj_t* missingLabel = lv_label_create(parent);
        lv_label_set_text(missingLabel, "Scenario list not loaded.");
        lv_obj_set_style_text_color(missingLabel, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(missingLabel, &lv_font_montserrat_12, 0);
        return;
    }

    scenarioNameLabel_ = lv_label_create(parent);
    lv_label_set_long_mode(scenarioNameLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(scenarioNameLabel_, LV_PCT(100));
    lv_obj_set_style_text_color(scenarioNameLabel_, lv_color_hex(0xFFDD66), 0);
    lv_obj_set_style_text_font(scenarioNameLabel_, &lv_font_montserrat_12, 0);

    scenarioDescriptionLabel_ = lv_label_create(parent);
    lv_label_set_long_mode(scenarioDescriptionLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(scenarioDescriptionLabel_, LV_PCT(100));
    lv_obj_set_style_text_color(scenarioDescriptionLabel_, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(scenarioDescriptionLabel_, &lv_font_montserrat_12, 0);

    std::vector<std::string> scenarios = ScenarioMetadataCache::buildOptionsList();
    scenarioButtons_.clear();
    scenarioButtons_.reserve(scenarios.size());
    for (size_t i = 0; i < scenarios.size(); ++i) {
        lv_obj_t* container = LVGLBuilder::actionButton(parent)
                                  .text(scenarios[i].c_str())
                                  .mode(LVGLBuilder::ActionMode::Toggle)
                                  .width(LV_PCT(100))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutRow()
                                  .alignLeft()
                                  .buildOrLog();
        if (!container) {
            continue;
        }

        lv_obj_t* button = lv_obj_get_child(container, 0);
        if (!button) {
            continue;
        }

        Scenario::EnumType scenarioId =
            ScenarioMetadataCache::scenarioIdFromIndex(static_cast<uint16_t>(i));
        ScenarioButtonContext* context = new ScenarioButtonContext{ this, scenarioId };
        lv_obj_add_event_cb(button, onScenarioSelected, LV_EVENT_CLICKED, context);
        lv_obj_add_event_cb(button, onScenarioButtonDeleted, LV_EVENT_DELETE, context);
        scenarioButtons_[container] = scenarioId;
    }

    Scenario::EnumType initialScenario = metaIt->second.scenarioId;
    selectScenario(initialScenario);
    updateScenarioLabels();
}

std::string GenomeBrowserPanel::formatListLabel(
    const GenomeId& id, const GenomeMetadata& meta) const
{
    std::ostringstream oss;
    const std::string name = meta.name.empty() ? id.toShortString() : meta.name;
    oss << name << "\n";
    oss << "Fitness: " << std::fixed << std::setprecision(2) << meta.fitness;
    oss << "  Gen: " << meta.generation;
    oss << "  " << Scenario::toString(meta.scenarioId);
    return oss.str();
}

std::string GenomeBrowserPanel::formatDetailText(
    const GenomeId& id, const GenomeMetadata& meta) const
{
    std::ostringstream oss;
    oss << "Genome ID: " << id.toString() << "\n";
    if (!meta.name.empty()) {
        oss << "Name: " << meta.name << "\n";
    }
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

void GenomeBrowserPanel::clearScenarioPanelState()
{
    scenarioButtons_.clear();
    scenarioDescriptionLabel_ = nullptr;
    scenarioNameLabel_ = nullptr;
    scenarioPanelGenomeId_.reset();
    selectedScenarioId_.reset();
}

void GenomeBrowserPanel::selectScenario(Scenario::EnumType scenarioId)
{
    selectedScenarioId_ = scenarioId;
    for (const auto& [container, id] : scenarioButtons_) {
        LVGLBuilder::ActionButtonBuilder::setChecked(container, id == scenarioId);
    }
}

size_t GenomeBrowserPanel::sortKeyIndex(GenomeSortKey key) const
{
    return static_cast<size_t>(key);
}

void GenomeBrowserPanel::updateScenarioLabels()
{
    if (!scenarioNameLabel_ || !scenarioDescriptionLabel_) {
        return;
    }

    std::string scenarioName = "Unknown";
    std::string scenarioDescription;
    if (selectedScenarioId_.has_value()) {
        const Scenario::EnumType scenarioId = selectedScenarioId_.value();
        if (auto info = ScenarioMetadataCache::getScenarioInfo(scenarioId); info.has_value()) {
            scenarioName = info->name;
            scenarioDescription = info->description;
        }
        else {
            scenarioName = Scenario::toString(scenarioId);
        }
    }

    lv_label_set_text(scenarioNameLabel_, scenarioName.c_str());
    if (scenarioDescription.empty()) {
        lv_label_set_text(scenarioDescriptionLabel_, "");
    }
    else {
        lv_label_set_text(scenarioDescriptionLabel_, scenarioDescription.c_str());
    }
}

void GenomeBrowserPanel::updateSortButtons()
{
    for (const auto& row : sortRows_) {
        const bool isSelected = row.sortKey == sortKey_;
        if (row.keyButton) {
            LVGLBuilder::ActionButtonBuilder::setChecked(row.keyButton, isSelected);
        }
        if (row.directionButton) {
            LVGLBuilder::ActionButtonBuilder::setChecked(row.directionButton, isSelected);
            const auto direction = sortDirections_[sortKeyIndex(row.sortKey)];
            const auto arrow =
                direction == GenomeSortDirection::Asc ? kSortArrowUp : kSortArrowDown;
            LVGLBuilder::ActionButtonBuilder::setIcon(row.directionButton, arrow);
        }
    }
}

void GenomeBrowserPanel::onScenarioSelected(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* context = static_cast<ScenarioButtonContext*>(lv_event_get_user_data(e));
    if (!context || !context->panel) {
        return;
    }

    context->panel->selectScenario(context->scenarioId);
    context->panel->updateScenarioLabels();
}

void GenomeBrowserPanel::onScenarioButtonDeleted(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }

    auto* context = static_cast<ScenarioButtonContext*>(lv_event_get_user_data(e));
    delete context;
}

void GenomeBrowserPanel::onSortButtonDeleted(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }

    auto* context = static_cast<SortButtonContext*>(lv_event_get_user_data(e));
    delete context;
}

void GenomeBrowserPanel::onSortDirectionClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* context = static_cast<SortButtonContext*>(lv_event_get_user_data(e));
    if (!context || !context->panel) {
        return;
    }

    const size_t index = context->panel->sortKeyIndex(context->sortKey);
    auto& direction = context->panel->sortDirections_[index];
    direction = direction == GenomeSortDirection::Asc ? GenomeSortDirection::Desc
                                                      : GenomeSortDirection::Asc;
    context->panel->sortKey_ = context->sortKey;
    context->panel->updateSortButtons();
    context->panel->refresh();
}

void GenomeBrowserPanel::onSortKeyClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* context = static_cast<SortButtonContext*>(lv_event_get_user_data(e));
    if (!context || !context->panel) {
        return;
    }

    context->panel->sortKey_ = context->sortKey;
    context->panel->updateSortButtons();
    context->panel->refresh();
}

} // namespace Ui
} // namespace DirtSim
