#include "UiComponentManager.h"
#include "core/LoggingChannels.h"

namespace DirtSim {
namespace Ui {

UiComponentManager::UiComponentManager(lv_disp_t* display) : display(display)
{
    if (!display) {
        SLOG_ERROR("UiComponentManager initialized with null display");
        return;
    }

    // Initialize with the default screen.
    currentScreen = lv_disp_get_scr_act(display);
    SLOG_INFO("UiComponentManager initialized with display");
}

UiComponentManager::~UiComponentManager()
{
    SLOG_INFO("UiComponentManager cleanup started");

    // Reset unique_ptrs first (they'll clean up LVGL objects via callbacks).
    iconRail_.reset();
    expandablePanel_.reset();

    // Clean up any screens we created (not the default one).
    if (simulationScreen && simulationScreen != lv_disp_get_scr_act(display)) {
        cleanupScreen(simulationScreen);
    }
    if (mainMenuScreen && mainMenuScreen != lv_disp_get_scr_act(display)) {
        cleanupScreen(mainMenuScreen);
    }
    if (configScreen && configScreen != lv_disp_get_scr_act(display)) {
        cleanupScreen(configScreen);
    }

    SLOG_INFO("UiComponentManager cleanup completed");
}

lv_obj_t* UiComponentManager::getSimulationContainer()
{
    if (!display) return nullptr;

    simulationScreen = ensureScreen(simulationScreen, "simulation");
    transitionToScreen(simulationScreen);

    // Create layout structure if not already created.
    if (!simMainRow_) {
        createSimulationLayout();
    }

    return simulationScreen;
}

lv_obj_t* UiComponentManager::getCoreControlsContainer()
{
    getSimulationContainer(); // Ensure layout is created.
    // Deprecated: return panel content area for backward compatibility.
    return expandablePanel_ ? expandablePanel_->getContentArea() : nullptr;
}

lv_obj_t* UiComponentManager::getScenarioControlsContainer()
{
    getSimulationContainer(); // Ensure layout is created.
    // Deprecated: return panel content area for backward compatibility.
    return expandablePanel_ ? expandablePanel_->getContentArea() : nullptr;
}

lv_obj_t* UiComponentManager::getPhysicsControlsContainer()
{
    getSimulationContainer(); // Ensure layout is created.
    // Deprecated: return panel content area for backward compatibility.
    return expandablePanel_ ? expandablePanel_->getContentArea() : nullptr;
}

lv_obj_t* UiComponentManager::getWorldDisplayArea()
{
    getSimulationContainer(); // Ensure layout is created.
    return simWorldDisplayArea_;
}

lv_obj_t* UiComponentManager::getNeuralGridDisplayArea()
{
    getSimulationContainer(); // Ensure layout is created.
    return simNeuralGridDisplayArea_;
}

void UiComponentManager::setDisplayAreaRatio(uint32_t worldGrow, uint32_t neuralGrow)
{
    if (simWorldDisplayArea_) {
        lv_obj_set_flex_grow(simWorldDisplayArea_, worldGrow);
    }
    if (simNeuralGridDisplayArea_) {
        lv_obj_set_flex_grow(simNeuralGridDisplayArea_, neuralGrow);
    }
}

void UiComponentManager::setNeuralGridVisible(bool visible)
{
    if (neuralGridVisible_ == visible) return;

    neuralGridVisible_ = visible;

    if (simNeuralGridDisplayArea_) {
        if (visible) {
            lv_obj_clear_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            // 50/50 split when visible.
            setDisplayAreaRatio(1, 1);
        }
        else {
            lv_obj_add_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            // World gets full width when neural grid hidden.
            setDisplayAreaRatio(1, 0);
        }
    }

    LOG_DEBUG(Controls, "Neural grid visibility: {}", visible);
}

lv_obj_t* UiComponentManager::getMainMenuContainer()
{
    if (!display) return nullptr;

    mainMenuScreen = ensureScreen(mainMenuScreen, "main_menu");
    transitionToScreen(mainMenuScreen);
    return mainMenuScreen;
}

lv_obj_t* UiComponentManager::getConfigContainer()
{
    if (!display) return nullptr;

    configScreen = ensureScreen(configScreen, "config");
    transitionToScreen(configScreen);
    return configScreen;
}

void UiComponentManager::clearCurrentContainer()
{
    if (currentScreen) {
        lv_obj_clean(currentScreen);
        spdlog::debug("Cleared current container");
    }
}

void UiComponentManager::transitionToScreen(lv_obj_t* screen, bool animate)
{
    if (!screen || screen == currentScreen) {
        return;
    }

    if (animate) {
        lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    }
    else {
        lv_scr_load(screen);
    }

    currentScreen = screen;
    spdlog::debug("Transitioned to screen");
}

lv_obj_t* UiComponentManager::ensureScreen(lv_obj_t*& screen, const char* name)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        if (screen) {
            // Set medium gray background color.
            lv_obj_set_style_bg_color(screen, lv_color_hex(0x808080), 0);
            spdlog::debug("Created {} screen", name);
        }
        else {
            SLOG_ERROR("Failed to create {} screen", name);
        }
    }
    return screen;
}

void UiComponentManager::cleanupScreen(lv_obj_t*& screen)
{
    if (screen) {
        lv_obj_del(screen);
        screen = nullptr;
        spdlog::debug("Cleaned up screen");
    }
}

void UiComponentManager::createSimulationLayout()
{
    if (!simulationScreen) {
        SLOG_ERROR("createSimulationLayout: simulation screen not created");
        return;
    }

    // =========================================================================
    // NEW LAYOUT: Icon Rail + Expandable Panel + Display Area
    //
    // ┌────┬─────────┬──────────────────────────────────┐
    // │Icon│Expandable│                                  │
    // │Rail│ Panel   │   World Display (+ Neural Grid)  │
    // │48px│ 250px   │                                   │
    // │    │(hidden) │                                   │
    // └────┴─────────┴──────────────────────────────────┘
    // =========================================================================

    // Main container - horizontal flex.
    lv_obj_set_flex_flow(simulationScreen, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        simulationScreen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(simulationScreen, 0, 0);
    lv_obj_set_style_pad_gap(simulationScreen, 0, 0);

    // Create main row container (holds everything).
    simMainRow_ = lv_obj_create(simulationScreen);
    lv_obj_set_size(simMainRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(simMainRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        simMainRow_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(simMainRow_, 0, 0);
    lv_obj_set_style_pad_gap(simMainRow_, 0, 0);
    lv_obj_set_style_border_width(simMainRow_, 0, 0);
    lv_obj_set_style_bg_opa(simMainRow_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(simMainRow_, LV_OBJ_FLAG_SCROLLABLE);

    // -------------------------------------------------------------------------
    // Icon Rail (48px wide, full height).
    // -------------------------------------------------------------------------
    iconRail_ = std::make_unique<IconRail>(
        simMainRow_,
        [this](IconId selectedId, IconId previousId) {
            // Handle icon selection changes.
            LOG_INFO(
                Controls,
                "Icon selection changed: {} -> {}",
                static_cast<int>(previousId),
                static_cast<int>(selectedId));

            // Tree icon has special behavior - toggles neural grid.
            if (selectedId == IconId::TREE) {
                setNeuralGridVisible(true);
                // Don't show expandable panel for tree.
                if (expandablePanel_) {
                    expandablePanel_->hide();
                }
            }
            else if (previousId == IconId::TREE && selectedId != IconId::TREE) {
                // Switched away from tree - hide neural grid.
                setNeuralGridVisible(false);
            }

            // Show/hide expandable panel based on selection.
            if (expandablePanel_) {
                if (selectedId != IconId::COUNT && selectedId != IconId::TREE) {
                    expandablePanel_->show();
                }
                else if (selectedId == IconId::COUNT) {
                    expandablePanel_->hide();
                }
            }
        });

    // -------------------------------------------------------------------------
    // Expandable Panel (250px wide, hidden by default).
    // -------------------------------------------------------------------------
    expandablePanel_ = std::make_unique<ExpandablePanel>(simMainRow_);

    // -------------------------------------------------------------------------
    // Display Area (world + neural grid, fills remaining space).
    // -------------------------------------------------------------------------
    simDisplayArea_ = lv_obj_create(simMainRow_);
    lv_obj_set_height(simDisplayArea_, LV_PCT(100));
    lv_obj_set_flex_grow(simDisplayArea_, 1); // Take all remaining horizontal space.
    lv_obj_set_flex_flow(simDisplayArea_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        simDisplayArea_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(simDisplayArea_, 0, 0);
    lv_obj_set_style_pad_gap(simDisplayArea_, 0, 0);
    lv_obj_set_style_border_width(simDisplayArea_, 0, 0);
    lv_obj_set_style_bg_opa(simDisplayArea_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(simDisplayArea_, LV_OBJ_FLAG_SCROLLABLE);

    // World display area (flex-grow to fill space).
    simWorldDisplayArea_ = lv_obj_create(simDisplayArea_);
    lv_obj_set_size(simWorldDisplayArea_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(simWorldDisplayArea_, 1);
    lv_obj_set_style_pad_all(simWorldDisplayArea_, 0, 0);
    lv_obj_set_style_border_width(simWorldDisplayArea_, 0, 0);
    lv_obj_set_style_bg_opa(simWorldDisplayArea_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(simWorldDisplayArea_, LV_OBJ_FLAG_SCROLLABLE);

    // Neural grid display area (hidden by default).
    simNeuralGridDisplayArea_ = lv_obj_create(simDisplayArea_);
    lv_obj_set_size(simNeuralGridDisplayArea_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(simNeuralGridDisplayArea_, 1);
    lv_obj_set_style_pad_all(simNeuralGridDisplayArea_, 5, 0);
    lv_obj_set_style_border_width(simNeuralGridDisplayArea_, 1, 0);
    lv_obj_set_style_border_color(simNeuralGridDisplayArea_, lv_color_hex(0x606060), 0);
    lv_obj_set_style_bg_color(simNeuralGridDisplayArea_, lv_color_hex(0x303030), 0);
    lv_obj_clear_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_SCROLLABLE);

    // Start with neural grid hidden - set flags directly since neuralGridVisible_ starts false.
    lv_obj_add_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(simNeuralGridDisplayArea_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    setDisplayAreaRatio(1, 0);

    SLOG_INFO("UiComponentManager: Created icon-based simulation layout");
}

} // namespace Ui
} // namespace DirtSim
