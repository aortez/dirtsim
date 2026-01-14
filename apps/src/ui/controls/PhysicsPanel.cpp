#include "PhysicsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {

PhysicsPanel::PhysicsPanel(lv_obj_t* container, Network::WebSocketServiceInterface* wsService)
    : container_(container), wsService_(wsService), settings_(getDefaultPhysicsSettings())
{
    // Cache all section configs upfront.
    configs_ = PhysicsControlHelpers::createAllColumnConfigs();

    // Create view controller.
    viewController_ = std::make_unique<PanelViewController>(container_);

    // Create menu view.
    lv_obj_t* menuView = viewController_->createView("menu");
    createMenuView(menuView);

    // Show menu view initially.
    viewController_->showView("menu");

    // Fetch initial settings from server.
    fetchSettings();

    LOG_INFO(Controls, "PhysicsPanel: Initialized with PanelViewController (7 sections)");
}

PhysicsPanel::~PhysicsPanel()
{
    LOG_INFO(Controls, "PhysicsPanel: Destroyed");
}

void PhysicsPanel::createMenuView(lv_obj_t* view)
{
    // Section names matching the order in getSectionConfig().
    const char* sectionNames[] = { "General",     "Pressure", "Forces", "Light",
                                   "Swap Tuning", "Swap2",    "Frag" };

    // Clear button mapping.
    buttonToSection_.clear();

    for (int i = 0; i < 7; i++) {
        lv_obj_t* container = LVGLBuilder::actionButton(view)
                                  .text(sectionNames[i])
                                  .icon(LV_SYMBOL_RIGHT)
                                  .width(LV_PCT(95))
                                  .height(LVGLBuilder::Style::ACTION_SIZE)
                                  .layoutRow()
                                  .alignLeft()
                                  .buildOrLog();

        if (container) {
            // Get the inner button (first child of container).
            lv_obj_t* button = lv_obj_get_child(container, 0);
            if (button) {
                // Store button->section mapping (don't touch ActionButton's user_data!).
                buttonToSection_[button] = i;
                lv_obj_add_event_cb(button, onSectionClicked, LV_EVENT_CLICKED, this);
            }
        }
    }
}

void PhysicsPanel::showSection(int sectionIndex)
{
    if (sectionIndex < 0 || sectionIndex > 6) {
        LOG_ERROR(Controls, "PhysicsPanel: Invalid section index {}", sectionIndex);
        return;
    }

    // Create section view if it doesn't exist.
    if (!viewController_->hasView("section")) {
        viewController_->createView("section");
    }

    // Get section view and clear it.
    lv_obj_t* sectionView = viewController_->getView("section");
    lv_obj_clean(sectionView);
    controls_.clear();
    widgetToControl_.clear();

    // Create section content.
    createSectionView(sectionView, sectionIndex);

    // Update state and show view.
    activeSection_ = sectionIndex;
    viewController_->showView("section");

    const auto& config = getSectionConfig(sectionIndex);
    LOG_INFO(
        Controls,
        "PhysicsPanel: Showing section '{}' with {} controls",
        config.title,
        controls_.size());
}

void PhysicsPanel::createSectionView(lv_obj_t* view, int sectionIndex)
{
    // Create back button header.
    LVGLBuilder::actionButton(view)
        .text("Back")
        .icon(LV_SYMBOL_LEFT)
        .width(LV_PCT(95))
        .height(LVGLBuilder::Style::ACTION_SIZE)
        .layoutRow()
        .alignLeft()
        .callback(onBackClicked, this)
        .buildOrLog();

    // Create section title.
    const auto& config = getSectionConfig(sectionIndex);
    lv_obj_t* titleLabel = lv_label_create(view);
    lv_label_set_text(titleLabel, config.title);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(titleLabel, 8, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);

    // Create a container for the controls.
    lv_obj_t* controlsContainer = lv_obj_create(view);
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
    PhysicsControlHelpers::createControlsFromColumn(
        controlsContainer,
        config,
        controls_.data(),
        0,
        widgetToControl_,
        onGenericToggle,
        onGenericValueChange,
        this);

    // Update controls from current settings.
    PhysicsControlHelpers::updateControlsFromSettings(
        controls_.data(), controls_.size(), settings_);
}

void PhysicsPanel::showMenu()
{
    // Clear section content.
    controls_.clear();
    widgetToControl_.clear();

    // Update state and show menu view.
    activeSection_ = -1;
    viewController_->showView("menu");

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
            return configs_.light;
        case 4:
            return configs_.swapTuning;
        case 5:
            return configs_.swap2;
        case 6:
        default:
            return configs_.frag;
    }
}

void PhysicsPanel::updateFromSettings(const PhysicsSettings& settings)
{
    LOG_DEBUG(Controls, "PhysicsPanel: Updating from server settings");
    settings_ = settings;

    // Only update controls if we're in section view.
    if (activeSection_ >= 0 && !controls_.empty()) {
        PhysicsControlHelpers::updateControlsFromSettings(
            controls_.data(), controls_.size(), settings_);
    }
}

void PhysicsPanel::onSectionClicked(lv_event_t* e)
{
    PhysicsPanel* self = static_cast<PhysicsPanel*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));

    // Look up section index from button mapping.
    auto it = self->buttonToSection_.find(btn);
    if (it == self->buttonToSection_.end()) {
        LOG_ERROR(Controls, "PhysicsPanel: Unknown button clicked");
        return;
    }

    self->showSection(it->second);
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
            LOG_ERROR(
                Controls, "PhysicsPanel: Exception in enableSetter for {}: {}", label, ex.what());
            return;
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

    // Handle ACTION_STEPPER controls on VALUE_CHANGED.
    if (control->config.type == PhysicsControlHelpers::ControlType::ACTION_STEPPER) {
        if (code != LV_EVENT_VALUE_CHANGED) {
            return;
        }
        int32_t stepperValue = LVGLBuilder::ActionStepperBuilder::getValue(control->stepperWidget);
        double scaledValue = stepperValue * control->config.valueScale;
        LOG_INFO(
            Controls, "PhysicsPanel: {} changed to {:.2f}", control->config.label, scaledValue);

        if (control->config.valueSetter) {
            control->config.valueSetter(self->settings_, scaledValue);
        }
        self->syncSettings();
        return;
    }

    // Handle DROPDOWN controls on VALUE_CHANGED.
    if (control->config.type == PhysicsControlHelpers::ControlType::DROPDOWN) {
        if (code != LV_EVENT_VALUE_CHANGED) {
            return;
        }
        int selectedIndex = static_cast<int>(lv_dropdown_get_selected(target));
        LOG_INFO(
            Controls, "PhysicsPanel: {} changed to index {}", control->config.label, selectedIndex);

        if (control->config.indexSetter) {
            control->config.indexSetter(self->settings_, selectedIndex);
        }
        self->syncSettings();
        return;
    }
}

PhysicsControlHelpers::Control* PhysicsPanel::findControl(lv_obj_t* widget)
{
    return PhysicsControlHelpers::findControl(widget, widgetToControl_);
}

void PhysicsPanel::fetchSettings()
{
    settings_ = PhysicsControlHelpers::fetchSettingsFromServer(wsService_);

    // Update controls if in section view.
    if (activeSection_ >= 0 && !controls_.empty()) {
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
