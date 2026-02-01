#include "IconRail.h"
#include "core/Assert.h"
#include "core/ColorNames.h"
#include "core/IconFont.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace {
uint32_t rgbaToRgb(uint32_t rgba)
{
    return rgba >> 8;
}

constexpr lv_opa_t DIMMED_ICON_OPA = LV_OPA_60;
} // namespace

namespace DirtSim {
namespace Ui {

IconRail::IconRail(lv_obj_t* parent, EventSink* eventSink) : eventSink_(eventSink)
{
    iconFont_ = std::make_unique<IconFont>(ICON_SIZE - 36);

    // Define our icon configuration with FontAwesome icons and per-icon colors.
    // Order determines display order in the rail.
    iconConfigs_ = {
        { IconId::PLAY, IconFont::PLAY, "Play Simulation", 0x90EE90 },         // Light green.
        { IconId::CORE, IconFont::HOME, "Core Controls", 0x87CEEB },           // Light blue.
        { IconId::EVOLUTION, IconFont::CHART_LINE, "Evolution", 0xDA70D6 },    // Orchid/purple.
        { IconId::GENOME_BROWSER, IconFont::DNA, "Genome Browser", 0x40E0D0 }, // Turquoise.
        { IconId::TRAINING_RESULTS, IconFont::FILE_CABINET, "Training Results", 0xFFD700 }, // Gold.
        { IconId::SCENARIO, IconFont::FILM, "Scenario", 0xFFA500 }, // Orange.
        { IconId::NETWORK, IconFont::WIFI, "Network", 0x00CED1 },   // Dark turquoise.
        { IconId::PHYSICS, IconFont::COG, "Physics", 0xC0C0C0 },    // Silver.
        { IconId::TREE, IconFont::BRAIN, "Tree Vision", 0x32CD32 }, // Lime green.
    };

    createIcons(parent);
    createModeButtons();

    // Tree icon starts hidden (only shown when tree exists).
    setTreeIconVisible(false);

    // Start in normal mode (buttons already visible from createIcons).
    applyMode();

    LOG_INFO(Controls, "IconRail created with {} icons", iconConfigs_.size());
}

IconRail::~IconRail()
{
    if (autoShrinkTimer_) {
        lv_timer_delete(autoShrinkTimer_);
        autoShrinkTimer_ = nullptr;
    }

    // Delete overlay objects (they're children of the screen, not the container).
    if (expandButton_) {
        lv_obj_delete(expandButton_);
        expandButton_ = nullptr;
    }

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
    lv_obj_set_style_bg_color(container_, lv_color_hex(rgbaToRgb(ColorNames::uiGrayDark())), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Create buttons for each icon using ActionButton.
    for (size_t i = 0; i < iconConfigs_.size(); i++) {
        const auto& config = iconConfigs_[i];

        lv_obj_t* btnContainer = LVGLBuilder::actionButton(container_)
                                     .icon(config.symbol)
                                     .font(iconFont_->font())
                                     .mode(LVGLBuilder::ActionMode::Toggle)
                                     .size(ICON_SIZE)
                                     .glowColor(config.color)
                                     .textColor(config.color)
                                     .buildOrLog();

        if (!btnContainer) {
            LOG_WARN(Controls, "Failed to create button for icon {}", config.tooltip);
            buttons_.push_back(nullptr);
            continue;
        }

        // Get the inner button for event callback.
        lv_obj_t* btn = lv_obj_get_child(btnContainer, 0);
        if (btn) {
            // Store index in user data for callback.
            lv_obj_set_user_data(btn, this);
            lv_obj_add_event_cb(btn, onIconClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
        }

        buttons_.push_back(btnContainer);
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

    LOG_DEBUG(
        Controls,
        "Icon clicked: {} (was: {})",
        static_cast<int>(clickedId),
        static_cast<int>(previousId));

    // Toggle behavior: clicking selected icon deselects it.
    if (clickedId == self->selectedId_) {
        self->selectedId_ = IconId::COUNT;
    }
    else {
        self->selectedId_ = clickedId;
    }

    self->updateButtonVisuals();
    self->resetAutoShrinkTimer();

    // Queue event for state machine to process.
    if (self->eventSink_) {
        self->eventSink_->queueEvent(IconSelectedEvent{ self->selectedId_, previousId });
    }
}

void IconRail::updateButtonVisuals()
{
    const bool hasSelection = (selectedId_ != IconId::COUNT);

    for (size_t i = 0; i < buttons_.size() && i < iconConfigs_.size(); i++) {
        lv_obj_t* btnContainer = buttons_[i];
        if (!btnContainer) continue;

        bool isSelected = (iconConfigs_[i].id == selectedId_);
        LVGLBuilder::ActionButtonBuilder::setChecked(btnContainer, isSelected);

        lv_obj_t* innerButton = lv_obj_get_child(btnContainer, 0);
        if (!innerButton) continue;

        lv_opa_t targetOpa = LV_OPA_COVER;
        if (hasSelection && !isSelected) {
            targetOpa = DIMMED_ICON_OPA;
        }

        lv_obj_set_style_opa(innerButton, targetOpa, 0);
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

                    if (eventSink_) {
                        eventSink_->queueEvent(IconSelectedEvent{ selectedId_, previousId });
                    }
                }
            }
            break;
        }
    }

    LOG_DEBUG(Controls, "Tree icon visibility: {}", visible);
}

void IconRail::setVisibleIcons(const std::vector<IconId>& visibleIcons)
{
    // Store the allowed icons so applyMode() knows which icons can be shown.
    allowedIcons_ = visibleIcons;

    for (size_t i = 0; i < iconConfigs_.size() && i < buttons_.size(); i++) {
        if (!buttons_[i]) continue;

        const IconId id = iconConfigs_[i].id;
        const bool shouldBeVisible =
            std::find(visibleIcons.begin(), visibleIcons.end(), id) != visibleIcons.end();

        if (shouldBeVisible) {
            lv_obj_clear_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);

            if (selectedId_ == id) {
                const IconId previousId = selectedId_;
                selectedId_ = IconId::COUNT;
                updateButtonVisuals();
                if (eventSink_) {
                    eventSink_->queueEvent(IconSelectedEvent{ selectedId_, previousId });
                }
            }
        }
    }

    resetAutoShrinkTimer();
}

void IconRail::showIcons()
{
    setMode(RailMode::Normal);
    if (!allowedIcons_.empty()) {
        setVisibleIcons(allowedIcons_);
    }
}

void IconRail::selectIcon(IconId id)
{
    if (id == selectedId_) return;

    IconId previousId = selectedId_;
    selectedId_ = id;
    updateButtonVisuals();
    resetAutoShrinkTimer();

    if (eventSink_) {
        eventSink_->queueEvent(IconSelectedEvent{ selectedId_, previousId });
    }
}

bool IconRail::isIconSelectable(IconId id) const
{
    if (id == IconId::COUNT) {
        return false;
    }

    for (size_t i = 0; i < iconConfigs_.size() && i < buttons_.size(); i++) {
        if (iconConfigs_[i].id != id) {
            continue;
        }

        lv_obj_t* button = buttons_[i];
        if (!button) {
            return false;
        }

        return !lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN);
    }

    return false;
}

void IconRail::deselectAll()
{
    if (selectedId_ == IconId::COUNT) return;

    IconId previousId = selectedId_;
    selectedId_ = IconId::COUNT;
    updateButtonVisuals();
    resetAutoShrinkTimer();

    if (eventSink_) {
        eventSink_->queueEvent(IconSelectedEvent{ selectedId_, previousId });
    }
}

void IconRail::createModeButtons()
{
    if (!container_) return;

    // Get the screen (go up from container -> simMainRow -> screen).
    // This allows the expand button to float on top of everything.
    lv_obj_t* parent = lv_obj_get_parent(container_);
    lv_obj_t* screen = parent ? lv_obj_get_parent(parent) : nullptr;
    if (!screen) {
        screen = container_; // Fallback to container if we can't find screen.
    }

    // Create expand button as an OVERLAY on the screen.
    // This lets it extend past the rail and float on top of everything.
    expandButton_ = LVGLBuilder::actionButton(screen)
                        .icon(LV_SYMBOL_RIGHT)
                        .mode(LVGLBuilder::ActionMode::Push)
                        .size(LVGLBuilder::Style::ACTION_SIZE)
                        .glowColor(0x808080)
                        .textColor(0xFFFFFF)
                        .buildOrLog();

    if (expandButton_) {
        const int expandWidth = LVGLBuilder::Style::ACTION_SIZE;
        const int expandHeight = LVGLBuilder::Style::ACTION_SIZE * 2;
        lv_obj_set_size(expandButton_, expandWidth, expandHeight);

        // Remove from any layout - position absolutely.
        lv_obj_add_flag(expandButton_, LV_OBJ_FLAG_FLOATING);

        // Position: left edge, vertically centered.
        lv_obj_align(expandButton_, LV_ALIGN_LEFT_MID, 0, 0);

        // Inner button fills the trough.
        lv_obj_t* innerBtn = lv_obj_get_child(expandButton_, 0);
        if (innerBtn) {
            const int padding = LVGLBuilder::Style::TROUGH_PADDING;
            lv_obj_set_size(innerBtn, expandWidth - padding * 2, expandHeight - padding * 2);
            lv_obj_set_user_data(innerBtn, this);
            lv_obj_add_event_cb(
                innerBtn, onModeButtonClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(1));
        }

        // Start hidden (normal mode is default).
        lv_obj_add_flag(expandButton_, LV_OBJ_FLAG_HIDDEN);
    }

    // Create collapse button (shown in normal mode, at bottom).
    collapseButton_ = LVGLBuilder::actionButton(container_)
                          .icon(LV_SYMBOL_LEFT)
                          .mode(LVGLBuilder::ActionMode::Push)
                          .size(ICON_SIZE)
                          .glowColor(0x808080)
                          .textColor(0xFFFFFF)
                          .buildOrLog();

    if (collapseButton_) {
        lv_obj_t* btn = lv_obj_get_child(collapseButton_, 0);
        if (btn) {
            lv_obj_set_user_data(btn, this);
            lv_obj_add_event_cb(
                btn, onModeButtonClicked, LV_EVENT_CLICKED, reinterpret_cast<void*>(0));
        }
    }

    LOG_DEBUG(Controls, "Created mode buttons (expand/collapse)");
}

void IconRail::applyMode()
{
    if (!container_) return;

    bool minimized = (mode_ == RailMode::Minimized);

    // Animate width transition.
    int targetWidth = minimized ? MINIMIZED_RAIL_WIDTH : RAIL_WIDTH;
    int currentWidth = lv_obj_get_width(container_);
    if (currentWidth != targetWidth) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, container_);
        lv_anim_set_values(&a, currentWidth, targetWidth);
        lv_anim_set_time(&a, MODE_ANIM_DURATION_MS);
        lv_anim_set_exec_cb(
            &a, [](void* obj, int32_t v) { lv_obj_set_width(static_cast<lv_obj_t*>(obj), v); });
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }

    // Update styles immediately.
    if (minimized) {
        lv_obj_set_style_pad_all(container_, 2, 0);
        lv_obj_set_flex_align(
            container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    else {
        lv_obj_set_style_pad_all(container_, (RAIL_WIDTH - ICON_SIZE) / 2, 0);
        lv_obj_set_flex_align(
            container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    // Show/hide icon buttons.
    for (size_t i = 0; i < buttons_.size(); i++) {
        if (buttons_[i]) {
            if (minimized) {
                lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
            }
            else {
                const IconId id = iconConfigs_[i].id;

                // Check if this icon is in the allowed set (if set was configured).
                const bool isAllowed = allowedIcons_.empty()
                    || std::find(allowedIcons_.begin(), allowedIcons_.end(), id)
                        != allowedIcons_.end();

                // Also respect tree visibility setting.
                const bool showIcon = isAllowed && (id != IconId::TREE || treeIconVisible_);

                if (showIcon) {
                    lv_obj_clear_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
                }
                else {
                    lv_obj_add_flag(buttons_[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // Show/hide mode buttons.
    if (expandButton_) {
        if (minimized) {
            lv_obj_clear_flag(expandButton_, LV_OBJ_FLAG_HIDDEN);

            lv_obj_t* innerBtn = lv_obj_get_child(expandButton_, 0);
            if (innerBtn) {
                lv_anim_del(expandButton_, nullptr);
                lv_obj_set_style_opa(expandButton_, LV_OPA_COVER, 0);
                lv_anim_t opaAnim;
                lv_anim_init(&opaAnim);
                lv_anim_set_var(&opaAnim, expandButton_);
                lv_anim_set_values(&opaAnim, LV_OPA_COVER, LV_OPA_30);
                lv_anim_set_time(&opaAnim, 500);
                lv_anim_set_exec_cb(&opaAnim, [](void* obj, int32_t v) {
                    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
                });
                lv_anim_set_path_cb(&opaAnim, lv_anim_path_ease_out);
                lv_anim_start(&opaAnim);

                lv_obj_t* iconLabel = lv_obj_get_child(innerBtn, 0);
                if (iconLabel) {
                    lv_obj_set_style_text_opa(iconLabel, LV_OPA_70, 0);
                    lv_anim_t textOpaAnim;
                    lv_anim_init(&textOpaAnim);
                    lv_anim_set_var(&textOpaAnim, iconLabel);
                    lv_anim_set_values(&textOpaAnim, LV_OPA_70, LV_OPA_COVER);
                    lv_anim_set_time(&textOpaAnim, 500);
                    lv_anim_set_exec_cb(&textOpaAnim, [](void* obj, int32_t v) {
                        lv_obj_set_style_text_opa(
                            static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
                    });
                    lv_anim_set_path_cb(&textOpaAnim, lv_anim_path_ease_out);
                    lv_anim_start(&textOpaAnim);
                }
            }
        }
        else {
            lv_obj_add_flag(expandButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (collapseButton_) {
        if (minimized) {
            lv_obj_add_flag(collapseButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(collapseButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    LOG_INFO(Controls, "IconRail mode set to: {}", minimized ? "Minimized" : "Normal");

    resetAutoShrinkTimer();
}

void IconRail::setMode(RailMode mode)
{
    if (mode_ == mode) return;
    mode_ = mode;

    // When minimizing, deselect any selected icon to close the expandable panel.
    if (mode == RailMode::Minimized && selectedId_ != IconId::COUNT) {
        deselectAll();
    }

    applyMode();

    // Queue mode change event for state machine to process.
    if (eventSink_) {
        eventSink_->queueEvent(RailModeChangedEvent{ mode_ });
    }
}

void IconRail::toggleMode()
{
    setMode(mode_ == RailMode::Normal ? RailMode::Minimized : RailMode::Normal);
}

void IconRail::onModeButtonClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    IconRail* self = static_cast<IconRail*>(lv_obj_get_user_data(btn));
    size_t isExpand = reinterpret_cast<size_t>(lv_event_get_user_data(e));

    if (!self) return;

    self->resetAutoShrinkTimer();

    if (isExpand) {
        self->setMode(RailMode::Normal);
    }
    else {
        self->setMode(RailMode::Minimized);
    }
}

void IconRail::createAutoShrinkTimer()
{
    autoShrinkTimer_ = lv_timer_create(onAutoShrinkTimer, AUTO_SHRINK_TIMEOUT_MS, this);
    if (autoShrinkTimer_) {
        // Pause initially - only active when rail is expanded with no selection.
        lv_timer_pause(autoShrinkTimer_);
        LOG_DEBUG(Controls, "Auto-shrink timer created ({}ms)", AUTO_SHRINK_TIMEOUT_MS);
    }
}

void IconRail::resetAutoShrinkTimer()
{
    if (!autoShrinkTimer_) return;

    // Only run timer when rail is expanded and no icon is selected.
    if (mode_ == RailMode::Normal && selectedId_ == IconId::COUNT) {
        lv_timer_reset(autoShrinkTimer_);
        lv_timer_resume(autoShrinkTimer_);
    }
    else {
        lv_timer_pause(autoShrinkTimer_);
    }
}

void IconRail::onAutoShrinkTimer(lv_timer_t* timer)
{
    IconRail* self = static_cast<IconRail*>(lv_timer_get_user_data(timer));
    if (!self) return;

    // Only request shrink if no icon is selected and currently expanded.
    if (self->selectedId_ == IconId::COUNT && self->mode_ == RailMode::Normal) {
        LOG_INFO(Controls, "Auto-shrink timer fired, queueing event");
        // Queue event for state machine to handle (don't touch LVGL objects from timer).
        if (self->eventSink_) {
            self->eventSink_->queueEvent(RailAutoShrinkRequestEvent{});
        }
    }

    lv_timer_pause(timer);
}

} // namespace Ui
} // namespace DirtSim
