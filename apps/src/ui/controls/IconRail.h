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

/**
 * @brief Identifiers for icons in an IconRail.
 */
enum class IconId { CORE = 0, EVOLUTION, NETWORK, PHYSICS, PLAY, SCENARIO, TREE, COUNT };

enum class RailMode {
    Normal,   // Full width with all icon buttons.
    Minimized // Narrow width with single expand button.
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
    /**
     * @brief Construct the icon rail.
     * @param parent Parent LVGL object to attach to.
     * @param eventSink Event sink for queueing icon selection events.
     */
    IconRail(lv_obj_t* parent, EventSink* eventSink);
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
     * @return Selected IconId, or IconId::COUNT if none selected.
     */
    IconId getSelectedIcon() const { return selectedId_; }

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

    RailMode getMode() const { return mode_; }
    void setMode(RailMode mode);
    void toggleMode();
    bool isMinimized() const { return mode_ == RailMode::Minimized; }

    void setSwipeZoneEnabled(bool enabled);

private:
    lv_obj_t* container_ = nullptr;
    std::vector<lv_obj_t*> buttons_;
    std::vector<IconConfig> iconConfigs_;
    std::unique_ptr<IconFont> iconFont_; // FontAwesome loaded at runtime.

    IconId selectedId_ = IconId::COUNT;
    bool treeIconVisible_ = false;
    std::vector<IconId>
        allowedIcons_; // Icons that are allowed to be shown (set by setVisibleIcons).
    EventSink* eventSink_ = nullptr;

    // Mode support.
    RailMode mode_ = RailMode::Normal;
    lv_obj_t* expandButton_ = nullptr;   // Shown in minimized mode (overlay on screen).
    lv_obj_t* collapseButton_ = nullptr; // Shown in normal mode.
    lv_obj_t* swipeZone_ = nullptr;      // Invisible swipe detection area (overlay on screen).

    // Auto-shrink timer (minimizes rail after inactivity).
    lv_timer_t* autoShrinkTimer_ = nullptr;
    static constexpr uint32_t AUTO_SHRINK_TIMEOUT_MS = 10000; // 10 seconds.

    // Colors.
    static constexpr uint32_t BG_COLOR = 0x303030;
    static constexpr uint32_t SELECTED_COLOR = 0x0066CC;
    static constexpr uint32_t ICON_COLOR = 0xFFFFFF;

    // Dimensions optimized for HyperPixel 4.0 (480px height).
    // With 4 icons: 4×96 + 3×12 = 420px (fits nicely with room to spare).
    static constexpr int RAIL_WIDTH = 108;
    static constexpr int MINIMIZED_RAIL_WIDTH = 40; // Half of ACTION_SIZE.
    static constexpr int ICON_SIZE = 96;
    static constexpr int GAP = 12;
    static constexpr int SWIPE_ZONE_WIDTH = 80; // Width of invisible swipe detection area.

    void createIcons(lv_obj_t* parent);
    void createModeButtons();
    void createAutoShrinkTimer();
    void resetAutoShrinkTimer();
    void applyMode();
    void updateButtonVisuals();

    // Static LVGL callbacks.
    static void onIconClicked(lv_event_t* e);
    static void onModeButtonClicked(lv_event_t* e);
    static void onAutoShrinkTimer(lv_timer_t* timer);
    static void onGesture(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
