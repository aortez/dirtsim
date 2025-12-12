#include "PhysicsPanel.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "ui/ui_builders/LVGLBuilder.h"

namespace DirtSim {
namespace Ui {

PhysicsPanel::PhysicsPanel(lv_obj_t* container, Network::WebSocketService* wsService)
    : container_(container), wsService_(wsService), settings_(getDefaultPhysicsSettings())
{
    auto configs = PhysicsControlHelpers::createAllColumnConfigs();

    struct SectionConfig {
        const char* title;
        PhysicsControlHelpers::ColumnConfig config;
        bool initiallyExpanded;
    };

    std::vector<SectionConfig> sections = {
        { "General", configs.generalPhysics, true },
        { "Pressure", configs.pressure, false },
        { "Forces", configs.forces, false },
        { "Swap Tuning", configs.swapTuning, false },
        { "Swap2", configs.swap2, false },
        { "Frag", configs.frag, false },
    };

    for (const auto& section : sections) {
        lv_obj_t* sectionContainer = createCollapsibleSection(
            container_, section.title, section.initiallyExpanded);

        if (sectionContainer) {
            size_t added = PhysicsControlHelpers::createControlsFromColumn(sectionContainer,
                                                                            section.config,
                                                                            controls_.data(),
                                                                            controlCount_,
                                                                            widgetToControl_,
                                                                            onGenericToggle,
                                                                            onGenericValueChange,
                                                                            this);
            controlCount_ += added;
        }
    }

    fetchSettings();

    LOG_INFO(Controls, "PhysicsPanel: Initialized with {} controls in 6 sections", controlCount_);
}

PhysicsPanel::~PhysicsPanel()
{
    LOG_INFO(Controls, "PhysicsPanel: Destroyed");
}

lv_obj_t* PhysicsPanel::createCollapsibleSection(lv_obj_t* parent,
                                                  const char* title,
                                                  bool initiallyExpanded)
{
    lv_obj_t* panel = LVGLBuilder::collapsiblePanel(parent)
                          .title(title)
                          .size(LV_PCT(100), LV_SIZE_CONTENT)
                          .initiallyExpanded(initiallyExpanded)
                          .backgroundColor(0x303030)
                          .headerColor(0x404040)
                          .buildOrLog();

    if (!panel) {
        LOG_ERROR(Controls, "PhysicsPanel: Failed to create collapsible section '{}'", title);
        return nullptr;
    }

    return lv_obj_get_child(panel, 1);
}

void PhysicsPanel::updateFromSettings(const PhysicsSettings& settings)
{
    LOG_INFO(Controls, "PhysicsPanel: Updating UI from server settings");
    settings_ = settings;
    PhysicsControlHelpers::updateControlsFromSettings(controls_.data(), controlCount_, settings_);
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
    updateFromSettings(settings_);
}

void PhysicsPanel::syncSettings()
{
    PhysicsControlHelpers::syncSettingsToServer(wsService_, settings_);
}

} // namespace Ui
} // namespace DirtSim
