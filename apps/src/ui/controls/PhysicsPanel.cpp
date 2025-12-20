#include "PhysicsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {

PhysicsPanel::PhysicsPanel(lv_obj_t* container, Network::WebSocketService* wsService)
    : container_(container), wsService_(wsService), settings_(getDefaultPhysicsSettings())
{
    // Cache all section configs upfront.
    configs_ = PhysicsControlHelpers::createAllColumnConfigs();

    // Create the menu container (visible by default).
    menuContainer_ = lv_obj_create(container_);
    lv_obj_set_size(menuContainer_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(menuContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        menuContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(menuContainer_, 0, 0);
    lv_obj_set_style_pad_row(menuContainer_, 4, 0);
    lv_obj_set_style_bg_opa(menuContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menuContainer_, 0, 0);

    // Create the section container (hidden by default).
    sectionContainer_ = lv_obj_create(container_);
    lv_obj_set_size(sectionContainer_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sectionContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        sectionContainer_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(sectionContainer_, 0, 0);
    lv_obj_set_style_pad_row(sectionContainer_, 4, 0);
    lv_obj_set_style_bg_opa(sectionContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sectionContainer_, 0, 0);
    lv_obj_add_flag(sectionContainer_, LV_OBJ_FLAG_HIDDEN);

    // Build the menu view.
    createMenuView();

    // Fetch initial settings from server.
    fetchSettings();

    LOG_INFO(Controls, "PhysicsPanel: Initialized with modal navigation (6 sections)");
}

PhysicsPanel::~PhysicsPanel()
{
    LOG_INFO(Controls, "PhysicsPanel: Destroyed");
}

void PhysicsPanel::createMenuView()
{
    // Section names matching the order in getSectionConfig().
    const char* sectionNames[] = { "General", "Pressure", "Forces", "Swap Tuning", "Swap2", "Frag" };

    for (int i = 0; i < 6; i++) {
        lv_obj_t* btn = LVGLBuilder::button(menuContainer_)
                            .text(sectionNames[i])
                            .size(LV_PCT(95), LVGLBuilder::Style::CONTROL_HEIGHT)
                            .backgroundColor(LVGLBuilder::Style::BUTTON_BG_COLOR)
                            .pressedColor(LVGLBuilder::Style::BUTTON_PRESSED_COLOR)
                            .textColor(LVGLBuilder::Style::BUTTON_TEXT_COLOR)
                            .radius(LVGLBuilder::Style::RADIUS)
                            .buildOrLog();

        if (btn) {
            // Store section index in user data.
            lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
            lv_obj_add_event_cb(btn, onSectionClicked, LV_EVENT_CLICKED, this);

            // Add a right arrow indicator.
            lv_obj_t* arrow = lv_label_create(btn);
            lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(arrow, lv_color_hex(0xAAAAAA), 0);
            lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
        }
    }
}

void PhysicsPanel::showSection(int sectionIndex)
{
    if (sectionIndex < 0 || sectionIndex > 5) {
        LOG_ERROR(Controls, "PhysicsPanel: Invalid section index {}", sectionIndex);
        return;
    }

    // Hide menu, show section container.
    lv_obj_add_flag(menuContainer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sectionContainer_, LV_OBJ_FLAG_HIDDEN);

    // Clear any existing section content.
    lv_obj_clean(sectionContainer_);
    controls_.clear();
    widgetToControl_.clear();

    // Create back button header.
    lv_obj_t* backBtn = LVGLBuilder::button(sectionContainer_)
                            .text("Back")
                            .size(LV_PCT(95), LVGLBuilder::Style::CONTROL_HEIGHT)
                            .backgroundColor(LVGLBuilder::Style::BUTTON_BG_COLOR)
                            .pressedColor(LVGLBuilder::Style::BUTTON_PRESSED_COLOR)
                            .textColor(LVGLBuilder::Style::BUTTON_TEXT_COLOR)
                            .radius(LVGLBuilder::Style::RADIUS)
                            .icon(LV_SYMBOL_LEFT)
                            .buildOrLog();

    if (backBtn) {
        lv_obj_add_event_cb(backBtn, onBackClicked, LV_EVENT_CLICKED, this);
    }

    // Create section title.
    const auto& config = getSectionConfig(sectionIndex);
    lv_obj_t* titleLabel = lv_label_create(sectionContainer_);
    lv_label_set_text(titleLabel, config.title);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Create a container for the controls.
    lv_obj_t* controlsContainer = lv_obj_create(sectionContainer_);
    lv_obj_set_size(controlsContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(controlsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        controlsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(controlsContainer, LVGLBuilder::Style::GAP, 0);
    lv_obj_set_style_pad_left(controlsContainer, LVGLBuilder::Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_right(controlsContainer, LVGLBuilder::Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_top(controlsContainer, LVGLBuilder::Style::PAD_VERTICAL, 0);
    lv_obj_set_style_pad_bottom(controlsContainer, LVGLBuilder::Style::PAD_VERTICAL, 0);
    lv_obj_set_style_bg_opa(controlsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controlsContainer, 0, 0);

    // Resize controls vector for this section.
    controls_.resize(config.controls.size());

    // Create controls for this section.
    size_t added = PhysicsControlHelpers::createControlsFromColumn(controlsContainer,
                                                                    config,
                                                                    controls_.data(),
                                                                    0,
                                                                    widgetToControl_,
                                                                    onGenericToggle,
                                                                    onGenericValueChange,
                                                                    this);

    // Update state.
    activeSection_ = sectionIndex;
    currentView_ = ViewMode::SECTION;

    // Update controls from current settings.
    PhysicsControlHelpers::updateControlsFromSettings(controls_.data(), controls_.size(), settings_);

    LOG_INFO(
        Controls, "PhysicsPanel: Showing section '{}' with {} controls", config.title, added);
}

void PhysicsPanel::showMenu()
{
    // Clear section content.
    lv_obj_clean(sectionContainer_);
    controls_.clear();
    widgetToControl_.clear();

    // Hide section, show menu.
    lv_obj_add_flag(sectionContainer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(menuContainer_, LV_OBJ_FLAG_HIDDEN);

    // Update state.
    activeSection_ = -1;
    currentView_ = ViewMode::MENU;

    LOG_INFO(Controls, "PhysicsPanel: Returned to menu view");
}

const PhysicsControlHelpers::ColumnConfig& PhysicsPanel::getSectionConfig(int index) const
{
    switch (index) {
    case 0:
        return configs_.generalPhysics;
    case 1:
        return configs_.pressure;
    case 2:
        return configs_.forces;
    case 3:
        return configs_.swapTuning;
    case 4:
        return configs_.swap2;
    case 5:
    default:
        return configs_.frag;
    }
}

void PhysicsPanel::updateFromSettings(const PhysicsSettings& settings)
{
    LOG_DEBUG(Controls, "PhysicsPanel: Updating from server settings");
    settings_ = settings;

    // Only update controls if we're in section view.
    if (currentView_ == ViewMode::SECTION && !controls_.empty()) {
        PhysicsControlHelpers::updateControlsFromSettings(
            controls_.data(), controls_.size(), settings_);
    }
}

void PhysicsPanel::onSectionClicked(lv_event_t* e)
{
    PhysicsPanel* self = static_cast<PhysicsPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    intptr_t sectionIndex = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));

    self->showSection(static_cast<int>(sectionIndex));
}

void PhysicsPanel::onBackClicked(lv_event_t* e)
{
    PhysicsPanel* self = static_cast<PhysicsPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    self->showMenu();
}

void PhysicsPanel::onGenericToggle(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    PhysicsPanel* self = static_cast<PhysicsPanel*>(lv_obj_get_user_data(target));
    if (!self) {
        self = static_cast<PhysicsPanel*>(lv_event_get_user_data(e));
    }

    if (!self) {
        LOG_WARN(Controls, "PhysicsPanel::onGenericToggle - self is null");
        return;
    }

    auto* control = self->findControl(target);
    if (!control) {
        LOG_WARN(Controls, "PhysicsPanel: Could not find control for toggle event");
        return;
    }

    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    const char* label = control->config.label ? control->config.label : "Unknown";
    LOG_INFO(Controls, "PhysicsPanel: {} toggled to {}", label, enabled ? "ON" : "OFF");

    if (control->config.enableSetter) {
        try {
            control->config.enableSetter(self->settings_, enabled);
        }
        catch (const std::exception& ex) {
            LOG_ERROR(Controls, "PhysicsPanel: Exception in enableSetter for {}: {}", label, ex.what());
            return;
        }
    }

    if (enabled && control->config.type == PhysicsControlHelpers::ControlType::TOGGLE_SLIDER) {
        if (control->sliderWidget) {
            int value = lv_slider_get_value(control->sliderWidget);
            double scaledValue = value * control->config.valueScale;
            if (control->config.valueSetter) {
                control->config.valueSetter(self->settings_, scaledValue);
            }
            LOG_DEBUG(Controls, "PhysicsPanel: Restored {} to {:.2f}", control->config.label, scaledValue);
        }
        else {
            LOG_WARN(Controls, "PhysicsPanel: No slider widget found for {}", control->config.label);
        }
    }

    try {
        self->syncSettings();
    }
    catch (const std::exception& ex) {
        LOG_ERROR(Controls, "PhysicsPanel: Exception in syncSettings: {}", ex.what());
    }
}

void PhysicsPanel::onGenericValueChange(lv_event_t* e)
{
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        return;
    }
    else if (code != LV_EVENT_RELEASED) {
        return;
    }

    PhysicsPanel* self = static_cast<PhysicsPanel*>(lv_obj_get_user_data(target));
    if (!self) {
        self = static_cast<PhysicsPanel*>(lv_event_get_user_data(e));
    }

    if (!self) {
        LOG_WARN(Controls, "PhysicsPanel::onGenericValueChange - self is null");
        return;
    }

    auto* control = self->findControl(target);
    if (!control) {
        LOG_WARN(Controls, "PhysicsPanel: Could not find control for value change event");
        return;
    }

    int value = lv_slider_get_value(target);
    double scaledValue = value * control->config.valueScale;

    LOG_INFO(Controls, "PhysicsPanel: {} released at {:.2f}", control->config.label, scaledValue);

    if (control->config.valueSetter) {
        control->config.valueSetter(self->settings_, scaledValue);
    }

    self->syncSettings();
}

PhysicsControlHelpers::Control* PhysicsPanel::findControl(lv_obj_t* widget)
{
    return PhysicsControlHelpers::findControl(widget, widgetToControl_);
}

void PhysicsPanel::fetchSettings()
{
    settings_ = PhysicsControlHelpers::fetchSettingsFromServer(wsService_);

    // Update controls if in section view.
    if (currentView_ == ViewMode::SECTION && !controls_.empty()) {
        PhysicsControlHelpers::updateControlsFromSettings(
            controls_.data(), controls_.size(), settings_);
    }
}

void PhysicsPanel::syncSettings()
{
    PhysicsControlHelpers::syncSettingsToServer(wsService_, settings_);
}

} // namespace Ui
} // namespace DirtSim
