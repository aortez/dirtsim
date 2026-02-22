#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "lvgl/lvgl.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {

struct EvolutionConfig;
struct PopulationSpec;
struct TrainingSpec;
namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class EventSink;

class TrainingPopulationPanel {
public:
    struct BrainOption {
        std::string kind;
        bool requiresGenome = false;
    };
    using PopulationTotalChangedCallback = std::function<void(int)>;

    TrainingPopulationPanel(
        lv_obj_t* container,
        EventSink& eventSink,
        Network::WebSocketServiceInterface* wsService,
        bool evolutionStarted,
        EvolutionConfig& evolutionConfig,
        TrainingSpec& trainingSpec);
    ~TrainingPopulationPanel();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted();
    void setPopulationTotal(int total);
    void setPopulationTotalChangedCallback(const PopulationTotalChangedCallback& callback);
    void addSeedGenome(const GenomeId& id);

private:
    struct PopulationEntry {
        std::optional<GenomeId> genomeId;
        bool isRandom = false;
    };

    struct EntryContext {
        TrainingPopulationPanel* panel = nullptr;
        size_t index = 0;
    };

    lv_obj_t* container_ = nullptr;
    EventSink& eventSink_;
    Network::WebSocketServiceInterface* wsService_ = nullptr;

    bool evolutionStarted_ = false;
    bool ignoreEvents_ = false;

    EvolutionConfig& evolutionConfig_;
    TrainingSpec& trainingSpec_;

    lv_obj_t* addCountStepper_ = nullptr;
    lv_obj_t* addButton_ = nullptr;
    lv_obj_t* populationList_ = nullptr;
    lv_obj_t* clearAllButton_ = nullptr;
    lv_obj_t* clearAllConfirmCheckbox_ = nullptr;
    lv_obj_t* detailConfirmCheckbox_ = nullptr;
    lv_obj_t* detailRemoveButton_ = nullptr;
    lv_obj_t* detailOverlay_ = nullptr;
    lv_obj_t* organismButton_ = nullptr;
    lv_obj_t* organismList_ = nullptr;
    lv_obj_t* scenarioButton_ = nullptr;
    lv_obj_t* totalCountLabel_ = nullptr;
    lv_obj_t* scenarioColumn_ = nullptr;
    lv_obj_t* mainColumn_ = nullptr;
    lv_obj_t* listColumn_ = nullptr;

    std::vector<Scenario::EnumType> scenarioOptions_;
    std::vector<std::string> scenarioLabels_;

    std::vector<OrganismType> organismOptions_;
    std::vector<std::string> organismLabels_;

    std::vector<BrainOption> brainOptions_;

    std::unordered_map<lv_obj_t*, Scenario::EnumType> scenarioButtonToValue_;
    std::unordered_map<lv_obj_t*, OrganismType> organismButtonToValue_;

    Scenario::EnumType selectedScenario_ = Scenario::EnumType::TreeGermination;
    OrganismType selectedOrganism_ = OrganismType::TREE;
    std::string brainKind_;
    bool brainRequiresGenome_ = false;
    int populationTotal_ = 0;
    int addCount_ = 1;
    bool scenarioColumnVisible_ = false;
    bool organismListVisible_ = false;
    std::optional<size_t> detailEntryIndex_;
    std::vector<PopulationEntry> populationEntries_;
    std::vector<std::unique_ptr<EntryContext>> entryContexts_;

    PopulationTotalChangedCallback populationTotalChangedCallback_;

    void createLayout();
    void createMainColumn(lv_obj_t* parent);
    void createListColumn(lv_obj_t* parent);
    void createScenarioColumn(lv_obj_t* parent);
    void updateControlsEnabled();
    void updateSelectorLabels();
    void refreshFromSpec();
    void applySpecUpdates();
    BrainOption resolveBrainOptionForScenario(Scenario::EnumType scenarioId) const;
    void setBrainOptionsForOrganism(OrganismType organismType);
    void syncUiFromState();
    void updateCountsLabel();
    void rebuildPopulationList();
    void closeDetailModal();
    void openDetailModal(size_t index);
    void updateDetailRemoveState();
    void updateClearAllState();
    void setControlEnabled(lv_obj_t* control, bool enabled);
    void setScenarioColumnVisible(bool visible);
    void setOrganismListVisible(bool visible);
    PopulationSpec* findPopulationSpec();
    PopulationSpec& ensurePopulationSpec();
    void pruneEmptySpecs();
    void removeEntry(size_t index);
    int computeTotalPopulation() const;
    int computeSeedCount() const;
    int computeRandomCount() const;
    std::string formatEntryLabel(const PopulationEntry& entry, int index) const;
    std::string formatEntryDetailText(const PopulationEntry& entry) const;

    static void onScenarioButtonClicked(lv_event_t* e);
    static void onOrganismButtonClicked(lv_event_t* e);
    static void onScenarioBackClicked(lv_event_t* e);
    static void onScenarioSelected(lv_event_t* e);
    static void onOrganismSelected(lv_event_t* e);
    static void onAddCountChanged(lv_event_t* e);
    static void onAddClicked(lv_event_t* e);
    static void onEntryClicked(lv_event_t* e);
    static void onDetailOkClicked(lv_event_t* e);
    static void onDetailRemoveClicked(lv_event_t* e);
    static void onDetailConfirmToggled(lv_event_t* e);
    static void onClearAllClicked(lv_event_t* e);
    static void onClearAllConfirmToggled(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
