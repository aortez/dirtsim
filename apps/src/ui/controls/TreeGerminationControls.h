#pragma once

#include "ScenarioControlsBase.h"
#include "core/scenarios/TreeGerminationConfig.h"
#include "lvgl/lvgl.h"
#include <memory>
#include <unordered_map>

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

class PanelViewController;

/**
 * Tree Germination scenario controls with brain type selector.
 */
class TreeGerminationControls : public ScenarioControlsBase {
public:
    TreeGerminationControls(
        lv_obj_t* container,
        Network::WebSocketServiceInterface* wsService,
        const Config::TreeGermination& config);
    ~TreeGerminationControls() override;

    void updateFromConfig(const ScenarioConfig& config) override;

protected:
    void createWidgets() override;

private:
    std::unique_ptr<PanelViewController> viewController_;

    lv_obj_t* brainTypeButton_ = nullptr;
    std::unordered_map<lv_obj_t*, int> buttonToBrainTypeIndex_;
    int currentBrainTypeIndex_ = 0;
    Config::TreeGermination currentConfig_;

    void createMainView(lv_obj_t* view);
    void createBrainTypeSelectionView(lv_obj_t* view);

    static void onBrainTypeButtonClicked(lv_event_t* e);
    static void onBrainTypeSelected(lv_event_t* e);
    static void onBrainTypeBackClicked(lv_event_t* e);

    Config::TreeGermination getCurrentConfig() const;
    static const char* getBrainTypeName(Config::TreeBrainType type);
};

} // namespace Ui
} // namespace DirtSim
