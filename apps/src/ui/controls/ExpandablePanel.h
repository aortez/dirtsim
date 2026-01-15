#pragma once

#include "lvgl/lvgl.h"
#include <functional>
#include <memory>

namespace DirtSim {
namespace Ui {

/**
 * @brief Expandable panel that slides out from the icon rail.
 *
 * Shows/hides a content area to the right of the icon rail.
 * Content is swapped when different icons are selected.
 */
class ExpandablePanel {
public:
    /**
     * @brief Construct the expandable panel.
     * @param parent Parent LVGL object to attach to.
     */
    explicit ExpandablePanel(lv_obj_t* parent);
    ~ExpandablePanel();

    // Prevent copying.
    ExpandablePanel(const ExpandablePanel&) = delete;
    ExpandablePanel& operator=(const ExpandablePanel&) = delete;

    /**
     * @brief Get the LVGL container object.
     */
    lv_obj_t* getContainer() const { return container_; }

    /**
     * @brief Get the content area where panel contents are placed.
     */
    lv_obj_t* getContentArea() const { return contentArea_; }

    /**
     * @brief Show the panel.
     */
    void show();

    /**
     * @brief Hide the panel.
     */
    void hide();

    /**
     * @brief Check if panel is currently visible.
     */
    bool isVisible() const { return visible_; }

    /**
     * @brief Clear all content from the panel.
     */
    void clearContent();

    /**
     * @brief Get the panel width.
     */
    int getWidth() const { return PANEL_WIDTH; }

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* contentArea_ = nullptr;
    bool visible_ = false;

    // Dimensions.
    static constexpr int PANEL_WIDTH = 250;
};

} // namespace Ui
} // namespace DirtSim
