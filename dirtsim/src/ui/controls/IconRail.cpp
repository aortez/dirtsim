#include "IconRail.h"
#include "core/LoggingChannels.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

IconRail::IconRail(lv_obj_t* parent, SelectCallback onSelect) : onSelectCallback_(std::move(onSelect))
{
    // Define our icon configuration with per-icon colors.
    iconConfigs_ = {
        { IconId::CORE, LV_SYMBOL_HOME, "Core Controls", 0x87CEEB },       // Light blue.
        { IconId::SCENARIO, LV_SYMBOL_VIDEO, "Scenario", 0xFFA500 },       // Orange.
        { IconId::PHYSICS, LV_SYMBOL_SETTINGS, "Physics", 0xC0C0C0 },      // Silver.
        { IconId::TREE, LV_SYMBOL_EYE_OPEN, "Tree Vision", 0x32CD32 },     // Lime green.
    };

    createIcons(parent);

    // Tree icon starts hidden (only shown when tree exists).
    setTreeIconVisible(false);

    LOG_INFO(Controls, "IconRail created with {} icons", iconConfigs_.size());
}

IconRail::~IconRail()
{
    // LVGL handles cleanup of child objects when parent is deleted.
    LOG_INFO(Controls, "IconRail destroyed");
}

void IconRail::createIcons(lv_obj_t* parent)
{
    // Create the container.
    container_ = lv_obj_create(parent);
    if (!container_) {
        LOG_ERROR(Controls, "Failed to create IconRail container");
        return;
    }

    // Style the container.
    lv_obj_set_size(container_, RAIL_WIDTH, LV_PCT(100));
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container_, (RAIL_WIDTH - ICON_SIZE) / 2, 0);
    lv_obj_set_style_pad_row(container_, GAP, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Create buttons for each icon.
    for (size_t i = 0; i < iconConfigs_.size(); i++) {
        const auto& config = iconConfigs_[i];

        lv_obj_t* btn = lv_btn_create(container_);
        if (!btn) {
            LOG_WARN(Controls, "Failed to create button for icon {}", config.tooltip);
            buttons_.push_back(nullptr);
            continue;
        }

        // Style the button.
        lv_obj_set_size(btn, ICON_SIZE, ICON_SIZE);
        lv_obj_set_style_bg_color(btn, lv_color_hex(BG_COLOR), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        // Pressed state.
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), LV_STATE_PRESSED);

        // Create icon label with per-icon color.
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, config.symbol);
        lv_obj_set_style_text_color(label, lv_color_hex(config.color), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
        lv_obj_center(label);

        // Store index in user data for callback.
        lv_obj_set_user_data(btn, this);

        // Add click callback.
        lv_obj_add_event_cb(btn, onIconClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));

        buttons_.push_back(btn);
    }
}

void IconRail::onIconClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    IconRail* self = static_cast<IconRail*>(lv_obj_get_user_data(btn));
    size_t index = reinterpret_cast<size_t>(lv_event_get_user_data(e));

    if (!self || index >= self->iconConfigs_.size()) return;

    IconId clickedId = self->iconConfigs_[index].id;
    IconId previousId = self->selectedId_;

    LOG_DEBUG(Controls, "Icon clicked: {} (was: {})", static_cast<int>(clickedId), static_cast<int>(previousId));

    // Toggle behavior: clicking selected icon deselects it.
    if (clickedId == self->selectedId_) {
        self->selectedId_ = IconId::COUNT;
    }
    else {
        self->selectedId_ = clickedId;
    }

    self->updateButtonVisuals();

    // Notify primary callback.
    if (self->onSelectCallback_) {
        self->onSelectCallback_(self->selectedId_, previousId);
    }

    // Notify secondary callback (e.g., SimPlayground).
    if (self->secondaryCallback_) {
        self->secondaryCallback_(self->selectedId_, previousId);
    }
}

void IconRail::updateButtonVisuals()
{
    for (size_t i = 0; i < buttons_.size() && i < iconConfigs_.size(); i++) {
        lv_obj_t* btn = buttons_[i];
        if (!btn) continue;

        bool isSelected = (iconConfigs_[i].id == selectedId_);
        uint32_t color = isSelected ? SELECTED_COLOR : BG_COLOR;
        lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    }
}

void IconRail::setTreeIconVisible(bool visible)
{
    treeIconVisible_ = visible;

    // Find tree button and show/hide it.
    for (size_t i = 0; i < iconConfigs_.size() && i < buttons_.size(); i++) {
        if (iconConfigs_[i].id == IconId::TREE && buttons_[i]) {
            if (visible) {
                lv_obj_clear_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
            }
            else {
                lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);

                // If tree was selected, deselect it.
                if (selectedId_ == IconId::TREE) {
                    IconId previousId = selectedId_;
                    selectedId_ = IconId::COUNT;
                    updateButtonVisuals();

                    if (onSelectCallback_) {
                        onSelectCallback_(selectedId_, previousId);
                    }
                }
            }
            break;
        }
    }

    LOG_DEBUG(Controls, "Tree icon visibility: {}", visible);
}

void IconRail::selectIcon(IconId id)
{
    if (id == selectedId_) return;

    IconId previousId = selectedId_;
    selectedId_ = id;
    updateButtonVisuals();

    if (onSelectCallback_) {
        onSelectCallback_(selectedId_, previousId);
    }
}

void IconRail::deselectAll()
{
    if (selectedId_ == IconId::COUNT) return;

    IconId previousId = selectedId_;
    selectedId_ = IconId::COUNT;
    updateButtonVisuals();

    if (onSelectCallback_) {
        onSelectCallback_(selectedId_, previousId);
    }
}

} // namespace Ui
} // namespace DirtSim
