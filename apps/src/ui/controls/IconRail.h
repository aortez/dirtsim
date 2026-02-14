#pragma once

#include "core/IconFont.h"
#include "lvgl/lvgl.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace DirtSim {
namespace Ui {

// Forward declaration to avoid circular dependency with Event.h.
class EventSink;
class FractalAnimator;

/**
 * @brief Identifiers for icons in an IconRail.
 */
enum class IconId {
    NONE = 0,
    CORE = 1,
    EVOLUTION = 2,
    NETWORK = 3,
    PHYSICS = 4,
    PLAY = 5,
    SCENARIO = 6,
    TREE = 7,
    GENOME_BROWSER = 8,
    TRAINING_RESULTS = 9,
    MUSIC = 10,
    DUCK = 11,
    SETTINGS = 12
};

enum class RailMode {
    Normal,   // Full width with all icon buttons.
    Minimized // Narrow width with single expand button.
};

enum class RailLayout { SingleColumn, TwoColumn };

enum class MinimizedAffordanceAnchor {
    LeftCenter,
    LeftTop,
    LeftBottom,
};

struct MinimizedAffordanceStyle {
    MinimizedAffordanceAnchor anchor = MinimizedAffordanceAnchor::LeftCenter;
    int width = 0;  // Use default width when <= 0.
    int height = 0; // Use default height when <= 0.
    int offsetX = 0;
    int offsetY = 0;
};

/**
 * @brief Configuration for a single icon in an IconRail.
 */
struct IconConfig {
    IconId id;
    const char* symbol;        // LV_SYMBOL_* or text.
    const char* tooltip;       // Description for accessibility.
    uint32_t color = 0xFFFFFF; // Icon color (default white).
};

/**
 * @brief Vertical rail of icon buttons for navigation.
 *
 * Provides a 48px wide column of icons that control panel visibility.
 * Only one panel can be open at a time (radio-button behavior).
 * The Tree icon has special behavior - it toggles neural grid visibility
 * rather than opening a panel.
 */
class IconRail {
public:
    static constexpr int RAIL_WIDTH = 108;
    static constexpr int RAIL_WIDTH_TWO_COLUMN = 216;
    static constexpr int MINIMIZED_RAIL_WIDTH = 0;
    static constexpr int MINIMIZED_AFFORDANCE_DEFAULT_WIDTH = 80;
    static constexpr int MINIMIZED_AFFORDANCE_DEFAULT_HEIGHT = 160;
    static constexpr int MINIMIZED_AFFORDANCE_SQUARE_SIZE = 120;

    /**
     * @brief Construct the icon rail.
     * @param parent Parent LVGL object to attach to.
     * @param eventSink Event sink for queueing icon selection events.
     */
    IconRail(lv_obj_t* parent, EventSink* eventSink, FractalAnimator* fractalAnimator);
    ~IconRail();

    // Prevent copying.
    IconRail(const IconRail&) = delete;
    IconRail& operator=(const IconRail&) = delete;

    /**
     * @brief Get the LVGL container object.
     */
    lv_obj_t* getContainer() const { return container_; }

    /**
     * @brief Show or hide the tree icon based on tree presence.
     */
    void setTreeIconVisible(bool visible);
    void setVisibleIcons(const std::vector<IconId>& visibleIcons);

    /**
     * @brief Get the currently selected icon.
     * @return Selected IconId, or IconId::NONE if none selected.
     */
    IconId getSelectedIcon() const { return selectedId_; }

    /**
     * @brief Check if an icon is currently visible/selectable.
     */
    bool isIconSelectable(IconId id) const;

    /**
     * @brief Programmatically select an icon (updates visuals and queues event).
     */
    void selectIcon(IconId id);

    /**
     * @brief Deselect the current icon (closes any open panel).
     */
    void deselectAll();

    /**
     * @brief Check if tree icon is currently visible.
     */
    bool isTreeIconVisible() const { return treeIconVisible_; }

    void showIcons();

    RailMode getMode() const { return mode_; }
    void setMode(RailMode mode);
    void toggleMode();
    bool isMinimized() const { return mode_ == RailMode::Minimized; }

    RailLayout getLayout() const { return layout_; }
    void setLayout(RailLayout layout);

    static MinimizedAffordanceStyle minimizedAffordanceLeftCenter();
    static MinimizedAffordanceStyle minimizedAffordanceLeftTopSquare();
    static MinimizedAffordanceStyle minimizedAffordanceLeftBottomSquare();

    MinimizedAffordanceStyle getMinimizedAffordanceStyle() const
    {
        return minimizedAffordanceStyle_;
    }
    void setMinimizedAffordanceStyle(const MinimizedAffordanceStyle& style);

    void setVisible(bool visible);
    bool isVisible() const { return visible_; }

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* iconsViewport_ = nullptr;
    lv_obj_t* iconsLayout_ = nullptr;
    std::vector<lv_obj_t*> buttons_;
    std::vector<IconConfig> iconConfigs_;
    std::unique_ptr<IconFont> iconFont_; // FontAwesome loaded at runtime.

    IconId selectedId_ = IconId::NONE;
    bool treeIconVisible_ = false;
    std::vector<IconId>
        allowedIcons_; // Icons that are allowed to be shown (set by setVisibleIcons).
    EventSink* eventSink_ = nullptr;
    FractalAnimator* fractalAnimator_ = nullptr;
    uint64_t duckViewId_ = 0;

    // Mode support.
    RailMode mode_ = RailMode::Normal;
    RailLayout layout_ = RailLayout::SingleColumn;
    bool visible_ = true;
    MinimizedAffordanceStyle minimizedAffordanceStyle_{};
    lv_obj_t* expandButton_ = nullptr;   // Shown in minimized mode (overlay on screen).
    lv_obj_t* collapseButton_ = nullptr; // Shown in normal mode.

    static constexpr uint32_t MODE_ANIM_DURATION_MS = 250;

    // Dimensions optimized for HyperPixel 4.0 (480px height).
    static constexpr int ICON_SIZE = 96;
    static constexpr int GAP = 12;
    static constexpr int ICON_PAD = (RAIL_WIDTH - ICON_SIZE) / 2;

    void createIcons(lv_obj_t* parent);
    void createModeButtons();
    void applyMode();
    void applyExpandButtonGeometry();
    void updateButtonVisuals();
    void configureDuckIcon(lv_obj_t* button);

    // Static LVGL callbacks.
    static void onIconClicked(lv_event_t* e);
    static void onModeButtonClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
