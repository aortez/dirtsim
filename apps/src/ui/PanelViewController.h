#pragma once

#include "lvgl/lvgl.h"
#include <string>
#include <unordered_map>

namespace DirtSim {
namespace Ui {

/**
 * @brief Manages multiple views within a panel with modal-style navigation.
 *
 * Provides a simple API for creating and switching between views:
 * - Each view is a full-panel container.
 * - Only one view is visible at a time.
 * - Switching views automatically hides the current view and shows the target.
 *
 * This creates consistent modal navigation similar to PhysicsPanel, where
 * clicking a button fills the entire panel with that view's content, hiding
 * everything else until the user navigates back.
 *
 * Example usage:
 * ```cpp
 * PanelViewController vc(panelContainer);
 * lv_obj_t* mainView = vc.createView("main");
 * // ... add controls to mainView ...
 * lv_obj_t* modalView = vc.createView("options");
 * // ... add back button and options to modalView ...
 * vc.showView("main"); // Initially show main.
 * // Later: vc.showView("options");
 * ```
 */
class PanelViewController {
public:
    explicit PanelViewController(lv_obj_t* panelContainer);
    ~PanelViewController();

    lv_obj_t* createView(const std::string& viewId);
    void showView(const std::string& viewId);
    std::string getCurrentView() const;
    bool hasView(const std::string& viewId) const;
    lv_obj_t* getView(const std::string& viewId) const;

private:
    lv_obj_t* container_;
    std::unordered_map<std::string, lv_obj_t*> views_;
    std::string currentViewId_;
};

} // namespace Ui
} // namespace DirtSim
