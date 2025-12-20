#pragma once

#include "PhysicsControlHelpers.h"
#include "core/PhysicsSettings.h"
#include "lvgl/lvgl.h"
#include <unordered_map>
#include <vector>

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Modal physics panel with two-level navigation.
 *
 * Provides a menu of 6 physics sections. Clicking a section shows only that
 * section's controls with a back button to return to the menu. This modal
 * approach works better on small screens than scrollable collapsible sections.
 *
 * Sections:
 * - General: Timescale, gravity, elasticity, air resistance, enable swap (5 controls).
 * - Pressure: Hydrostatic, dynamic, diffusion, iterations, scale (5 controls).
 * - Forces: Cohesion, adhesion, viscosity, friction, cohesion resist (5 controls).
 * - Swap Tuning: Buoyancy energy, cohesion bonds, horizontal flow, fluid lubrication (4 controls).
 * - Swap2: Horizontal non-fluid penalty, target resist, non-fluid energy (3 controls).
 * - Frag: Enabled, threshold, full threshold, spray fraction (4 controls).
 */
class PhysicsPanel {
public:
    PhysicsPanel(lv_obj_t* container, Network::WebSocketService* wsService);
    ~PhysicsPanel();

    void updateFromSettings(const PhysicsSettings& settings);

private:
    enum class ViewMode { MENU, SECTION };

    lv_obj_t* container_;
    Network::WebSocketService* wsService_;

    // View state.
    ViewMode currentView_ = ViewMode::MENU;
    int activeSection_ = -1; // -1 = none, 0-5 = section index.

    // Containers for the two views.
    lv_obj_t* menuContainer_ = nullptr;
    lv_obj_t* sectionContainer_ = nullptr;

    // Physics settings and controls (only populated when in section view).
    PhysicsSettings settings_;
    std::vector<PhysicsControlHelpers::Control> controls_;
    std::unordered_map<lv_obj_t*, PhysicsControlHelpers::Control*> widgetToControl_;

    // Cached section configs.
    PhysicsControlHelpers::AllColumnConfigs configs_;

    // View management.
    void createMenuView();
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
