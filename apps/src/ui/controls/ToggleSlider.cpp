#include "ToggleSlider.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

// --- Builder Implementation ---

ToggleSlider::Builder::Builder(lv_obj_t* parent) : parent_(parent)
{}

ToggleSlider::Builder& ToggleSlider::Builder::label(const char* text)
{
    labelText_ = text;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::range(int min, int max)
{
    rangeMin_ = min;
    rangeMax_ = max;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::value(int initialValue)
{
    initialValue_ = initialValue;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::defaultValue(int defValue)
{
    defaultValue_ = defValue;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::valueScale(double scale)
{
    valueScale_ = scale;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::valueFormat(const char* format)
{
    valueFormat_ = format;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::initiallyEnabled(bool enabled)
{
    initiallyEnabled_ = enabled;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::sliderWidth(int width)
{
    sliderWidth_ = width;
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::onToggle(ToggleCallback callback)
{
    toggleCallback_ = std::move(callback);
    return *this;
}

ToggleSlider::Builder& ToggleSlider::Builder::onValueChange(ValueCallback callback)
{
    valueCallback_ = std::move(callback);
    return *this;
}

std::unique_ptr<ToggleSlider> ToggleSlider::Builder::build()
{
    return std::unique_ptr<ToggleSlider>(new ToggleSlider(
        parent_,
        labelText_,
        rangeMin_,
        rangeMax_,
        initialValue_,
        defaultValue_,
        valueScale_,
        valueFormat_,
        initiallyEnabled_,
        sliderWidth_,
        std::move(toggleCallback_),
        std::move(valueCallback_)));
}

// --- ToggleSlider Implementation ---

ToggleSlider::Builder ToggleSlider::create(lv_obj_t* parent)
{
    return Builder(parent);
}

ToggleSlider::ToggleSlider(
    lv_obj_t* parent,
    const std::string& labelText,
    int rangeMin,
    int rangeMax,
    int initialValue,
    int defaultValue,
    double valueScale,
    const std::string& valueFormat,
    bool initiallyEnabled,
    int sliderWidth,
    ToggleCallback toggleCallback,
    ValueCallback valueCallback)
    : parent_(parent),
      labelText_(labelText),
      rangeMin_(rangeMin),
      rangeMax_(rangeMax),
      defaultValue_(defaultValue),
      valueScale_(valueScale),
      valueFormat_(valueFormat),
      sliderWidth_(sliderWidth),
      savedValue_(initialValue),
      toggleCallback_(std::move(toggleCallback)),
      valueCallback_(std::move(valueCallback))
{
    createWidgets();

    // Set initial state.
    if (initiallyEnabled) {
        lv_obj_add_state(switch_, LV_STATE_CHECKED);
        lv_slider_set_value(slider_, initialValue, LV_ANIM_OFF);
    }
    else {
        lv_slider_set_value(slider_, 0, LV_ANIM_OFF);
    }

    updateValueLabel();
    updateSliderColors();
}

ToggleSlider::~ToggleSlider()
{
    if (container_) {
        // Remove the user_data pointer before deleting to prevent dangling pointer access.
        lv_obj_set_user_data(container_, nullptr);
        lv_obj_del(container_);
        container_ = nullptr;
    }
}

ToggleSlider::ToggleSlider(ToggleSlider&& other) noexcept
    : parent_(other.parent_),
      container_(other.container_),
      label_(other.label_),
      valueLabel_(other.valueLabel_),
      switch_(other.switch_),
      slider_(other.slider_),
      labelText_(std::move(other.labelText_)),
      rangeMin_(other.rangeMin_),
      rangeMax_(other.rangeMax_),
      defaultValue_(other.defaultValue_),
      valueScale_(other.valueScale_),
      valueFormat_(std::move(other.valueFormat_)),
      sliderWidth_(other.sliderWidth_),
      savedValue_(other.savedValue_),
      toggleCallback_(std::move(other.toggleCallback_)),
      valueCallback_(std::move(other.valueCallback_))
{
    // Update user_data to point to this instance.
    if (container_) {
        lv_obj_set_user_data(container_, this);
    }

    // Null out the other's pointers to prevent double-delete.
    other.container_ = nullptr;
    other.label_ = nullptr;
    other.valueLabel_ = nullptr;
    other.switch_ = nullptr;
    other.slider_ = nullptr;
}

ToggleSlider& ToggleSlider::operator=(ToggleSlider&& other) noexcept
{
    if (this != &other) {
        // Clean up existing objects.
        if (container_) {
            lv_obj_set_user_data(container_, nullptr);
            lv_obj_del(container_);
        }

        // Move from other.
        parent_ = other.parent_;
        container_ = other.container_;
        label_ = other.label_;
        valueLabel_ = other.valueLabel_;
        switch_ = other.switch_;
        slider_ = other.slider_;
        labelText_ = std::move(other.labelText_);
        rangeMin_ = other.rangeMin_;
        rangeMax_ = other.rangeMax_;
        defaultValue_ = other.defaultValue_;
        valueScale_ = other.valueScale_;
        valueFormat_ = std::move(other.valueFormat_);
        sliderWidth_ = other.sliderWidth_;
        savedValue_ = other.savedValue_;
        toggleCallback_ = std::move(other.toggleCallback_);
        valueCallback_ = std::move(other.valueCallback_);

        // Update user_data to point to this instance.
        if (container_) {
            lv_obj_set_user_data(container_, this);
        }

        // Null out the other's pointers.
        other.container_ = nullptr;
        other.label_ = nullptr;
        other.valueLabel_ = nullptr;
        other.switch_ = nullptr;
        other.slider_ = nullptr;
    }
    return *this;
}

void ToggleSlider::createWidgets()
{
    using Style = LVGLBuilder::Style;

    // Create container for the whole control group.
    constexpr int containerHeight = Style::SWITCH_HEIGHT + Style::GAP + Style::SLIDER_KNOB_SIZE + 8;
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, Style::CONTROL_WIDTH, containerHeight);
    lv_obj_set_style_pad_all(container_, Style::GAP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, Style::RADIUS, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Blue background to match LabeledSwitch theme.
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    // Store pointer to this instance for callbacks.
    lv_obj_set_user_data(container_, this);

    // Create label (top left, vertically centered with switch).
    label_ = lv_label_create(container_);
    lv_label_set_text(label_, labelText_.c_str());
    lv_obj_align(label_, LV_ALIGN_TOP_LEFT, 0, (Style::SWITCH_HEIGHT - 16) / 2);
    lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label_, Style::CONTROL_FONT, 0);

    // Create switch (top right).
    switch_ = lv_switch_create(container_);
    lv_obj_align(switch_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(switch_, Style::SWITCH_WIDTH, Style::SWITCH_HEIGHT);
    lv_obj_add_event_cb(switch_, onSwitchChanged, LV_EVENT_VALUE_CHANGED, this);

    // Create slider (below label/switch).
    slider_ = lv_slider_create(container_);
    lv_obj_align(slider_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(slider_, LV_PCT(100), Style::SLIDER_TRACK_HEIGHT);
    lv_slider_set_range(slider_, rangeMin_, rangeMax_);

    // Style the slider knob for easy touch grabbing.
    lv_obj_set_style_pad_all(
        slider_, Style::SLIDER_KNOB_SIZE / 2 - Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_, Style::SLIDER_KNOB_RADIUS, LV_PART_KNOB);

    // Round the track ends.
    lv_obj_set_style_radius(slider_, Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_, Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_INDICATOR);

    lv_obj_add_event_cb(slider_, onSliderChanged, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(slider_, onSliderPressed, LV_EVENT_PRESSED, this);

    // Create value label (right of label).
    valueLabel_ = lv_label_create(container_);
    lv_obj_align_to(valueLabel_, label_, LV_ALIGN_OUT_RIGHT_MID, Style::GAP, 0);
    lv_obj_set_style_text_font(valueLabel_, Style::CONTROL_FONT, 0);
    lv_obj_set_style_text_color(valueLabel_, lv_color_hex(0xFFFFFF), 0);
}

void ToggleSlider::setValue(int value)
{
    // Clamp to range.
    if (value < rangeMin_) value = rangeMin_;
    if (value > rangeMax_) value = rangeMax_;

    // Auto-enable if disabled and setting a non-zero value.
    if (value > 0 && !isEnabled()) {
        setEnabled(true);
    }

    lv_slider_set_value(slider_, value, LV_ANIM_OFF);
    savedValue_ = value;
    updateValueLabel();
}

void ToggleSlider::setEnabled(bool enabled)
{
    bool currentlyEnabled = isEnabled();
    if (enabled == currentlyEnabled) return;

    if (enabled) {
        lv_obj_add_state(switch_, LV_STATE_CHECKED);

        // Restore saved value (or use default).
        int valueToRestore = (savedValue_ > 0) ? savedValue_ : defaultValue_;
        lv_slider_set_value(slider_, valueToRestore, LV_ANIM_OFF);
    }
    else {
        // Save current value before disabling.
        int currentValue = lv_slider_get_value(slider_);
        if (currentValue > 0) {
            savedValue_ = currentValue;
        }

        lv_obj_remove_state(switch_, LV_STATE_CHECKED);
        lv_slider_set_value(slider_, 0, LV_ANIM_OFF);
    }

    updateValueLabel();
    updateSliderColors();
}

int ToggleSlider::getValue() const
{
    return lv_slider_get_value(slider_);
}

double ToggleSlider::getScaledValue() const
{
    return getValue() * valueScale_;
}

bool ToggleSlider::isEnabled() const
{
    return lv_obj_has_state(switch_, LV_STATE_CHECKED);
}

void ToggleSlider::updateValueLabel()
{
    double scaledValue = getValue() * valueScale_;
    char buf[32];
    snprintf(buf, sizeof(buf), valueFormat_.c_str(), scaledValue);
    lv_label_set_text(valueLabel_, buf);
}

void ToggleSlider::updateSliderColors()
{
    if (isEnabled()) {
        lv_obj_set_style_bg_color(slider_, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_, lv_palette_main(LV_PALETTE_BLUE), LV_PART_KNOB);
    }
    else {
        lv_obj_set_style_bg_color(slider_, lv_color_hex(0x808080), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_, lv_color_hex(0x808080), LV_PART_KNOB);
    }
}

// --- Static Callbacks ---

void ToggleSlider::onSwitchChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    ToggleSlider* self = static_cast<ToggleSlider*>(lv_event_get_user_data(e));
    if (!self) return;

    bool enabled = self->isEnabled();

    if (enabled) {
        // Restore saved value (or use default).
        int valueToRestore = (self->savedValue_ > 0) ? self->savedValue_ : self->defaultValue_;
        lv_slider_set_value(self->slider_, valueToRestore, LV_ANIM_OFF);
    }
    else {
        // Save current value before disabling.
        int currentValue = lv_slider_get_value(self->slider_);
        if (currentValue > 0) {
            self->savedValue_ = currentValue;
        }
        lv_slider_set_value(self->slider_, 0, LV_ANIM_OFF);
    }

    self->updateValueLabel();
    self->updateSliderColors();

    // Call user callback.
    if (self->toggleCallback_) {
        self->toggleCallback_(enabled);
    }
}

void ToggleSlider::onSliderChanged(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    ToggleSlider* self = static_cast<ToggleSlider*>(lv_event_get_user_data(e));
    if (!self) return;

    self->updateValueLabel();

    // Call user callback.
    if (self->valueCallback_) {
        self->valueCallback_(self->getValue());
    }
}

void ToggleSlider::onSliderPressed(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;

    ToggleSlider* self = static_cast<ToggleSlider*>(lv_event_get_user_data(e));
    if (!self) return;

    // Auto-enable when user grabs disabled slider.
    if (!self->isEnabled()) {
        lv_obj_add_state(self->switch_, LV_STATE_CHECKED);

        // Restore value and update visuals.
        int valueToRestore = (self->savedValue_ > 0) ? self->savedValue_ : self->defaultValue_;
        lv_slider_set_value(self->slider_, valueToRestore, LV_ANIM_OFF);
        self->updateValueLabel();
        self->updateSliderColors();

        // Call user callback.
        if (self->toggleCallback_) {
            self->toggleCallback_(true);
        }
    }
}

} // namespace Ui
} // namespace DirtSim
