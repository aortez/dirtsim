#include "LVGLBuilder.h"
#include "core/LoggingChannels.h"
#include "spdlog/spdlog.h"
#include <cstdio>

using DirtSim::LoggingChannels;

// Result utilities.
template <typename T>
auto Ok(T&& value)
{
    return Result<std::decay_t<T>, std::string>::okay(std::forward<T>(value));
}

template <typename E>
auto Error(const E& error)
{
    return Result<lv_obj_t*, E>(error);
}

// ============================================================================
// SliderBuilder Implementation.
// ============================================================================

struct SliderLogData {
    std::string label;
    std::string format;
    std::function<double(int32_t)> transform;
    bool has_transform = false;
    bool active = false;
};

static void sliderLogCallback(lv_event_t* e)
{
    auto* data = static_cast<SliderLogData*>(lv_event_get_user_data(e));
    if (!data) {
        return;
    }

    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        data->active = true;
        return;
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    if (!data->active) {
        return;
    }
    data->active = false;

    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const int32_t raw_value = lv_slider_get_value(slider);
    if (data->has_transform) {
        const double display_value = data->transform(raw_value);
        char buf[32];
        snprintf(buf, sizeof(buf), data->format.c_str(), display_value);
        LOG_INFO(Controls, "Slider '{}' set to {}", data->label, buf);
        return;
    }

    LOG_INFO(Controls, "Slider '{}' set to {}", data->label, raw_value);
}

static void sliderLogDeleteCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }
    auto* data = static_cast<SliderLogData*>(lv_event_get_user_data(e));
    delete data;
}

LVGLBuilder::SliderBuilder::SliderBuilder(lv_obj_t* parent)
    : parent_(parent),
      slider_(nullptr),
      label_(nullptr),
      value_label_(nullptr),
      size_(200, 10),
      position_(0, 0, LV_ALIGN_TOP_LEFT),
      min_value_(0),
      max_value_(100),
      initial_value_(50),
      callback_(nullptr),
      user_data_(nullptr),
      callback_data_factory_(nullptr),
      use_factory_(false),
      event_code_(LV_EVENT_ALL),
      label_position_(0, -25, LV_ALIGN_TOP_LEFT),
      has_label_(false),
      value_label_position_(110, -25, LV_ALIGN_TOP_LEFT),
      has_value_label_(false)
{}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::size(int width, int height)
{
    size_ = Size(width, height);
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::size(const Size& sz)
{
    size_ = sz;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::position(int x, int y, lv_align_t align)
{
    position_ = Position(x, y, align);
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::position(const Position& pos)
{
    position_ = pos;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::range(int min, int max)
{
    if (min >= max) {
        spdlog::warn("SliderBuilder: Invalid range [{}, {}] - min must be less than max", min, max);
        return *this;
    }
    min_value_ = min;
    max_value_ = max;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::value(int initial_value)
{
    initial_value_ = initial_value;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::label(
    const char* text, int offset_x, int offset_y)
{
    if (!text) {
        spdlog::warn("SliderBuilder: null text provided for label");
        return *this;
    }
    label_text_ = text;
    label_position_ = Position(position_.x + offset_x, position_.y + offset_y, position_.align);
    has_label_ = true;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::valueLabel(
    const char* format, int offset_x, int offset_y)
{
    if (!format) {
        spdlog::warn("SliderBuilder: null format provided for value label");
        return *this;
    }
    value_format_ = format;
    value_label_position_ =
        Position(position_.x + offset_x, position_.y + offset_y, position_.align);
    has_value_label_ = true;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::callback(lv_event_cb_t cb, void* user_data)
{
    callback_ = cb;
    user_data_ = user_data;
    use_factory_ = false;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::callback(
    lv_event_cb_t cb, std::function<void*(lv_obj_t*)> callback_data_factory)
{
    callback_ = cb;
    callback_data_factory_ = callback_data_factory;
    use_factory_ = true;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::events(lv_event_code_t event_code)
{
    event_code_ = event_code;
    return *this;
}

LVGLBuilder::SliderBuilder& LVGLBuilder::SliderBuilder::valueTransform(
    std::function<double(int32_t)> transform)
{
    value_transform_ = transform;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::SliderBuilder::build()
{
    if (!parent_) {
        std::string error = "SliderBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    if (min_value_ >= max_value_) {
        std::string error = "SliderBuilder: invalid range [" + std::to_string(min_value_) + ", "
            + std::to_string(max_value_) + "] - min must be less than max";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Create slider.
    auto result = createSlider();
    if (result.isError()) {
        return result;
    }

    // Create optional labels.
    if (has_label_) {
        createLabel();
    }

    if (has_value_label_) {
        createValueLabel();
    }

    // Setup events.
    setupEvents();

    spdlog::debug(
        "SliderBuilder: Successfully created slider at ({}, {}) with range [{}, {}]",
        position_.x,
        position_.y,
        min_value_,
        max_value_);

    return Result<lv_obj_t*, std::string>::okay(slider_);
}

lv_obj_t* LVGLBuilder::SliderBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("SliderBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

Result<lv_obj_t*, std::string> LVGLBuilder::SliderBuilder::createSlider()
{
    slider_ = lv_slider_create(parent_);
    if (!slider_) {
        std::string error = "SliderBuilder: Failed to create slider object";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Set size.
    lv_obj_set_size(slider_, size_.width, size_.height);

    // Set position.
    lv_obj_align(slider_, position_.align, position_.x, position_.y);

    // Set range.
    lv_slider_set_range(slider_, min_value_, max_value_);

    // Set initial value (clamp to range).
    int clamped_value = std::max(min_value_, std::min(max_value_, initial_value_));
    if (clamped_value != initial_value_) {
        spdlog::warn(
            "SliderBuilder: Initial value {} clamped to range [{}, {}], using {}",
            initial_value_,
            min_value_,
            max_value_,
            clamped_value);
    }
    lv_slider_set_value(slider_, clamped_value, LV_ANIM_OFF);

    return Result<lv_obj_t*, std::string>::okay(slider_);
}

void LVGLBuilder::SliderBuilder::createLabel()
{
    label_ = lv_label_create(parent_);
    if (!label_) {
        spdlog::warn("SliderBuilder: Failed to create label object");
        return;
    }

    lv_label_set_text(label_, label_text_.c_str());
    lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), 0); // White text.
    lv_obj_align(label_, label_position_.align, label_position_.x, label_position_.y);
}

void LVGLBuilder::SliderBuilder::createValueLabel()
{
    value_label_ = lv_label_create(parent_);
    if (!value_label_) {
        spdlog::warn("SliderBuilder: Failed to create value label object");
        return;
    }

    lv_obj_set_style_text_color(value_label_, lv_color_hex(0xFFFFFF), 0); // White text.

    // Set initial value text based on slider's current value.
    char buf[32];
    int32_t current_value = lv_slider_get_value(slider_);

    // Apply transform if provided, otherwise use raw value.
    double display_value;
    if (value_transform_) {
        display_value = value_transform_(current_value);
    }
    else {
        display_value = static_cast<double>(current_value);
    }

    snprintf(buf, sizeof(buf), value_format_.c_str(), display_value);
    lv_label_set_text(value_label_, buf);

    lv_obj_align(
        value_label_,
        value_label_position_.align,
        value_label_position_.x,
        value_label_position_.y);
}

void LVGLBuilder::SliderBuilder::setupEvents()
{
    void* user_data = user_data_;

    // If using factory, create callback data with value label.
    if (use_factory_ && callback_data_factory_) {
        user_data = callback_data_factory_(value_label_);
    }

    // Add user's callback.
    if (callback_) {
        lv_obj_add_event_cb(slider_, callback_, event_code_, user_data);
    }

    const std::string label = label_text_.empty() ? "Slider" : label_text_;
    const std::string format = value_format_.empty() ? "%.1f" : value_format_;
    auto* logData = new SliderLogData{
        label, format, value_transform_, static_cast<bool>(value_transform_), false
    };
    lv_obj_add_event_cb(slider_, sliderLogCallback, LV_EVENT_PRESSED, logData);
    lv_obj_add_event_cb(slider_, sliderLogCallback, LV_EVENT_RELEASED, logData);
    lv_obj_add_event_cb(slider_, sliderLogCallback, LV_EVENT_PRESS_LOST, logData);
    lv_obj_add_event_cb(slider_, sliderLogDeleteCallback, LV_EVENT_DELETE, logData);

    // Add auto-update callback for value label if we have one.
    if (value_label_ && has_value_label_) {
        // Create persistent data for the value label callback.
        ValueLabelData* data = new ValueLabelData{ value_label_, value_format_, value_transform_ };

        // Add the value update callback with the persistent data.
        lv_obj_add_event_cb(slider_, valueUpdateCallback, LV_EVENT_VALUE_CHANGED, data);

        // Add a delete callback to clean up the allocated data.
        lv_obj_add_event_cb(slider_, sliderDeleteCallback, LV_EVENT_DELETE, data);
    }
}

void LVGLBuilder::SliderBuilder::valueUpdateCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        ValueLabelData* data = static_cast<ValueLabelData*>(lv_event_get_user_data(e));
        if (data && data->value_label) {
            lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
            char buf[32];
            int32_t current_value = lv_slider_get_value(slider);

            // Apply transform if provided, otherwise use raw value.
            double display_value;
            if (data->transform) {
                display_value = data->transform(current_value);
            }
            else {
                display_value = static_cast<double>(current_value);
            }

            snprintf(buf, sizeof(buf), data->format.c_str(), display_value);
            lv_label_set_text(data->value_label, buf);
        }
    }
}

void LVGLBuilder::SliderBuilder::sliderDeleteCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        ValueLabelData* data = static_cast<ValueLabelData*>(lv_event_get_user_data(e));
        delete data;
    }
}

struct ButtonLogData {
    std::string label;
    bool checkable = false;
};

static void buttonLogCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    auto* data = static_cast<ButtonLogData*>(lv_event_get_user_data(e));
    if (!data) {
        return;
    }

    auto* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (data->checkable) {
        const bool is_checked = lv_obj_has_state(button, LV_STATE_CHECKED);
        LOG_INFO(Controls, "Button '{}' {}", data->label, is_checked ? "on" : "off");
        return;
    }

    LOG_INFO(Controls, "Button '{}' clicked", data->label);
}

static void buttonLogDeleteCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }
    auto* data = static_cast<ButtonLogData*>(lv_event_get_user_data(e));
    delete data;
}

// ============================================================================
// ButtonBuilder Implementation.
// ============================================================================

LVGLBuilder::ButtonBuilder::ButtonBuilder(lv_obj_t* parent)
    : parent_(parent),
      button_(nullptr),
      label_(nullptr),
      size_(Style::CONTROL_WIDTH, Style::ACTION_SIZE),
      position_(0, 0, LV_ALIGN_TOP_LEFT),
      is_toggle_(false),
      is_checkable_(false),
      callback_(nullptr),
      user_data_(nullptr),
      event_code_(LV_EVENT_CLICKED),
      bgColor_(0),
      pressedColor_(0),
      textColor_(0xFFFFFF),
      radius_(Style::RADIUS),
      font_(Style::CONTROL_FONT),
      hasBgColor_(false),
      hasPressedColor_(false),
      hasTextColor_(false)
{}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::size(int width, int height)
{
    size_ = Size(width, height);
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::size(const Size& sz)
{
    size_ = sz;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::position(int x, int y, lv_align_t align)
{
    position_ = Position(x, y, align);
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::position(const Position& pos)
{
    position_ = pos;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::text(const char* text)
{
    if (!text) {
        spdlog::warn("ButtonBuilder: null text provided");
        return *this;
    }
    text_ = text;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::toggle(bool enabled)
{
    is_toggle_ = enabled;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::checkable(bool enabled)
{
    is_checkable_ = enabled;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::callback(lv_event_cb_t cb, void* user_data)
{
    callback_ = cb;
    user_data_ = user_data;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::events(lv_event_code_t event_code)
{
    event_code_ = event_code;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::backgroundColor(uint32_t color)
{
    bgColor_ = color;
    hasBgColor_ = true;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::pressedColor(uint32_t color)
{
    pressedColor_ = color;
    hasPressedColor_ = true;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::textColor(uint32_t color)
{
    textColor_ = color;
    hasTextColor_ = true;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::radius(int px)
{
    radius_ = px;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::font(const lv_font_t* font)
{
    font_ = font;
    return *this;
}

LVGLBuilder::ButtonBuilder& LVGLBuilder::ButtonBuilder::icon(const char* symbol)
{
    if (symbol) {
        icon_ = symbol;
    }
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::ButtonBuilder::build()
{
    if (!parent_) {
        std::string error = "ButtonBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Create button.
    auto result = createButton();
    if (result.isError()) {
        return result;
    }

    // Create label if text provided.
    if (!text_.empty()) {
        createLabel();
    }

    // Setup behavior (toggle, checkable).
    setupBehavior();

    // Setup events.
    setupEvents();

    spdlog::debug(
        "ButtonBuilder: Successfully created button '{}' at ({}, {})",
        text_,
        position_.x,
        position_.y);

    return Result<lv_obj_t*, std::string>::okay(button_);
}

lv_obj_t* LVGLBuilder::ButtonBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("ButtonBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

Result<lv_obj_t*, std::string> LVGLBuilder::ButtonBuilder::createButton()
{
    button_ = lv_btn_create(parent_);
    if (!button_) {
        std::string error = "ButtonBuilder: Failed to create button object";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Set size.
    lv_obj_set_size(button_, size_.width, size_.height);

    // Set position.
    lv_obj_align(button_, position_.align, position_.x, position_.y);

    // Clear PRESS_LOCK so users can cancel by dragging away before releasing.
    // With this cleared, CLICKED only fires if press and release are both on the button.
    lv_obj_clear_flag(button_, LV_OBJ_FLAG_PRESS_LOCK);

    // Apply background color.
    if (hasBgColor_) {
        lv_obj_set_style_bg_color(button_, lv_color_hex(bgColor_), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button_, LV_OPA_COVER, LV_PART_MAIN);
    }

    // Apply pressed color (darker state when touched).
    if (hasPressedColor_) {
        lv_obj_set_style_bg_color(button_, lv_color_hex(pressedColor_), LV_STATE_PRESSED);
    }

    // Apply corner radius (always applied, defaults to Style::RADIUS).
    lv_obj_set_style_radius(button_, radius_, LV_PART_MAIN);

    return Result<lv_obj_t*, std::string>::okay(button_);
}

void LVGLBuilder::ButtonBuilder::createLabel()
{
    label_ = lv_label_create(button_);
    if (!label_) {
        spdlog::warn("ButtonBuilder: Failed to create label object");
        return;
    }

    // Build label text with optional icon prefix.
    std::string labelText;
    if (!icon_.empty()) {
        labelText = icon_ + " " + text_;
    }
    else {
        labelText = text_;
    }
    lv_label_set_text(label_, labelText.c_str());
    lv_obj_center(label_);

    // Apply text color.
    if (hasTextColor_) {
        lv_obj_set_style_text_color(label_, lv_color_hex(textColor_), LV_PART_MAIN);
    }

    // Apply font (always applied, defaults to Style::CONTROL_FONT).
    if (font_) {
        lv_obj_set_style_text_font(label_, font_, LV_PART_MAIN);
    }
}

void LVGLBuilder::ButtonBuilder::setupBehavior()
{
    if (is_checkable_) {
        lv_obj_add_flag(button_, LV_OBJ_FLAG_CHECKABLE);
    }

    // Note: LVGL doesn't have a specific "toggle" flag - toggle behavior.
    // is typically implemented through checkable flag and event handling.
    if (is_toggle_) {
        lv_obj_add_flag(button_, LV_OBJ_FLAG_CHECKABLE);
    }
}

void LVGLBuilder::ButtonBuilder::setupEvents()
{
    // Set user_data on the button object itself so event handlers can retrieve it.
    if (user_data_) {
        lv_obj_set_user_data(button_, user_data_);
    }
    if (callback_) {
        lv_obj_add_event_cb(button_, callback_, event_code_, user_data_);
    }

    const std::string label = text_.empty() ? "Button" : text_;
    auto* logData = new ButtonLogData{ label, is_toggle_ || is_checkable_ };
    lv_obj_add_event_cb(button_, buttonLogCallback, LV_EVENT_CLICKED, logData);
    lv_obj_add_event_cb(button_, buttonLogDeleteCallback, LV_EVENT_DELETE, logData);
}

// ============================================================================
// LabelBuilder Implementation.
// ============================================================================

LVGLBuilder::LabelBuilder::LabelBuilder(lv_obj_t* parent)
    : parent_(parent), position_(0, 0, LV_ALIGN_TOP_LEFT)
{}

LVGLBuilder::LabelBuilder& LVGLBuilder::LabelBuilder::text(const char* text)
{
    if (!text) {
        spdlog::warn("LabelBuilder: null text provided");
        return *this;
    }
    text_ = text;
    return *this;
}

LVGLBuilder::LabelBuilder& LVGLBuilder::LabelBuilder::position(int x, int y, lv_align_t align)
{
    position_ = Position(x, y, align);
    return *this;
}

LVGLBuilder::LabelBuilder& LVGLBuilder::LabelBuilder::position(const Position& pos)
{
    position_ = pos;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::LabelBuilder::build()
{
    if (!parent_) {
        std::string error = "LabelBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    lv_obj_t* label = lv_label_create(parent_);
    if (!label) {
        std::string error = "LabelBuilder: Failed to create label object";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    if (!text_.empty()) {
        lv_label_set_text(label, text_.c_str());
    }

    lv_obj_align(label, position_.align, position_.x, position_.y);

    spdlog::debug(
        "LabelBuilder: Successfully created label '{}' at ({}, {})",
        text_,
        position_.x,
        position_.y);

    return Result<lv_obj_t*, std::string>::okay(label);
}

// ============================================================================
// DropdownBuilder Implementation.
// ============================================================================

LVGLBuilder::DropdownBuilder::DropdownBuilder(lv_obj_t* parent)
    : parent_(parent),
      position_(0, 0, LV_ALIGN_TOP_LEFT),
      size_(Style::CONTROL_WIDTH, Style::ACTION_SIZE)
{}

LVGLBuilder::DropdownBuilder& LVGLBuilder::DropdownBuilder::options(const char* options)
{
    options_ = options ? options : "";
    return *this;
}

LVGLBuilder::DropdownBuilder& LVGLBuilder::DropdownBuilder::selected(uint16_t index)
{
    selectedIndex_ = index;
    return *this;
}

LVGLBuilder::DropdownBuilder& LVGLBuilder::DropdownBuilder::position(int x, int y, lv_align_t align)
{
    position_ = Position(x, y, align);
    return *this;
}

LVGLBuilder::DropdownBuilder& LVGLBuilder::DropdownBuilder::position(const Position& pos)
{
    position_ = pos;
    return *this;
}

LVGLBuilder::DropdownBuilder& LVGLBuilder::DropdownBuilder::size(int width, int height)
{
    size_ = Size(width, height);
    return *this;
}

LVGLBuilder::DropdownBuilder& LVGLBuilder::DropdownBuilder::size(const Size& s)
{
    size_ = s;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::DropdownBuilder::build()
{
    if (!parent_) {
        return Error<std::string>("DropdownBuilder: parent is null");
    }

    lv_obj_t* dropdown = lv_dropdown_create(parent_);
    if (!dropdown) {
        return Error<std::string>("DropdownBuilder: failed to create dropdown");
    }

    // Set options.
    if (!options_.empty()) {
        lv_dropdown_set_options(dropdown, options_.c_str());
    }

    // Set selected index.
    lv_dropdown_set_selected(dropdown, selectedIndex_);

    // Set size and position.
    lv_obj_set_size(dropdown, size_.width, size_.height);
    lv_obj_align(dropdown, position_.align, position_.x, position_.y);

    return Ok(dropdown);
}

lv_obj_t* LVGLBuilder::DropdownBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("DropdownBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

// ============================================================================
// LabeledSwitchBuilder Implementation.
// ============================================================================

struct LabeledSwitchLogData {
    std::string label;
};

static void labeledSwitchLogCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* data = static_cast<LabeledSwitchLogData*>(lv_event_get_user_data(e));
    if (!data) {
        return;
    }

    auto* toggle = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const bool is_checked = lv_obj_has_state(toggle, LV_STATE_CHECKED);
    LOG_INFO(Controls, "Toggle '{}' {}", data->label, is_checked ? "on" : "off");
}

static void labeledSwitchLogDeleteCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }
    auto* data = static_cast<LabeledSwitchLogData*>(lv_event_get_user_data(e));
    delete data;
}

LVGLBuilder::LabeledSwitchBuilder::LabeledSwitchBuilder(lv_obj_t* parent)
    : parent_(parent), container_(nullptr), switch_(nullptr), label_(nullptr)
{}

LVGLBuilder::LabeledSwitchBuilder& LVGLBuilder::LabeledSwitchBuilder::label(const char* text)
{
    label_text_ = text;
    return *this;
}

LVGLBuilder::LabeledSwitchBuilder& LVGLBuilder::LabeledSwitchBuilder::initialState(bool checked)
{
    initial_checked_ = checked;
    return *this;
}

LVGLBuilder::LabeledSwitchBuilder& LVGLBuilder::LabeledSwitchBuilder::callback(
    lv_event_cb_t cb, void* user_data)
{
    callback_ = cb;
    user_data_ = user_data;
    return *this;
}

LVGLBuilder::LabeledSwitchBuilder& LVGLBuilder::LabeledSwitchBuilder::size(int width, int height)
{
    width_ = width;
    height_ = height;
    return *this;
}

LVGLBuilder::LabeledSwitchBuilder& LVGLBuilder::LabeledSwitchBuilder::height(int h)
{
    height_ = h;
    return *this;
}

LVGLBuilder::LabeledSwitchBuilder& LVGLBuilder::LabeledSwitchBuilder::width(int w)
{
    width_ = w;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::LabeledSwitchBuilder::build()
{
    return createLabeledSwitch();
}

lv_obj_t* LVGLBuilder::LabeledSwitchBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("LabeledSwitchBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

// Helper callback for container click to toggle switch.
static void labeledSwitchContainerClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t* container = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* switch_obj = static_cast<lv_obj_t*>(lv_obj_get_user_data(container));

    if (switch_obj) {
        // Toggle switch state.
        if (lv_obj_has_state(switch_obj, LV_STATE_CHECKED)) {
            lv_obj_clear_state(switch_obj, LV_STATE_CHECKED);
        }
        else {
            lv_obj_add_state(switch_obj, LV_STATE_CHECKED);
        }

        // Send VALUE_CHANGED event to trigger the callback.
        lv_obj_send_event(switch_obj, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

Result<lv_obj_t*, std::string> LVGLBuilder::LabeledSwitchBuilder::createLabeledSwitch()
{
    // Create horizontal container for switch + label.
    container_ = lv_obj_create(parent_);
    if (!container_) {
        return Result<lv_obj_t*, std::string>::error("Failed to create container");
    }

    // Apply sizing (defaults from Style constants).
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(container_, Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_right(container_, Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_column(container_, Style::GAP, 0);

    lv_obj_set_style_bg_color(container_, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    // Rounded corners (from Style constants).
    lv_obj_set_style_radius(container_, Style::RADIUS, 0);

    // Clear PRESS_LOCK for cancelable press behavior (like buttons).
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_PRESS_LOCK);

    // Create switch with touch-friendly size.
    switch_ = lv_switch_create(container_);
    if (!switch_) {
        return Result<lv_obj_t*, std::string>::error("Failed to create switch");
    }
    lv_obj_set_size(switch_, Style::SWITCH_WIDTH, Style::SWITCH_HEIGHT);

    // Set initial state.
    if (initial_checked_) {
        lv_obj_add_state(switch_, LV_STATE_CHECKED);
    }

    // Set up callback.
    if (callback_) {
        lv_obj_add_event_cb(switch_, callback_, LV_EVENT_VALUE_CHANGED, user_data_);
    }

    const std::string label = label_text_.empty() ? "Toggle" : label_text_;
    auto* logData = new LabeledSwitchLogData{ label };
    lv_obj_add_event_cb(switch_, labeledSwitchLogCallback, LV_EVENT_VALUE_CHANGED, logData);
    lv_obj_add_event_cb(switch_, labeledSwitchLogDeleteCallback, LV_EVENT_DELETE, logData);

    // Create label.
    if (!label_text_.empty()) {
        label_ = lv_label_create(container_);
        if (label_) {
            lv_label_set_text(label_, label_text_.c_str());
            lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(label_, Style::CONTROL_FONT, 0);
        }
    }

    // Store switch pointer in container's user data for click handler.
    lv_obj_set_user_data(container_, switch_);

    // Add click handler to container to toggle switch.
    lv_obj_add_event_cb(container_, labeledSwitchContainerClicked, LV_EVENT_CLICKED, nullptr);

    // Make container clickable.
    lv_obj_add_flag(container_, LV_OBJ_FLAG_CLICKABLE);

    return Result<lv_obj_t*, std::string>::okay(switch_);
}

// ============================================================================
// ToggleSliderBuilder Implementation.
// ============================================================================

// State structure for toggle slider callbacks.
struct ToggleSliderState {
    lv_obj_t* slider;
    lv_obj_t* valueLabel;
    lv_obj_t* switch_obj;
    double valueScale;
    std::string valueFormat;
    int savedValue;
    int defaultValue;
    lv_event_cb_t sliderCallback;
    void* sliderUserData;
    lv_event_cb_t toggleCallback;
    void* toggleUserData;
    std::string label;
    bool sliderInteractionActive;
};

static void toggleSliderSwitchCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    ToggleSliderState* state = static_cast<ToggleSliderState*>(lv_event_get_user_data(e));
    if (!state) return;

    bool isEnabled = lv_obj_has_state(state->switch_obj, LV_STATE_CHECKED);

    if (isEnabled) {
        // Toggle ON: Restore saved value (or use default).
        int valueToRestore = (state->savedValue > 0) ? state->savedValue : state->defaultValue;
        lv_slider_set_value(state->slider, valueToRestore, LV_ANIM_OFF);

        // Restore blue color.
        lv_obj_set_style_bg_color(
            state->slider, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(state->slider, lv_palette_main(LV_PALETTE_BLUE), LV_PART_KNOB);

        // Update value label.
        double scaledValue = valueToRestore * state->valueScale;
        char buf[32];
        snprintf(buf, sizeof(buf), state->valueFormat.c_str(), scaledValue);
        lv_label_set_text(state->valueLabel, buf);
    }
    else {
        // Toggle OFF: Save current value, set to 0, gray out slider.
        // Note: Slider stays interactive for auto-enable feature.
        int currentValue = lv_slider_get_value(state->slider);
        if (currentValue > 0) {
            state->savedValue = currentValue;
        }

        lv_slider_set_value(state->slider, 0, LV_ANIM_OFF);

        // Grey color when disabled (visual feedback only, still interactive).
        lv_obj_set_style_bg_color(state->slider, lv_color_hex(0x808080), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(state->slider, lv_color_hex(0x808080), LV_PART_KNOB);

        // Update value label to 0.
        char buf[32];
        snprintf(buf, sizeof(buf), state->valueFormat.c_str(), 0.0);
        lv_label_set_text(state->valueLabel, buf);
    }

    const std::string label = state->label.empty() ? "Toggle" : state->label;
    LOG_INFO(Controls, "Toggle '{}' {}", label, isEnabled ? "on" : "off");

    // Call user callback if provided.
    if (state->toggleCallback) {
        state->toggleCallback(e);
    }
}

static void toggleSliderValueCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    ToggleSliderState* state = static_cast<ToggleSliderState*>(lv_event_get_user_data(e));
    if (!state) return;

    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int value = lv_slider_get_value(slider);
    double scaledValue = value * state->valueScale;

    // Update value label.
    char buf[32];
    snprintf(buf, sizeof(buf), state->valueFormat.c_str(), scaledValue);
    lv_label_set_text(state->valueLabel, buf);

    // Call user callback if provided.
    if (state->sliderCallback) {
        state->sliderCallback(e);
    }
}

static void toggleSliderInteractionCallback(lv_event_t* e)
{
    ToggleSliderState* state = static_cast<ToggleSliderState*>(lv_event_get_user_data(e));
    if (!state) {
        return;
    }

    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        state->sliderInteractionActive = true;
        return;
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    if (!state->sliderInteractionActive) {
        return;
    }
    state->sliderInteractionActive = false;

    const int value = lv_slider_get_value(state->slider);
    const double scaledValue = value * state->valueScale;
    char buf[32];
    snprintf(buf, sizeof(buf), state->valueFormat.c_str(), scaledValue);
    const std::string label = state->label.empty() ? "Slider" : state->label;
    LOG_INFO(Controls, "Slider '{}' set to {}", label, buf);
}

static void toggleSliderAutoEnableCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;

    ToggleSliderState* state = static_cast<ToggleSliderState*>(lv_event_get_user_data(e));
    if (!state) return;

    // Check if toggle is currently disabled.
    bool isEnabled = lv_obj_has_state(state->switch_obj, LV_STATE_CHECKED);
    if (!isEnabled) {
        // Auto-enable the toggle when user grabs disabled slider.
        lv_obj_add_state(state->switch_obj, LV_STATE_CHECKED);

        // Trigger the switch callback to restore value, enable slider, update colors, etc.
        lv_obj_send_event(state->switch_obj, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

static void toggleSliderDeleteCallback(lv_event_t* e)
{
    ToggleSliderState* state = static_cast<ToggleSliderState*>(lv_event_get_user_data(e));
    delete state;
}

LVGLBuilder::ToggleSliderBuilder::ToggleSliderBuilder(lv_obj_t* parent)
    : parent_(parent),
      container_(nullptr),
      switch_(nullptr),
      slider_(nullptr),
      label_(nullptr),
      valueLabel_(nullptr)
{}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::label(const char* text)
{
    label_text_ = text;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::sliderWidth(int width)
{
    slider_width_ = width;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::range(int min, int max)
{
    range_min_ = min;
    range_max_ = max;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::value(int initialValue)
{
    initial_value_ = initialValue;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::defaultValue(int defValue)
{
    default_value_ = defValue;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::valueScale(double scale)
{
    value_scale_ = scale;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::valueFormat(const char* format)
{
    value_format_ = format;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::initiallyEnabled(bool enabled)
{
    initially_enabled_ = enabled;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::onToggle(
    lv_event_cb_t cb, void* user_data)
{
    toggle_callback_ = cb;
    toggle_user_data_ = user_data;
    return *this;
}

LVGLBuilder::ToggleSliderBuilder& LVGLBuilder::ToggleSliderBuilder::onSliderChange(
    lv_event_cb_t cb, void* user_data)
{
    slider_callback_ = cb;
    slider_user_data_ = user_data;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::ToggleSliderBuilder::createToggleSlider()
{
    // Create container for the whole control group.
    // Height accommodates: top row (switch height) + gap + slider (with large knob).
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

    // Create label (top left, vertically centered with switch).
    label_ = lv_label_create(container_);
    lv_label_set_text(label_, label_text_.c_str());
    lv_obj_align(label_, LV_ALIGN_TOP_LEFT, 0, (Style::SWITCH_HEIGHT - 16) / 2);
    lv_obj_set_style_text_color(label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label_, Style::CONTROL_FONT, 0);

    // Create switch (top right) - large touch-friendly size.
    switch_ = lv_switch_create(container_);
    lv_obj_align(switch_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(switch_, Style::SWITCH_WIDTH, Style::SWITCH_HEIGHT);

    if (initially_enabled_) {
        lv_obj_add_state(switch_, LV_STATE_CHECKED);
    }

    // Create slider (below label/switch) - large touch-friendly knob.
    slider_ = lv_slider_create(container_);
    lv_obj_align(slider_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(slider_, LV_PCT(100), Style::SLIDER_TRACK_HEIGHT);
    lv_slider_set_range(slider_, range_min_, range_max_);
    lv_slider_set_value(slider_, initially_enabled_ ? initial_value_ : 0, LV_ANIM_OFF);

    // Style the slider knob for easy touch grabbing.
    lv_obj_set_style_pad_all(
        slider_, Style::SLIDER_KNOB_SIZE / 2 - Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_, Style::SLIDER_KNOB_RADIUS, LV_PART_KNOB);

    // Round the track ends.
    lv_obj_set_style_radius(slider_, Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_, Style::SLIDER_TRACK_HEIGHT / 2, LV_PART_INDICATOR);

    // Set initial color (slider always interactive for auto-enable).
    if (!initially_enabled_) {
        lv_obj_set_style_bg_color(slider_, lv_color_hex(0x808080), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_, lv_color_hex(0x808080), LV_PART_KNOB);
    }
    else {
        lv_obj_set_style_bg_color(slider_, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_, lv_palette_main(LV_PALETTE_BLUE), LV_PART_KNOB);
    }

    // Create value label (right of label, shows current value).
    valueLabel_ = lv_label_create(container_);
    double scaledValue = (initially_enabled_ ? initial_value_ : 0) * value_scale_;
    char buf[32];
    snprintf(buf, sizeof(buf), value_format_.c_str(), scaledValue);
    lv_label_set_text(valueLabel_, buf);
    lv_obj_align_to(valueLabel_, label_, LV_ALIGN_OUT_RIGHT_MID, Style::GAP, 0);
    lv_obj_set_style_text_font(valueLabel_, Style::CONTROL_FONT, 0);
    lv_obj_set_style_text_color(valueLabel_, lv_color_hex(0xFFFFFF), 0);

    // Create persistent state for callbacks.
    ToggleSliderState* state = new ToggleSliderState{ slider_,
                                                      valueLabel_,
                                                      switch_,
                                                      value_scale_,
                                                      value_format_,
                                                      initial_value_,
                                                      default_value_,
                                                      slider_callback_,
                                                      slider_user_data_,
                                                      toggle_callback_,
                                                      toggle_user_data_,
                                                      label_text_,
                                                      false };

    // Set user_data on widgets so user callbacks can access it.
    if (toggle_user_data_) {
        lv_obj_set_user_data(switch_, toggle_user_data_);
    }
    if (slider_user_data_) {
        lv_obj_set_user_data(slider_, slider_user_data_);
    }

    // Set up callbacks.
    lv_obj_add_event_cb(switch_, toggleSliderSwitchCallback, LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(slider_, toggleSliderValueCallback, LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(slider_, toggleSliderAutoEnableCallback, LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(slider_, toggleSliderInteractionCallback, LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(slider_, toggleSliderInteractionCallback, LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(slider_, toggleSliderInteractionCallback, LV_EVENT_PRESS_LOST, state);

    // Cleanup callback to free state.
    lv_obj_add_event_cb(container_, toggleSliderDeleteCallback, LV_EVENT_DELETE, state);

    return Result<lv_obj_t*, std::string>::okay(container_);
}

Result<lv_obj_t*, std::string> LVGLBuilder::ToggleSliderBuilder::build()
{
    return createToggleSlider();
}

lv_obj_t* LVGLBuilder::ToggleSliderBuilder::buildOrLog()
{
    auto result = createToggleSlider();
    if (result.isError()) {
        spdlog::error("ToggleSliderBuilder: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

// ============================================================================
// CollapsiblePanelBuilder Implementation.
// ============================================================================

LVGLBuilder::CollapsiblePanelBuilder::CollapsiblePanelBuilder(lv_obj_t* parent)
    : parent_(parent),
      container_(nullptr),
      header_(nullptr),
      content_(nullptr),
      title_label_(nullptr),
      indicator_(nullptr),
      size_(LV_PCT(30), LV_SIZE_CONTENT),
      is_expanded_(true),
      bg_color_(0x303030),
      header_color_(0x404040),
      toggle_callback_(nullptr),
      user_data_(nullptr)
{}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::title(const char* text)
{
    if (text) {
        title_text_ = text;
    }
    return *this;
}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::size(
    int width, int height)
{
    size_ = Size(width, height);
    return *this;
}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::size(const Size& sz)
{
    size_ = sz;
    return *this;
}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::initiallyExpanded(
    bool expanded)
{
    is_expanded_ = expanded;
    return *this;
}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::backgroundColor(
    uint32_t color)
{
    bg_color_ = color;
    return *this;
}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::headerColor(
    uint32_t color)
{
    header_color_ = color;
    return *this;
}

LVGLBuilder::CollapsiblePanelBuilder& LVGLBuilder::CollapsiblePanelBuilder::onToggle(
    lv_event_cb_t cb, void* user_data)
{
    toggle_callback_ = cb;
    user_data_ = user_data;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::CollapsiblePanelBuilder::build()
{
    if (!parent_) {
        std::string error = "CollapsiblePanelBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    auto result = createCollapsiblePanel();
    if (result.isError()) {
        return result;
    }

    spdlog::debug(
        "CollapsiblePanelBuilder: Successfully created collapsible panel '{}'", title_text_);

    return Result<lv_obj_t*, std::string>::okay(container_);
}

lv_obj_t* LVGLBuilder::CollapsiblePanelBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("CollapsiblePanelBuilder: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

Result<lv_obj_t*, std::string> LVGLBuilder::CollapsiblePanelBuilder::createCollapsiblePanel()
{
    // Create main container.
    container_ = lv_obj_create(parent_);
    if (!container_) {
        std::string error = "CollapsiblePanelBuilder: Failed to create container";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    lv_obj_set_size(container_, size_.width, size_.height);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(bg_color_), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    // Create clickable header.
    header_ = lv_obj_create(container_);
    if (!header_) {
        std::string error = "CollapsiblePanelBuilder: Failed to create header";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    lv_obj_set_size(header_, LV_PCT(100), Style::ACTION_SIZE);
    lv_obj_set_flex_flow(header_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header_, Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_right(header_, Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_ver(header_, 0, 0); // No vertical padding - let flex centering handle it.
    lv_obj_set_style_pad_column(header_, Style::GAP, 0);
    lv_obj_set_style_bg_color(header_, lv_color_hex(header_color_), 0);
    lv_obj_set_style_bg_opa(header_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header_, Style::RADIUS, 0);
    lv_obj_add_flag(header_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(header_, LV_OBJ_FLAG_PRESS_LOCK);

    // Create expand/collapse indicator.
    indicator_ = lv_label_create(header_);
    if (!indicator_) {
        std::string error = "CollapsiblePanelBuilder: Failed to create indicator";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    lv_label_set_text(indicator_, is_expanded_ ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(indicator_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(indicator_, lv_color_hex(0xFFFFFF), 0);

    // Create title label.
    title_label_ = lv_label_create(header_);
    if (!title_label_) {
        std::string error = "CollapsiblePanelBuilder: Failed to create title label";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    lv_label_set_text(title_label_, title_text_.c_str());
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xFFFFFF), 0);

    // Create content area.
    content_ = lv_obj_create(container_);
    if (!content_) {
        std::string error = "CollapsiblePanelBuilder: Failed to create content area";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    lv_obj_set_size(content_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content_, Style::GAP, 0);
    lv_obj_set_style_pad_left(content_, Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_right(content_, Style::PAD_HORIZONTAL, 0);
    lv_obj_set_style_pad_top(content_, Style::PAD_VERTICAL, 0);
    lv_obj_set_style_pad_bottom(content_, Style::PAD_VERTICAL, 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);

    // Set initial state.
    if (!is_expanded_) {
        lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
    }

    // Allocate and store state for the header click callback.
    PanelState* state = new PanelState{ content_, indicator_, is_expanded_ };
    lv_obj_set_user_data(header_, state);

    // Setup header click event.
    lv_obj_add_event_cb(header_, onHeaderClick, LV_EVENT_CLICKED, nullptr);

    // Setup optional user callback (called after internal state change).
    if (toggle_callback_) {
        lv_obj_add_event_cb(header_, toggle_callback_, LV_EVENT_CLICKED, user_data_);
    }

    // Add delete callback to clean up allocated state.
    lv_obj_add_event_cb(
        header_,
        [](lv_event_t* e) {
            lv_obj_t* header = static_cast<lv_obj_t*>(lv_event_get_target(e));
            PanelState* state = static_cast<PanelState*>(lv_obj_get_user_data(header));
            delete state;
        },
        LV_EVENT_DELETE,
        nullptr);

    return Result<lv_obj_t*, std::string>::okay(container_);
}

void LVGLBuilder::CollapsiblePanelBuilder::onHeaderClick(lv_event_t* e)
{
    lv_obj_t* header = static_cast<lv_obj_t*>(lv_event_get_target(e));
    PanelState* state = static_cast<PanelState*>(lv_obj_get_user_data(header));

    if (!state || !state->content || !state->indicator) {
        spdlog::warn("CollapsiblePanelBuilder: Invalid panel state in header click");
        return;
    }

    // Toggle expanded state.
    state->is_expanded = !state->is_expanded;

    // Update indicator symbol.
    lv_label_set_text(state->indicator, state->is_expanded ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT);

    // Show/hide content with animation.
    if (state->is_expanded) {
        lv_obj_remove_flag(state->content, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(state->content, LV_OBJ_FLAG_HIDDEN);
    }

    spdlog::debug(
        "CollapsiblePanelBuilder: Panel toggled to {}",
        state->is_expanded ? "expanded" : "collapsed");
}

// ============================================================================
// Static Factory Methods.
// ============================================================================

LVGLBuilder::SliderBuilder LVGLBuilder::slider(lv_obj_t* parent)
{
    return SliderBuilder(parent);
}

LVGLBuilder::ButtonBuilder LVGLBuilder::button(lv_obj_t* parent)
{
    return ButtonBuilder(parent);
}

LVGLBuilder::LabelBuilder LVGLBuilder::label(lv_obj_t* parent)
{
    return LabelBuilder(parent);
}

LVGLBuilder::DropdownBuilder LVGLBuilder::dropdown(lv_obj_t* parent)
{
    return DropdownBuilder(parent);
}

LVGLBuilder::LabeledSwitchBuilder LVGLBuilder::labeledSwitch(lv_obj_t* parent)
{
    return LabeledSwitchBuilder(parent);
}

LVGLBuilder::ToggleSliderBuilder LVGLBuilder::toggleSlider(lv_obj_t* parent)
{
    return ToggleSliderBuilder(parent);
}

LVGLBuilder::CollapsiblePanelBuilder LVGLBuilder::collapsiblePanel(lv_obj_t* parent)
{
    return CollapsiblePanelBuilder(parent);
}

LVGLBuilder::ActionButtonBuilder LVGLBuilder::actionButton(lv_obj_t* parent)
{
    return ActionButtonBuilder(parent);
}

// ============================================================================
// ActionButtonBuilder Implementation
// ============================================================================

LVGLBuilder::ActionButtonBuilder::ActionButtonBuilder(lv_obj_t* parent)
    : parent_(parent), container_(nullptr), button_(nullptr), label_(nullptr), icon_label_(nullptr)
{}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::text(const char* txt)
{
    if (txt) {
        text_ = txt;
    }
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::icon(const char* symbol)
{
    if (symbol) {
        icon_ = symbol;
    }
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::font(const lv_font_t* f)
{
    font_ = f;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::iconPositionRight()
{
    icon_trailing_ = true;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::mode(ActionMode m)
{
    mode_ = m;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::checked(bool initial)
{
    initial_checked_ = initial;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::size(int dimension)
{
    width_ = dimension;
    height_ = dimension;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::width(int w)
{
    width_ = w;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::height(int h)
{
    height_ = h;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::troughPadding(int px)
{
    trough_padding_ = px;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::layoutRow()
{
    layout_flow_ = LV_FLEX_FLOW_ROW;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::layoutColumn()
{
    layout_flow_ = LV_FLEX_FLOW_COLUMN;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::alignLeft()
{
    main_align_ = LV_FLEX_ALIGN_START;
    cross_align_ = LV_FLEX_ALIGN_CENTER;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::alignCenter()
{
    main_align_ = LV_FLEX_ALIGN_CENTER;
    cross_align_ = LV_FLEX_ALIGN_CENTER;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::backgroundColor(uint32_t color)
{
    bg_color_ = color;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::troughColor(uint32_t color)
{
    trough_color_ = color;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::glowColor(uint32_t color)
{
    glow_color_ = color;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::textColor(uint32_t color)
{
    text_color_ = color;
    return *this;
}

LVGLBuilder::ActionButtonBuilder& LVGLBuilder::ActionButtonBuilder::callback(
    lv_event_cb_t cb, void* user_data)
{
    callback_ = cb;
    user_data_ = user_data;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::ActionButtonBuilder::build()
{
    if (!parent_) {
        std::string error = "ActionButtonBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    auto result = createActionButton();
    if (result.isError()) {
        return result;
    }

    spdlog::debug(
        "ActionButtonBuilder: Successfully created action button '{}' ({}x{}, mode={})",
        text_,
        width_,
        height_,
        mode_ == ActionMode::Toggle ? "toggle" : "push");

    return Result<lv_obj_t*, std::string>::okay(container_);
}

lv_obj_t* LVGLBuilder::ActionButtonBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("ActionButtonBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

Result<lv_obj_t*, std::string> LVGLBuilder::ActionButtonBuilder::createActionButton()
{
    // Create outer container (the trough).
    container_ = lv_obj_create(parent_);
    if (!container_) {
        std::string error = "ActionButtonBuilder: Failed to create container";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Style the trough.
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_bg_color(container_, lv_color_hex(trough_color_), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container_, 8, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, trough_padding_, 0);

    // Remove scrollbars from container.
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Create inner button face.
    button_ = lv_btn_create(container_);
    if (!button_) {
        std::string error = "ActionButtonBuilder: Failed to create button";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Button fills the container minus padding.
    lv_obj_set_size(button_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(button_);

    // Style the button face.
    lv_obj_set_style_bg_color(button_, lv_color_hex(bg_color_), 0);
    lv_obj_set_style_bg_opa(button_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button_, 6, 0);
    lv_obj_set_style_border_width(button_, 0, 0);

    // Pressed state - slightly darker.
    lv_obj_set_style_bg_color(button_, lv_color_hex(bg_color_ - 0x101010), LV_STATE_PRESSED);

    // No shadow by default (off state).
    lv_obj_set_style_shadow_width(button_, 0, 0);
    lv_obj_set_style_shadow_spread(button_, 0, 0);

    // Set up flex layout for icon + text (configurable).
    lv_obj_set_flex_flow(button_, layout_flow_);
    lv_obj_set_flex_align(button_, main_align_, cross_align_, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(button_, 8, 0);
    if (layout_flow_ == LV_FLEX_FLOW_ROW) {
        lv_obj_set_style_pad_column(button_, 8, 0);
    }
    else {
        lv_obj_set_style_pad_row(button_, 2, 0);
    }

    auto createIconLabel = [&]() {
        if (icon_.empty()) {
            return;
        }
        icon_label_ = lv_label_create(button_);
        if (icon_label_) {
            lv_label_set_text(icon_label_, icon_.c_str());
            lv_obj_set_style_text_color(icon_label_, lv_color_hex(text_color_), 0);
            const lv_font_t* icon_font = font_ ? font_ : &lv_font_montserrat_40;
            lv_obj_set_style_text_font(icon_label_, icon_font, 0);
        }
    };

    auto createTextLabel = [&]() {
        if (text_.empty()) {
            return;
        }
        label_ = lv_label_create(button_);
        if (label_) {
            lv_label_set_text(label_, text_.c_str());
            lv_obj_set_style_text_color(label_, lv_color_hex(text_color_), 0);
            bool use_small_font = !icon_.empty() || text_.length() > 8;
            lv_obj_set_style_text_font(
                label_, use_small_font ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(label_, width_ - trough_padding_ * 2 - 12);
        }
    };

    if (layout_flow_ == LV_FLEX_FLOW_ROW && icon_trailing_) {
        createTextLabel();
        createIconLabel();
    }
    else {
        createIconLabel();
        createTextLabel();
    }

    // Allocate and store state for toggle behavior.
    ActionButtonState* state = new ActionButtonState{
        .is_toggle = (mode_ == ActionMode::Toggle),
        .is_checked = initial_checked_,
        .glow_color = glow_color_,
        .button = button_,
        .icon_label = icon_label_,
        .label = text_.empty() ? (icon_.empty() ? "ActionButton" : icon_) : text_,
        .user_callback = nullptr, // Not used - we register user callback separately.
        .user_data = nullptr
    };
    lv_obj_set_user_data(container_, state);

    // Add our internal click handler for toggle behavior.
    lv_obj_add_event_cb(button_, onButtonClicked, LV_EVENT_CLICKED, container_);

    // Add user's callback separately with their user_data.
    if (callback_) {
        lv_obj_add_event_cb(button_, callback_, LV_EVENT_CLICKED, user_data_);
    }

    // Add cleanup handler for when container is deleted.
    lv_obj_add_event_cb(
        container_,
        [](lv_event_t* e) {
            lv_obj_t* cont = static_cast<lv_obj_t*>(lv_event_get_target(e));
            ActionButtonState* st = static_cast<ActionButtonState*>(lv_obj_get_user_data(cont));
            delete st;
        },
        LV_EVENT_DELETE,
        nullptr);

    // Apply initial checked style if toggle mode and initially checked.
    if (mode_ == ActionMode::Toggle && initial_checked_) {
        applyCheckedStyle(true);
    }

    // Allow touch to cancel by dragging away.
    lv_obj_remove_flag(button_, LV_OBJ_FLAG_PRESS_LOCK);

    return Result<lv_obj_t*, std::string>::okay(container_);
}

void LVGLBuilder::ActionButtonBuilder::onButtonClicked(lv_event_t* e)
{
    lv_obj_t* container = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (!container) return;

    ActionButtonState* state = static_cast<ActionButtonState*>(lv_obj_get_user_data(container));
    if (!state) return;

    if (state->is_toggle) {
        // Toggle the state.
        state->is_checked = !state->is_checked;

        // Apply visual update.
        if (state->is_checked) {
            // ON: Add glow shadow.
            lv_obj_set_style_shadow_color(state->button, lv_color_hex(state->glow_color), 0);
            lv_obj_set_style_shadow_width(state->button, 15, 0);
            lv_obj_set_style_shadow_spread(state->button, 3, 0);
            lv_obj_set_style_shadow_opa(state->button, LV_OPA_80, 0);
        }
        else {
            // OFF: Remove glow.
            lv_obj_set_style_shadow_width(state->button, 0, 0);
            lv_obj_set_style_shadow_spread(state->button, 0, 0);
        }
    }

    if (state->is_toggle) {
        LOG_INFO(Controls, "Action button '{}' {}", state->label, state->is_checked ? "on" : "off");
    }
    else {
        LOG_INFO(Controls, "Action button '{}' clicked", state->label);
    }
    // User callback is registered separately and will be called by LVGL after this handler.
}

void LVGLBuilder::ActionButtonBuilder::applyCheckedStyle(bool checked)
{
    if (!button_) return;

    if (checked) {
        // ON: Add glow shadow.
        lv_obj_set_style_shadow_color(button_, lv_color_hex(glow_color_), 0);
        lv_obj_set_style_shadow_width(button_, 15, 0);
        lv_obj_set_style_shadow_spread(button_, 3, 0);
        lv_obj_set_style_shadow_opa(button_, LV_OPA_80, 0);
    }
    else {
        // OFF: Remove glow.
        lv_obj_set_style_shadow_width(button_, 0, 0);
        lv_obj_set_style_shadow_spread(button_, 0, 0);
    }
}

void LVGLBuilder::ActionButtonBuilder::setChecked(lv_obj_t* container, bool checked)
{
    if (!container) return;

    ActionButtonState* state = static_cast<ActionButtonState*>(lv_obj_get_user_data(container));
    if (!state || !state->is_toggle) return;

    state->is_checked = checked;

    if (checked) {
        lv_obj_set_style_shadow_color(state->button, lv_color_hex(state->glow_color), 0);
        lv_obj_set_style_shadow_width(state->button, 15, 0);
        lv_obj_set_style_shadow_spread(state->button, 3, 0);
        lv_obj_set_style_shadow_opa(state->button, LV_OPA_80, 0);
    }
    else {
        lv_obj_set_style_shadow_width(state->button, 0, 0);
        lv_obj_set_style_shadow_spread(state->button, 0, 0);
    }
}

bool LVGLBuilder::ActionButtonBuilder::isChecked(lv_obj_t* container)
{
    if (!container) return false;

    ActionButtonState* state = static_cast<ActionButtonState*>(lv_obj_get_user_data(container));
    if (!state) return false;

    return state->is_checked;
}

void LVGLBuilder::ActionButtonBuilder::setIcon(lv_obj_t* container, const char* symbol)
{
    if (!container || !symbol) return;

    ActionButtonState* state = static_cast<ActionButtonState*>(lv_obj_get_user_data(container));
    if (!state || !state->icon_label) return;

    lv_label_set_text(state->icon_label, symbol);
}

// ============================================================================
// ActionDropdownBuilder Implementation
// ============================================================================

struct ActionDropdownLogData {
    std::string label;
};

static void actionDropdownLogCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    auto* data = static_cast<ActionDropdownLogData*>(lv_event_get_user_data(e));
    if (!data) {
        return;
    }

    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    char buf[64];
    lv_dropdown_get_selected_str(dropdown, buf, sizeof(buf));
    const uint16_t index = lv_dropdown_get_selected(dropdown);
    LOG_INFO(Controls, "Dropdown '{}' set to '{}' ({})", data->label, buf, index);
}

static void actionDropdownLogDeleteCallback(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }
    auto* data = static_cast<ActionDropdownLogData*>(lv_event_get_user_data(e));
    delete data;
}

LVGLBuilder::ActionDropdownBuilder::ActionDropdownBuilder(lv_obj_t* parent)
    : parent_(parent), container_(nullptr), dropdown_(nullptr), label_(nullptr)
{}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::options(const char* opts)
{
    if (opts) {
        options_ = opts;
    }
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::selected(uint16_t index)
{
    selected_index_ = index;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::label(const char* text)
{
    if (text) {
        label_text_ = text;
    }
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::width(int w)
{
    width_ = w;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::dropdownWidth(int w)
{
    dropdown_width_ = w;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::troughPadding(int px)
{
    trough_padding_ = px;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::backgroundColor(
    uint32_t color)
{
    bg_color_ = color;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::troughColor(uint32_t color)
{
    trough_color_ = color;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::textColor(uint32_t color)
{
    text_color_ = color;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::labelColor(uint32_t color)
{
    label_color_ = color;
    return *this;
}

LVGLBuilder::ActionDropdownBuilder& LVGLBuilder::ActionDropdownBuilder::callback(
    lv_event_cb_t cb, void* user_data)
{
    callback_ = cb;
    user_data_ = user_data;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::ActionDropdownBuilder::build()
{
    if (!parent_) {
        std::string error = "ActionDropdownBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    auto result = createActionDropdown();
    if (result.isError()) {
        return result;
    }

    spdlog::debug("ActionDropdownBuilder: Successfully created action dropdown");

    return Result<lv_obj_t*, std::string>::okay(container_);
}

lv_obj_t* LVGLBuilder::ActionDropdownBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("ActionDropdownBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

Result<lv_obj_t*, std::string> LVGLBuilder::ActionDropdownBuilder::createActionDropdown()
{
    // Create outer container (the trough).
    container_ = lv_obj_create(parent_);
    if (!container_) {
        std::string error = "ActionDropdownBuilder: Failed to create container";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Style the trough.
    lv_obj_set_size(container_, width_, Style::ACTION_SIZE);
    lv_obj_set_style_bg_color(container_, lv_color_hex(trough_color_), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container_, 8, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, trough_padding_, 0);

    // Use row layout for label + dropdown.
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container_, 8, 0);

    // Remove scrollbars.
    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Create label if provided.
    if (!label_text_.empty()) {
        label_ = lv_label_create(container_);
        if (label_) {
            lv_label_set_text(label_, label_text_.c_str());
            lv_obj_set_style_text_color(label_, lv_color_hex(label_color_), 0);
            lv_obj_set_style_text_font(label_, &lv_font_montserrat_14, 0);
        }
    }

    // Create dropdown.
    dropdown_ = lv_dropdown_create(container_);
    if (!dropdown_) {
        std::string error = "ActionDropdownBuilder: Failed to create dropdown";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Set dropdown options and selection.
    if (!options_.empty()) {
        lv_dropdown_set_options(dropdown_, options_.c_str());
    }
    lv_dropdown_set_selected(dropdown_, selected_index_);

    // Size the dropdown.
    if (dropdown_width_ > 0) {
        lv_obj_set_width(dropdown_, dropdown_width_);
    }
    else {
        // Flex grow to fill available space.
        lv_obj_set_flex_grow(dropdown_, 1);
    }
    lv_obj_set_height(dropdown_, Style::ACTION_SIZE - (Style::TROUGH_PADDING * 2));

    // Style the dropdown button.
    lv_obj_set_style_bg_color(dropdown_, lv_color_hex(bg_color_), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown_, lv_color_hex(text_color_), LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown_, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(dropdown_, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(dropdown_, 8, LV_PART_MAIN);

    // Style the dropdown list (popup).
    lv_obj_t* list = lv_dropdown_get_list(dropdown_);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(bg_color_), LV_PART_MAIN);
        lv_obj_set_style_text_color(list, lv_color_hex(text_color_), LV_PART_MAIN);
        lv_obj_set_style_border_color(list, lv_color_hex(trough_color_), LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 6, LV_PART_MAIN);

        // Selected item styling.
        lv_obj_set_style_bg_color(list, lv_color_hex(0x0066CC), LV_PART_SELECTED);
        lv_obj_set_style_text_color(list, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
    }

    // Store dropdown pointer in container for static helpers.
    lv_obj_set_user_data(container_, dropdown_);

    // Add callback if provided.
    if (callback_) {
        lv_obj_add_event_cb(dropdown_, callback_, LV_EVENT_VALUE_CHANGED, user_data_);
    }

    const std::string label = label_text_.empty() ? "Dropdown" : label_text_;
    auto* logData = new ActionDropdownLogData{ label };
    lv_obj_add_event_cb(dropdown_, actionDropdownLogCallback, LV_EVENT_VALUE_CHANGED, logData);
    lv_obj_add_event_cb(dropdown_, actionDropdownLogDeleteCallback, LV_EVENT_DELETE, logData);

    return Result<lv_obj_t*, std::string>::okay(container_);
}

uint16_t LVGLBuilder::ActionDropdownBuilder::getSelected(lv_obj_t* container)
{
    if (!container) return 0;

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_obj_get_user_data(container));
    if (!dropdown) return 0;

    return lv_dropdown_get_selected(dropdown);
}

void LVGLBuilder::ActionDropdownBuilder::setSelected(lv_obj_t* container, uint16_t index)
{
    if (!container) return;

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_obj_get_user_data(container));
    if (!dropdown) return;

    lv_dropdown_set_selected(dropdown, index);
}

LVGLBuilder::ActionDropdownBuilder LVGLBuilder::actionDropdown(lv_obj_t* parent)
{
    return ActionDropdownBuilder(parent);
}

// ============================================================================
// ActionStepperBuilder Implementation
// ============================================================================

struct ActionStepperState {
    lv_obj_t* valueLabel;
    lv_obj_t* container;
    int32_t value;
    int32_t min;
    int32_t max;
    int32_t step;
    double scale;
    std::string format;
    std::string label;

    lv_timer_t* repeatTimer;
    bool isIncrementing;
    bool initialDelayPassed;
    bool loggedThisPress;

    static constexpr uint32_t INITIAL_DELAY_MS = 400;
    static constexpr uint32_t REPEAT_INTERVAL_MS = 80;
};

// Helper to get ActionStepperState from container widget.
// State is stored in valueLabel's user_data (not container's) to leave
// container's user_data available for the caller's use (e.g., PhysicsPanel*).
static ActionStepperState* getStepperStateFromContainer(lv_obj_t* container)
{
    if (!container) return nullptr;

    // Widget structure: container -> [minusBtn, centerSection, plusBtn]
    // centerSection -> [labelObj (optional), valueLabel]
    lv_obj_t* centerSection = lv_obj_get_child(container, 1);
    if (!centerSection) return nullptr;

    // valueLabel is always the last child of centerSection.
    uint32_t childCount = lv_obj_get_child_count(centerSection);
    if (childCount == 0) return nullptr;

    lv_obj_t* valueLabel = lv_obj_get_child(centerSection, childCount - 1);
    if (!valueLabel) return nullptr;

    return static_cast<ActionStepperState*>(lv_obj_get_user_data(valueLabel));
}

static void stepperApplyDelta(ActionStepperState* state, int32_t delta)
{
    int32_t oldValue = state->value;
    state->value = std::clamp(state->value + delta, state->min, state->max);

    if (state->value == oldValue) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), state->format.c_str(), state->value * state->scale);
    lv_label_set_text(state->valueLabel, buf);

    if (state->container) {
        lv_obj_send_event(state->container, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

static void stepperRepeatTimerCallback(lv_timer_t* timer)
{
    auto* state = static_cast<ActionStepperState*>(lv_timer_get_user_data(timer));
    if (!state) return;

    int32_t delta = state->isIncrementing ? state->step : -state->step;
    stepperApplyDelta(state, delta);

    if (!state->initialDelayPassed) {
        state->initialDelayPassed = true;
        lv_timer_set_period(timer, ActionStepperState::REPEAT_INTERVAL_MS);
    }
}

static void stepperStopRepeat(ActionStepperState* state)
{
    if (state->repeatTimer) {
        lv_timer_delete(state->repeatTimer);
        state->repeatTimer = nullptr;
    }
}

static void onStepperPressed(lv_event_t* e, bool increment)
{
    auto* state = static_cast<ActionStepperState*>(lv_event_get_user_data(e));
    if (!state) return;

    stepperStopRepeat(state);
    state->isIncrementing = increment;
    state->initialDelayPassed = false;
    state->loggedThisPress = false;
    state->repeatTimer =
        lv_timer_create(stepperRepeatTimerCallback, ActionStepperState::INITIAL_DELAY_MS, state);
}

static void onStepperReleased(lv_event_t* e)
{
    auto* state = static_cast<ActionStepperState*>(lv_event_get_user_data(e));
    if (!state) return;
    stepperStopRepeat(state);
    if (state->loggedThisPress) {
        return;
    }
    state->loggedThisPress = true;

    char buf[32];
    snprintf(buf, sizeof(buf), state->format.c_str(), state->value * state->scale);
    const std::string label = state->label.empty() ? "Stepper" : state->label;
    if (state->isIncrementing) {
        LOG_INFO(Controls, "Stepper '{}' incremented to {}", label, buf);
    }
    else {
        LOG_INFO(Controls, "Stepper '{}' decremented to {}", label, buf);
    }
}

LVGLBuilder::ActionStepperBuilder::ActionStepperBuilder(lv_obj_t* parent)
    : parent_(parent),
      container_(nullptr),
      minusBtn_(nullptr),
      plusBtn_(nullptr),
      labelObj_(nullptr),
      valueObj_(nullptr)
{}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::label(const char* text)
{
    if (text) {
        label_text_ = text;
    }
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::range(
    int32_t min, int32_t max)
{
    min_ = min;
    max_ = max;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::step(int32_t stepSize)
{
    step_ = stepSize;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::value(int32_t initialValue)
{
    value_ = initialValue;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::valueFormat(const char* fmt)
{
    if (fmt) {
        value_format_ = fmt;
    }
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::valueScale(double scale)
{
    value_scale_ = scale;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::width(int w)
{
    width_ = w;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::height(int h)
{
    height_ = h;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::backgroundColor(
    uint32_t color)
{
    bg_color_ = color;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::troughColor(uint32_t color)
{
    trough_color_ = color;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::textColor(uint32_t color)
{
    text_color_ = color;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::buttonColor(uint32_t color)
{
    button_color_ = color;
    return *this;
}

LVGLBuilder::ActionStepperBuilder& LVGLBuilder::ActionStepperBuilder::callback(
    lv_event_cb_t cb, void* user_data)
{
    callback_ = cb;
    user_data_ = user_data;
    return *this;
}

Result<lv_obj_t*, std::string> LVGLBuilder::ActionStepperBuilder::build()
{
    if (!parent_) {
        std::string error = "ActionStepperBuilder: parent cannot be null";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    auto result = createActionStepper();
    if (result.isError()) {
        return result;
    }

    spdlog::debug("ActionStepperBuilder: Successfully created action stepper");

    return Result<lv_obj_t*, std::string>::okay(container_);
}

lv_obj_t* LVGLBuilder::ActionStepperBuilder::buildOrLog()
{
    auto result = build();
    if (result.isError()) {
        spdlog::error("ActionStepperBuilder::buildOrLog failed: {}", result.errorValue());
        return nullptr;
    }
    return result.value();
}

Result<lv_obj_t*, std::string> LVGLBuilder::ActionStepperBuilder::createActionStepper()
{
    // Create outer container (the trough).
    container_ = lv_obj_create(parent_);
    if (!container_) {
        std::string error = "ActionStepperBuilder: Failed to create container";
        spdlog::error(error);
        return Result<lv_obj_t*, std::string>::error(error);
    }

    // Style the trough.
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_bg_color(container_, lv_color_hex(trough_color_), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container_, 8, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, Style::TROUGH_PADDING, 0);

    // Row layout for the three sections.
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container_, Style::TROUGH_PADDING, 0);

    lv_obj_set_scrollbar_mode(container_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    // Calculate button size (square, height minus padding).
    int btnSize = height_ - (Style::TROUGH_PADDING * 2);

    // Create state structure.
    const std::string label = label_text_.empty() ? "Stepper" : label_text_;
    auto* state = new ActionStepperState{ nullptr, container_,   value_,        min_,  max_,
                                          step_,   value_scale_, value_format_, label, nullptr,
                                          false,   false,        false };

    // --- Minus button ---
    minusBtn_ = lv_btn_create(container_);
    lv_obj_set_size(minusBtn_, btnSize, btnSize);
    lv_obj_set_style_bg_color(minusBtn_, lv_color_hex(button_color_), 0);
    lv_obj_set_style_bg_opa(minusBtn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(minusBtn_, 6, 0);
    lv_obj_set_style_border_width(minusBtn_, 0, 0);
    lv_obj_set_style_shadow_width(minusBtn_, 0, 0);
    lv_obj_set_style_bg_color(minusBtn_, lv_color_hex(0x606060), LV_STATE_PRESSED);

    lv_obj_t* minusLabel = lv_label_create(minusBtn_);
    lv_label_set_text(minusLabel, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(minusLabel, lv_color_hex(text_color_), 0);
    lv_obj_set_style_text_font(minusLabel, &lv_font_montserrat_20, 0);
    lv_obj_center(minusLabel);

    lv_obj_add_event_cb(minusBtn_, onMinusClicked, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(
        minusBtn_, [](lv_event_t* e) { onStepperPressed(e, false); }, LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(minusBtn_, onStepperReleased, LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(minusBtn_, onStepperReleased, LV_EVENT_PRESS_LOST, state);

    // --- Center section (label + value) ---
    lv_obj_t* centerSection = lv_obj_create(container_);
    lv_obj_set_flex_grow(centerSection, 1);
    lv_obj_set_height(centerSection, btnSize);
    lv_obj_set_style_bg_color(centerSection, lv_color_hex(bg_color_), 0);
    lv_obj_set_style_bg_opa(centerSection, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(centerSection, 6, 0);
    lv_obj_set_style_border_width(centerSection, 0, 0);
    lv_obj_set_style_pad_all(centerSection, 4, 0);

    lv_obj_set_flex_flow(centerSection, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        centerSection, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(centerSection, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(centerSection, LV_OBJ_FLAG_SCROLLABLE);

    // Label (if provided).
    if (!label_text_.empty()) {
        labelObj_ = lv_label_create(centerSection);
        lv_label_set_text(labelObj_, label_text_.c_str());
        lv_obj_set_style_text_color(labelObj_, lv_color_hex(text_color_), 0);
        lv_obj_set_style_text_font(labelObj_, &lv_font_montserrat_14, 0);
    }

    // Value display.
    valueObj_ = lv_label_create(centerSection);
    lv_obj_set_style_text_color(valueObj_, lv_color_hex(text_color_), 0);
    lv_obj_set_style_text_font(valueObj_, &lv_font_montserrat_20, 0);
    state->valueLabel = valueObj_;

    // Set initial value display.
    char buf[32];
    snprintf(buf, sizeof(buf), value_format_.c_str(), value_ * value_scale_);
    lv_label_set_text(valueObj_, buf);

    // --- Plus button ---
    plusBtn_ = lv_btn_create(container_);
    lv_obj_set_size(plusBtn_, btnSize, btnSize);
    lv_obj_set_style_bg_color(plusBtn_, lv_color_hex(button_color_), 0);
    lv_obj_set_style_bg_opa(plusBtn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(plusBtn_, 6, 0);
    lv_obj_set_style_border_width(plusBtn_, 0, 0);
    lv_obj_set_style_shadow_width(plusBtn_, 0, 0);
    lv_obj_set_style_bg_color(plusBtn_, lv_color_hex(0x606060), LV_STATE_PRESSED);

    lv_obj_t* plusLabel = lv_label_create(plusBtn_);
    lv_label_set_text(plusLabel, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(plusLabel, lv_color_hex(text_color_), 0);
    lv_obj_set_style_text_font(plusLabel, &lv_font_montserrat_20, 0);
    lv_obj_center(plusLabel);

    lv_obj_add_event_cb(plusBtn_, onPlusClicked, LV_EVENT_CLICKED, state);
    lv_obj_add_event_cb(
        plusBtn_, [](lv_event_t* e) { onStepperPressed(e, true); }, LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(plusBtn_, onStepperReleased, LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(plusBtn_, onStepperReleased, LV_EVENT_PRESS_LOST, state);

    // Store state in valueLabel's user_data (not container's) so that
    // container's user_data remains available for the caller's use.
    lv_obj_set_user_data(state->valueLabel, state);

    // Register user callback on container (if provided).
    if (callback_) {
        lv_obj_add_event_cb(container_, callback_, LV_EVENT_VALUE_CHANGED, user_data_);
    }

    // Cleanup callback.
    lv_obj_add_event_cb(
        container_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
            lv_obj_t* container = static_cast<lv_obj_t*>(lv_event_get_target(e));
            auto* st = getStepperStateFromContainer(container);
            if (st) {
                if (st->repeatTimer) {
                    lv_timer_delete(st->repeatTimer);
                }
                delete st;
            }
        },
        LV_EVENT_DELETE,
        nullptr);

    return Result<lv_obj_t*, std::string>::okay(container_);
}

void LVGLBuilder::ActionStepperBuilder::onMinusClicked(lv_event_t* e)
{
    auto* state = static_cast<ActionStepperState*>(lv_event_get_user_data(e));
    if (!state) return;
    if (state->initialDelayPassed) return;
    stepperApplyDelta(state, -state->step);
}

void LVGLBuilder::ActionStepperBuilder::onPlusClicked(lv_event_t* e)
{
    auto* state = static_cast<ActionStepperState*>(lv_event_get_user_data(e));
    if (!state) return;
    if (state->initialDelayPassed) return;
    stepperApplyDelta(state, state->step);
}

int32_t LVGLBuilder::ActionStepperBuilder::getValue(lv_obj_t* container)
{
    auto* state = getStepperStateFromContainer(container);
    if (!state) return 0;

    return state->value;
}

void LVGLBuilder::ActionStepperBuilder::setValue(lv_obj_t* container, int32_t value)
{
    auto* state = getStepperStateFromContainer(container);
    if (!state) return;

    // Clamp value to range.
    state->value = std::clamp(value, state->min, state->max);

    // Update display.
    char buf[32];
    snprintf(buf, sizeof(buf), state->format.c_str(), state->value * state->scale);
    lv_label_set_text(state->valueLabel, buf);
}

void LVGLBuilder::ActionStepperBuilder::setStep(lv_obj_t* container, int32_t stepSize)
{
    auto* state = getStepperStateFromContainer(container);
    if (!state) return;
    if (stepSize <= 0) return;

    state->step = stepSize;
}

LVGLBuilder::ActionStepperBuilder LVGLBuilder::actionStepper(lv_obj_t* parent)
{
    return ActionStepperBuilder(parent);
}
