#pragma once

#include "BrowserPanel.h"
#include "core/ScenarioId.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "ui/state-machine/EventSink.h"
#include <optional>
#include <unordered_map>

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

    Network::WebSocketServiceInterface* wsService_ = nullptr;
    EventSink* eventSink_ = nullptr;
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
    void buildScenarioPanel(lv_obj_t* parent, const BrowserPanel::Item& item);
    void clearScenarioPanelState();
    void selectScenario(Scenario::EnumType scenarioId);
    void updateScenarioLabels();
    static void onScenarioSelected(lv_event_t* e);
    static void onScenarioButtonDeleted(lv_event_t* e);

    std::string formatListLabel(const GenomeId& id, const GenomeMetadata& meta) const;
    std::string formatDetailText(const GenomeId& id, const GenomeMetadata& meta) const;
};

} // namespace Ui
} // namespace DirtSim
