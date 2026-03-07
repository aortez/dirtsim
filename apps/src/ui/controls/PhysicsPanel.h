#pragma once

#include "PhysicsControlHelpers.h"
#include "core/PhysicsSettings.h"
#include "lvgl/lvgl.h"
#include "ui/PanelViewController.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace DirtSim {

namespace Network {
class WebSocketServiceInterface;
}

namespace Ui {

/**
 * Modal physics panel with two-level navigation: section menu → section controls.
 */
class PhysicsPanel {
public:
    PhysicsPanel(lv_obj_t* container, Network::WebSocketServiceInterface* wsService);
    ~PhysicsPanel();

    void updateFromSettings(const PhysicsSettings& settings);

private:
    lv_obj_t* container_;
    Network::WebSocketServiceInterface* wsService_;

    // View controller for modal navigation.
    std::unique_ptr<PanelViewController> viewController_;

    // View state.
    int activeSection_ = -1; // -1 = none, 0-6 = section index.

    // Physics settings and controls (only populated when in section view).
    PhysicsSettings settings_;
    std::vector<PhysicsControlHelpers::Control> controls_;
    std::unordered_map<lv_obj_t*, PhysicsControlHelpers::Control*> widgetToControl_;

    // Section button to index mapping.
    std::unordered_map<lv_obj_t*, int> buttonToSection_;

    // Cached section configs.
    PhysicsControlHelpers::AllColumnConfigs configs_;

    // View management.
    void createMenuView(lv_obj_t* view);
    void createSectionView(lv_obj_t* view, int sectionIndex);
    void showSection(int sectionIndex);
    void showMenu();

    // Callbacks.
    static void onSectionClicked(lv_event_t* e);
    static void onBackClicked(lv_event_t* e);
    static void onGenericToggle(lv_event_t* e);
    static void onGenericValueChange(lv_event_t* e);

    PhysicsControlHelpers::Control* findControl(lv_obj_t* widget);
    void fetchSettings();
    void syncSettings();

    // Helper to get section config by index.
    const PhysicsControlHelpers::ColumnConfig& getSectionConfig(int index) const;
};

} // namespace Ui
} // namespace DirtSim
