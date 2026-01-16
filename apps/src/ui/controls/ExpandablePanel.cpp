#include "ExpandablePanel.h"
#include "core/ColorNames.h"
#include "core/LoggingChannels.h"
#include <spdlog/spdlog.h>

namespace {
uint32_t rgbaToRgb(uint32_t rgba)
{
    return rgba >> 8;
}
} // namespace

namespace DirtSim {
namespace Ui {

ExpandablePanel::ExpandablePanel(lv_obj_t* parent)
{
    // Create the container.
    container_ = lv_obj_create(parent);
    if (!container_) {
        LOG_ERROR(Controls, "Failed to create ExpandablePanel container");
        return;
    }

    // Style the container.
    lv_obj_set_size(container_, width_, LV_PCT(100));
    lv_obj_set_style_bg_color(container_, lv_color_hex(rgbaToRgb(ColorNames::uiGrayDark())), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    // Create scrollable content area inside.
    contentArea_ = lv_obj_create(container_);
    if (!contentArea_) {
        LOG_ERROR(Controls, "Failed to create ExpandablePanel content area");
        return;
    }

    // Style the content area.
    lv_obj_set_size(contentArea_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(contentArea_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        contentArea_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(contentArea_, 5, 0);
    lv_obj_set_style_pad_row(contentArea_, 5, 0);
    lv_obj_set_style_bg_opa(contentArea_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(contentArea_, 0, 0);
    lv_obj_set_scroll_dir(contentArea_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(contentArea_, LV_SCROLLBAR_MODE_AUTO);

    // Start hidden - set flags directly since visible_ starts as false.
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    LOG_INFO(Controls, "ExpandablePanel created ({}px wide, hidden)", width_);
}

ExpandablePanel::~ExpandablePanel()
{
    LOG_INFO(Controls, "ExpandablePanel destroyed");
}

void ExpandablePanel::show()
{
    if (visible_) return;

    visible_ = true;
    // Clear both HIDDEN and IGNORE_LAYOUT so panel participates in flex layout.
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    LOG_DEBUG(Controls, "ExpandablePanel shown");
}

void ExpandablePanel::hide()
{
    if (!visible_) return;

    visible_ = false;
    // Set both HIDDEN and IGNORE_LAYOUT so flex container doesn't reserve space.
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    LOG_DEBUG(Controls, "ExpandablePanel hidden");
}

void ExpandablePanel::clearContent()
{
    if (contentArea_) {
        lv_obj_clean(contentArea_);
        LOG_DEBUG(Controls, "ExpandablePanel content cleared");
    }
}

void ExpandablePanel::setWidth(int width)
{
    if (width <= 0 || width_ == width) {
        return;
    }

    width_ = width;
    if (container_) {
        lv_obj_set_width(container_, width_);
    }
}

void ExpandablePanel::resetWidth()
{
    setWidth(PANEL_WIDTH);
}

} // namespace Ui
} // namespace DirtSim
