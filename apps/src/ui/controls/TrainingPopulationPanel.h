#pragma once

#include "core/ScenarioId.h"
#include "core/organisms/OrganismType.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {

struct EvolutionConfig;
struct TrainingSpec;

namespace Ui {

class EventSink;

class TrainingPopulationPanel {
public:
    struct BrainOption {
        std::string kind;
        bool requiresGenome = false;
    };

    TrainingPopulationPanel(
        lv_obj_t* container,
        EventSink& eventSink,
        bool evolutionStarted,
        EvolutionConfig& evolutionConfig,
        TrainingSpec& trainingSpec);
    ~TrainingPopulationPanel();

    void setEvolutionStarted(bool started);
    void setEvolutionCompleted();

private:
    lv_obj_t* container_ = nullptr;
    EventSink& eventSink_;

    std::unique_ptr<PanelViewController> viewController_;

    bool evolutionStarted_ = false;
    bool ignoreEvents_ = false;

    EvolutionConfig& evolutionConfig_;
    TrainingSpec& trainingSpec_;

    lv_obj_t* brainAButton_ = nullptr;
    lv_obj_t* brainBButton_ = nullptr;
    lv_obj_t* countAStepper_ = nullptr;
    lv_obj_t* countBStepper_ = nullptr;
    lv_obj_t* organismButton_ = nullptr;
    lv_obj_t* scenarioButton_ = nullptr;
    lv_obj_t* totalCountLabel_ = nullptr;

    std::vector<Scenario::EnumType> scenarioOptions_;
    std::vector<std::string> scenarioLabels_;

    std::vector<OrganismType> organismOptions_;
    std::vector<std::string> organismLabels_;

    std::vector<BrainOption> brainOptions_;

    std::unordered_map<lv_obj_t*, Scenario::EnumType> scenarioButtonToValue_;
    std::unordered_map<lv_obj_t*, OrganismType> organismButtonToValue_;
    std::unordered_map<lv_obj_t*, std::string> brainAButtonToValue_;
    std::unordered_map<lv_obj_t*, std::string> brainBButtonToValue_;

    Scenario::EnumType selectedScenario_ = Scenario::EnumType::TreeGermination;
    OrganismType selectedOrganism_ = OrganismType::TREE;
    std::string brainA_;
    std::string brainB_;
    int countA_ = 0;
    int countB_ = 0;

    void createMainView(lv_obj_t* view);
    void createScenarioSelectView(lv_obj_t* view);
    void createOrganismSelectView(lv_obj_t* view);
    void createBrainSelectView(
        lv_obj_t* view,
        const char* title,
        bool includeNone,
        std::unordered_map<lv_obj_t*, std::string>& buttonMap,
        lv_event_cb_t callback);
    void updateControlsEnabled();
    void updateSelectorLabels();
    void refreshFromSpec();
    void applySpec();
    void setBrainOptionsForOrganism(OrganismType organismType);
    void syncUiFromState();
    void updatePopulationSpec();

    static void onScenarioButtonClicked(lv_event_t* e);
    static void onOrganismButtonClicked(lv_event_t* e);
    static void onBrainAButtonClicked(lv_event_t* e);
    static void onBrainBButtonClicked(lv_event_t* e);
    static void onSelectionBackClicked(lv_event_t* e);
    static void onScenarioSelected(lv_event_t* e);
    static void onOrganismSelected(lv_event_t* e);
    static void onBrainASelected(lv_event_t* e);
    static void onBrainBSelected(lv_event_t* e);
    static void onCountAChanged(lv_event_t* e);
    static void onCountBChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
