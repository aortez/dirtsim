#pragma once

#include "ui/controls/ExpandablePanel.h"
#include "ui/controls/IconRail.h"
#include <lvgl.h>
#include <memory>
#include <string>

namespace DirtSim {
namespace Ui {

/**
 * @brief Lightweight manager for LVGL resources and screen management.
 *
 * UiComponentManager handles LVGL-specific resources like screens and containers,
 * but does NOT own business logic UI components. States own their UI
 * components and use UiComponentManager to get appropriate containers.
 *
 * New layout structure (icon-based):
 * ┌────┬─────────┬──────────────────────────────────┐
 * │Icon│Expandable│                                  │
 * │Rail│ Panel   │       World Display Area          │
 * │48px│ 250px   │       (+ Neural Grid when visible)│
 * │    │(hidden) │                                   │
 * └────┴─────────┴──────────────────────────────────┘
 */
class UiComponentManager {
public:
    explicit UiComponentManager(lv_disp_t* display);

    ~UiComponentManager();

    /**
     * @brief Get container for simulation UI.
     * Creates/prepares the simulation screen if needed.
     */
    lv_obj_t* getSimulationContainer();

    /**
     * @brief Get the icon rail component.
     */
    IconRail* getIconRail() { return iconRail_.get(); }

    /**
     * @brief Get the expandable panel component.
     */
    ExpandablePanel* getExpandablePanel() { return expandablePanel_.get(); }

    /**
     * @brief Get container for world display area (canvas grid).
     * Creates layout on simulation screen if needed.
     */
    lv_obj_t* getWorldDisplayArea();

    /**
     * @brief Get container for neural grid display area (15x15 tree vision).
     * Creates layout on simulation screen if needed.
     */
    lv_obj_t* getNeuralGridDisplayArea();

    /**
     * @brief Show or hide the neural grid display area.
     * @param visible True to show (50/50 split with world), false to hide.
     */
    void setNeuralGridVisible(bool visible);

    /**
     * @brief Check if neural grid is currently visible.
     */
    bool isNeuralGridVisible() const { return neuralGridVisible_; }

    /**
     * @brief Adjust the flex_grow ratio between world and neural grid display areas.
     * @param worldGrow Flex grow value for world display area.
     * @param neuralGrow Flex grow value for neural grid display area.
     */
    void setDisplayAreaRatio(uint32_t worldGrow, uint32_t neuralGrow);

    /**
     * @brief Get container for main menu UI.
     * Creates/prepares the menu screen if needed.
     */
    lv_obj_t* getMainMenuContainer();

    /**
     * @brief Get container for configuration UI.
     * Creates/prepares the config screen if needed.
     */
    lv_obj_t* getConfigContainer();

    /**
     * @brief Clear the current container of all children.
     * Called when states exit to ensure clean transitions.
     */
    void clearCurrentContainer();

    /**
     * @brief Get the current active screen.
     */
    lv_obj_t* getCurrentScreen() const { return currentScreen; }

    /**
     * @brief Transition to a specific screen with optional animation.
     */
    void transitionToScreen(lv_obj_t* screen, bool animate = true);

    // =========================================================================
    // DEPRECATED - These methods exist for backward compatibility during
    // transition. They return the expandable panel's content area.
    // =========================================================================

    /**
     * @brief Get container for core controls.
     * @deprecated Use getExpandablePanel()->getContentArea() instead.
     */
    lv_obj_t* getCoreControlsContainer();

    /**
     * @brief Get container for scenario-specific controls.
     * @deprecated Use getExpandablePanel()->getContentArea() instead.
     */
    lv_obj_t* getScenarioControlsContainer();

    /**
     * @brief Get container for physics parameter controls.
     * @deprecated Use getExpandablePanel()->getContentArea() instead.
     */
    lv_obj_t* getPhysicsControlsContainer();

private:
    lv_disp_t* display;

    // Screens for different states.
    lv_obj_t* simulationScreen = nullptr;
    lv_obj_t* mainMenuScreen = nullptr;
    lv_obj_t* configScreen = nullptr;

    // Current active screen.
    lv_obj_t* currentScreen = nullptr;

    // New icon-based layout components.
    std::unique_ptr<IconRail> iconRail_;
    std::unique_ptr<ExpandablePanel> expandablePanel_;

    // Simulation screen layout containers.
    lv_obj_t* simMainRow_ = nullptr;         // Main horizontal row (icon rail + rest).
    lv_obj_t* simDisplayArea_ = nullptr;     // Contains world + neural grid.
    lv_obj_t* simWorldDisplayArea_ = nullptr;
    lv_obj_t* simNeuralGridDisplayArea_ = nullptr;

    bool neuralGridVisible_ = false;

    /**
     * @brief Create a screen if it doesn't exist.
     */
    lv_obj_t* ensureScreen(lv_obj_t*& screen, const char* name);

    /**
     * @brief Clean up a screen and its children.
     */
    void cleanupScreen(lv_obj_t*& screen);

    /**
     * @brief Create the simulation screen layout structure.
     * Called lazily when first simulation container is requested.
     */
    void createSimulationLayout();
};

} // namespace Ui
} // namespace DirtSim
