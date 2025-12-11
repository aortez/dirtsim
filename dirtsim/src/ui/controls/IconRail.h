#pragma once

#include "lvgl/lvgl.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <functional>

namespace DirtSim {
namespace Ui {

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
    using IconId = LVGLBuilder::IconId;
    using SelectCallback = std::function<void(IconId selectedId, IconId previousId)>;

    /**
     * @brief Construct the icon rail.
     * @param parent Parent LVGL object to attach to.
     * @param onSelect Callback when an icon is selected/deselected.
     */
    IconRail(lv_obj_t* parent, SelectCallback onSelect);
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

    /**
     * @brief Get the currently selected icon.
     * @return Selected IconId, or IconId::COUNT if none selected.
     */
    IconId getSelectedIcon() const { return selectedId_; }

    /**
     * @brief Programmatically select an icon (updates visuals and triggers callback).
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

    /**
     * @brief Set an additional callback for icon selection changes.
     * This is called after the primary callback set in the constructor.
     */
    void setSecondaryCallback(SelectCallback callback) { secondaryCallback_ = std::move(callback); }

private:
    lv_obj_t* container_ = nullptr;
    std::vector<lv_obj_t*> buttons_;
    std::vector<LVGLBuilder::IconConfig> iconConfigs_;

    IconId selectedId_ = IconId::COUNT;
    bool treeIconVisible_ = false;
    SelectCallback onSelectCallback_;
    SelectCallback secondaryCallback_;

    // Colors.
    static constexpr uint32_t BG_COLOR = 0x303030;
    static constexpr uint32_t SELECTED_COLOR = 0x0066CC;
    static constexpr uint32_t ICON_COLOR = 0xFFFFFF;

    // Dimensions.
    static constexpr int RAIL_WIDTH = 48;
    static constexpr int ICON_SIZE = 44;
    static constexpr int GAP = 4;

    void createIcons(lv_obj_t* parent);
    void updateButtonVisuals();

    // Static LVGL callback.
    static void onIconClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
