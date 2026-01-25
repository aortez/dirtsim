#pragma once

#include "BrowserPanel.h"
#include "core/ScenarioId.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "core/organisms/evolution/GenomeSort.h"
#include "ui/state-machine/EventSink.h"
#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class GenomeBrowserPanel {
public:
    GenomeBrowserPanel(
        lv_obj_t* parent, Network::WebSocketServiceInterface* wsService, EventSink* eventSink);

    void refresh();
    Result<GenomeId, std::string> openDetailByIndex(size_t index);
    Result<GenomeId, std::string> openDetailById(const GenomeId& id);
    Result<std::monostate, std::string> loadDetailForId(const GenomeId& id);

private:
    struct ScenarioButtonContext {
        GenomeBrowserPanel* panel = nullptr;
        Scenario::EnumType scenarioId = Scenario::EnumType::Sandbox;
    };

    struct SortButtonContext {
        GenomeBrowserPanel* panel = nullptr;
        GenomeSortKey sortKey = GenomeSortKey::CreatedTimestamp;
    };

    struct SortRowWidgets {
        GenomeSortKey sortKey = GenomeSortKey::CreatedTimestamp;
        lv_obj_t* keyButton = nullptr;
        lv_obj_t* directionButton = nullptr;
    };

    Network::WebSocketServiceInterface* wsService_ = nullptr;
    EventSink* eventSink_ = nullptr;
    GenomeSortKey sortKey_ = GenomeSortKey::CreatedTimestamp;
    static constexpr size_t kSortKeyCount = static_cast<size_t>(GenomeSortKey::Generation) + 1;
    std::array<GenomeSortDirection, kSortKeyCount> sortDirections_{
        GenomeSortDirection::Desc,
        GenomeSortDirection::Desc,
        GenomeSortDirection::Desc,
    };
    std::vector<SortRowWidgets> sortRows_;
    BrowserPanel browser_;
    std::unordered_map<GenomeId, GenomeMetadata> metadataById_;
    std::unordered_map<lv_obj_t*, Scenario::EnumType> scenarioButtons_;
    std::optional<GenomeId> scenarioPanelGenomeId_;
    std::optional<Scenario::EnumType> selectedScenarioId_;
    lv_obj_t* scenarioNameLabel_ = nullptr;
    lv_obj_t* scenarioDescriptionLabel_ = nullptr;

    Result<std::vector<BrowserPanel::Item>, std::string> fetchList();
    Result<BrowserPanel::DetailText, std::string> fetchDetail(const BrowserPanel::Item& item);
    Result<bool, std::string> deleteItem(const BrowserPanel::Item& item);
    Result<std::monostate, std::string> loadItem(const BrowserPanel::Item& item);
    void buildSortControls(lv_obj_t* parent);
    void buildScenarioPanel(lv_obj_t* parent, const BrowserPanel::Item& item);
    void clearScenarioPanelState();
    size_t sortKeyIndex(GenomeSortKey key) const;
    void selectScenario(Scenario::EnumType scenarioId);
    void updateScenarioLabels();
    void updateSortButtons();
    static void onScenarioSelected(lv_event_t* e);
    static void onScenarioButtonDeleted(lv_event_t* e);
    static void onSortButtonDeleted(lv_event_t* e);
    static void onSortDirectionClicked(lv_event_t* e);
    static void onSortKeyClicked(lv_event_t* e);

    std::string formatListLabel(const GenomeId& id, const GenomeMetadata& meta) const;
    std::string formatDetailText(const GenomeId& id, const GenomeMetadata& meta) const;
};

} // namespace Ui
} // namespace DirtSim
