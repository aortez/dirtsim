#include "IconRail.h"
#include "core/Assert.h"
#include "core/IconFont.h"
#include "core/LoggingChannels.h"
#include "ui/state-machine/Event.h"
#include "ui/state-machine/EventSink.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

IconRail::IconRail(lv_obj_t* parent, EventSink* eventSink) : eventSink_(eventSink)
{
    // Load FontAwesome at icon size (slightly smaller than button for padding).
    iconFont_ = std::make_unique<IconFont>(ICON_SIZE - 24);

    // Define our icon configuration with FontAwesome icons and per-icon colors.
    // Order determines display order in the rail.
    iconConfigs_ = {
        { IconId::CORE, IconFont::HOME, "Core Controls", 0x87CEEB },        // Light blue.
        { IconId::EVOLUTION, IconFont::CHART_LINE, "Evolution", 0xDA70D6 }, // Orchid/purple.
        { IconId::NETWORK, IconFont::WIFI, "Network", 0x00CED1 },           // Dark turquoise.
        { IconId::PHYSICS, IconFont::COG, "Physics", 0xC0C0C0 },            // Silver.
        { IconId::PLAY, IconFont::PLAY, "Play Simulation", 0x90EE90 },      // Light green.
        { IconId::SCENARIO, IconFont::FILM, "Scenario", 0xFFA500 },         // Orange.
        { IconId::TREE, IconFont::BRAIN, "Tree Vision", 0x32CD32 },         // Lime green.
    };

    createIcons(parent);
    createModeButtons();
    createAutoShrinkTimer();

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
    if (swipeZone_) {
        lv_obj_delete(swipeZone_);
        swipeZone_ = nullptr;
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
    lv_obj_set_style_bg_color(container_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Add gesture detection for swipe-to-expand.
    lv_obj_set_user_data(container_, this);
    lv_obj_add_event_cb(container_, onGesture, LV_EVENT_GESTURE, nullptr);

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
    for (size_t i = 0; i < buttons_.size() && i < iconConfigs_.size(); i++) {
        lv_obj_t* btnContainer = buttons_[i];
        if (!btnContainer) continue;

        bool isSelected = (iconConfigs_[i].id == selectedId_);
        LVGLBuilder::ActionButtonBuilder::setChecked(btnContainer, isSelected);
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
}

void IconRail::selectIcon(IconId id)
{
    if (id == selectedId_) return;

    IconId previousId = selectedId_;
    selectedId_ = id;
    updateButtonVisuals();

    if (eventSink_) {
        eventSink_->queueEvent(IconSelectedEvent{ selectedId_, previousId });
    }
}

void IconRail::deselectAll()
{
    if (selectedId_ == IconId::COUNT) return;

    IconId previousId = selectedId_;
    selectedId_ = IconId::COUNT;
    updateButtonVisuals();

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
                        .size(MINIMIZED_RAIL_WIDTH - 4)
                        .glowColor(0x808080)
                        .textColor(0xFFFFFF)
                        .buildOrLog();

    if (expandButton_) {
        // Size: extends past the rail edge for visibility.
        const int extensionAmount = 24;
        const int expandWidth = MINIMIZED_RAIL_WIDTH + extensionAmount;
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

    // Create invisible swipe zone on right half of screen for gesture detection.
    // Placed on right side to avoid blocking expand button and other controls.
    if (screen) {
        swipeZone_ = lv_obj_create(screen);
        if (swipeZone_) {
            // Right half of screen, full height.
            lv_obj_set_size(swipeZone_, LV_PCT(50), LV_PCT(100));
            lv_obj_add_flag(swipeZone_, LV_OBJ_FLAG_FLOATING);
            lv_obj_align(swipeZone_, LV_ALIGN_RIGHT_MID, 0, 0);

            // Make it invisible but still receive touch events.
            lv_obj_set_style_bg_opa(swipeZone_, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(swipeZone_, 0, 0);
            lv_obj_clear_flag(swipeZone_, LV_OBJ_FLAG_SCROLLABLE);

            // Add gesture detection.
            lv_obj_set_user_data(swipeZone_, this);
            lv_obj_add_event_cb(swipeZone_, onGesture, LV_EVENT_GESTURE, nullptr);

            // Start hidden (normal mode is default).
            lv_obj_add_flag(swipeZone_, LV_OBJ_FLAG_HIDDEN);

            LOG_DEBUG(Controls, "Created swipe zone on right half of screen");
        }
    }

    LOG_DEBUG(Controls, "Created mode buttons (expand/collapse)");
}

void IconRail::applyMode()
{
    if (!container_) return;

    bool minimized = (mode_ == RailMode::Minimized);

    // Resize container and adjust layout.
    if (minimized) {
        lv_obj_set_width(container_, MINIMIZED_RAIL_WIDTH);
        lv_obj_set_style_pad_all(container_, 2, 0); // Small padding around button.
        // Center the expand button vertically.
        lv_obj_set_flex_align(
            container_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }
    else {
        lv_obj_set_width(container_, RAIL_WIDTH);
        lv_obj_set_style_pad_all(container_, (RAIL_WIDTH - ICON_SIZE) / 2, 0);
        // Icons start at top.
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

    // Show/hide mode buttons and swipe zone.
    if (expandButton_) {
        if (minimized) {
            lv_obj_clear_flag(expandButton_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(expandButton_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (swipeZone_) {
        if (minimized) {
            lv_obj_clear_flag(swipeZone_, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(swipeZone_, LV_OBJ_FLAG_HIDDEN);
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

void IconRail::onGesture(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    IconRail* self = static_cast<IconRail*>(lv_obj_get_user_data(obj));
    if (!self) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    // Log all gesture events for debugging.
    const char* dirName = (dir == LV_DIR_LEFT) ? "LEFT"
        : (dir == LV_DIR_RIGHT)                ? "RIGHT"
        : (dir == LV_DIR_TOP)                  ? "UP"
        : (dir == LV_DIR_BOTTOM)               ? "DOWN"
                                               : "NONE";
    LOG_INFO(
        Controls,
        "Gesture detected: {} (mode={})",
        dirName,
        self->mode_ == RailMode::Minimized ? "minimized" : "normal");

    // Swipe right to expand when minimized.
    if (dir == LV_DIR_RIGHT && self->mode_ == RailMode::Minimized) {
        LOG_INFO(Controls, "Swipe right detected - expanding IconRail");
        self->setMode(RailMode::Normal);
    }
    // Swipe left to minimize when expanded.
    else if (dir == LV_DIR_LEFT && self->mode_ == RailMode::Normal) {
        LOG_INFO(Controls, "Swipe left detected - minimizing IconRail");
        self->setMode(RailMode::Minimized);
    }
}

void IconRail::setSwipeZoneEnabled(bool enabled)
{
    if (enabled) {
        DIRTSIM_ASSERT(swipeZone_, "Cannot enable swipe zone - not created");
        lv_obj_clear_flag(swipeZone_, LV_OBJ_FLAG_HIDDEN);
    }
    else if (swipeZone_) {
        lv_obj_add_flag(swipeZone_, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace Ui
} // namespace DirtSim
