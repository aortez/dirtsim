#pragma once

#include "PhysicsControlHelpers.h"
#include "core/PhysicsSettings.h"
#include "lvgl/lvgl.h"
#include <array>
#include <unordered_map>

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

/**
 * @brief Consolidated physics panel with all physics controls in collapsible sections.
 *
 * Combines all physics controls into a single panel with 6 collapsible sections:
 * - General: Timescale, gravity, elasticity, air resistance, enable swap (5 controls).
 * - Pressure: Hydrostatic, dynamic, diffusion, iterations, scale (5 controls).
 * - Forces: Cohesion, adhesion, viscosity, friction, cohesion resist (5 controls).
 * - Swap Tuning: Buoyancy energy, cohesion bonds, horizontal flow, fluid lubrication (4 controls).
 * - Swap2: Horizontal non-fluid penalty, target resist, non-fluid energy (3 controls).
 * - Frag: Enabled, threshold, full threshold, spray fraction (4 controls).
 *
 * Total: 26 controls.
 */
class PhysicsPanel {
public:
    PhysicsPanel(lv_obj_t* container, Network::WebSocketService* wsService);
    ~PhysicsPanel();

    void updateFromSettings(const PhysicsSettings& settings);

private:
    lv_obj_t* container_;
    Network::WebSocketService* wsService_;

    PhysicsSettings settings_;
    // 5 + 5 + 5 + 4 + 3 + 4 = 26 controls.
    std::array<PhysicsControlHelpers::Control, 26> controls_;
    size_t controlCount_ = 0;
    std::unordered_map<lv_obj_t*, PhysicsControlHelpers::Control*> widgetToControl_;

    static void onGenericToggle(lv_event_t* e);
    static void onGenericValueChange(lv_event_t* e);

    PhysicsControlHelpers::Control* findControl(lv_obj_t* widget);
    void fetchSettings();
    void syncSettings();

    lv_obj_t* createCollapsibleSection(lv_obj_t* parent,
                                        const char* title,
                                        bool initiallyExpanded);
};

} // namespace Ui
} // namespace DirtSim
