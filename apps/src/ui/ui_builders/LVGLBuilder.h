#pragma once

#include "core/Result.h"
#include "lvgl/lvgl.h"
#include "ui/controls/IconRail.h"
#include <cmath>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

/**
 * LVGLBuilder provides a fluent interface for creating LVGL UI elements
 * with reduced boilerplate and consistent patterns.
 *
 * Example usage:
 *   auto slider = LVGLBuilder::slider(parent)
 *       .position(100, 50)
 *       .size(200, 10)
 *       .range(0, 100)
 *       .value(50)
 *       .label("Volume")
 *       .valueLabel("%.0f")
 *       .callback(volumeCallback, userData)
 *       .build();
 */
class LVGLBuilder {
public:
    // Forward declaration for position specification.
    struct Position {
        int x, y;
        lv_align_t align;

        Position(int x, int y, lv_align_t align = LV_ALIGN_TOP_LEFT) : x(x), y(y), align(align) {}
    };

    // Forward declaration for size specification.
    struct Size {
        int width, height;

        Size(int width, int height) : width(width), height(height) {}
    };

    /**
     * Shared style constants for consistent control sizing.
     * Optimized for touch screens (HyperPixel 4.0: 800x480).
     */
    struct Style {
        // Control dimensions.
        static constexpr int ACTION_SIZE = 80;
        static constexpr int CONTROL_WIDTH = LV_PCT(80);
        static constexpr int RADIUS = 8;

        // Slider dimensions.
        static constexpr int SLIDER_TRACK_HEIGHT = 15;
        static constexpr int SLIDER_KNOB_SIZE = 30;
        static constexpr int SLIDER_KNOB_RADIUS = 15;

        // Switch dimensions.
        static constexpr int SWITCH_WIDTH = 48;
        static constexpr int SWITCH_HEIGHT = 32;

        // Button colors.
        static constexpr uint32_t BUTTON_BG_COLOR = 0x505050;      // Default button background.
        static constexpr uint32_t BUTTON_PRESSED_COLOR = 0x606060; // Button pressed state.
        static constexpr uint32_t BUTTON_TEXT_COLOR = 0xFFFFFF;    // Button text color.

        // Fonts.
        static inline const lv_font_t* CONTROL_FONT = &lv_font_montserrat_16;

        // Padding.
        static constexpr int PAD_HORIZONTAL = 10; // Left/right padding inside controls.
        static constexpr int PAD_VERTICAL = 8;    // Top/bottom padding inside controls.
        static constexpr int GAP = 8;             // Gap between elements inside controls.
        static constexpr int TROUGH_PADDING = 4;  // Padding inside trough containers.

        // Trough colors (for ActionButton, ActionDropdown, etc.).
        static constexpr uint32_t TROUGH_COLOR = 0x202020;       // Dark inset trough.
        static constexpr uint32_t TROUGH_INNER_COLOR = 0x404040; // Inner element background.
    };

    /**
     * SliderBuilder - Fluent interface for creating sliders with labels and callbacks.
     */
    class SliderBuilder {
    public:
        explicit SliderBuilder(lv_obj_t* parent);

        // Core slider configuration.
        SliderBuilder& size(int width, int height = 10);
        SliderBuilder& size(const Size& sz);
        SliderBuilder& position(int x, int y, lv_align_t align = LV_ALIGN_TOP_LEFT);
        SliderBuilder& position(const Position& pos);
        SliderBuilder& range(int min, int max);
        SliderBuilder& value(int initial_value);

        // Label configuration.
        SliderBuilder& label(const char* text, int offset_x = 0, int offset_y = -20);
        SliderBuilder& valueLabel(
            const char* format = "%.1f", int offset_x = 110, int offset_y = -20);

        // Value transformation for display.
        SliderBuilder& valueTransform(std::function<double(int32_t)> transform);

        // Event handling.
        SliderBuilder& callback(lv_event_cb_t cb, void* user_data = nullptr);
        SliderBuilder& callback(
            lv_event_cb_t cb, std::function<void*(lv_obj_t*)> callback_data_factory);
        SliderBuilder& events(lv_event_code_t event_code = LV_EVENT_ALL);

        // Build the final slider (returns the slider object, not the container).
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns slider or nullptr).
        lv_obj_t* buildOrLog();

        // Access to created objects after build() for advanced use cases.
        lv_obj_t* getSlider() const { return slider_; }
        lv_obj_t* getLabel() const { return label_; }
        lv_obj_t* getValueLabel() const { return value_label_; }

    private:
        lv_obj_t* parent_;
        lv_obj_t* slider_;
        lv_obj_t* label_;
        lv_obj_t* value_label_;

        // Configuration storage.
        Size size_;
        Position position_;
        int min_value_, max_value_;
        int initial_value_;
        lv_event_cb_t callback_;
        void* user_data_;
        std::function<void*(lv_obj_t*)> callback_data_factory_;
        bool use_factory_;
        lv_event_code_t event_code_;

        // Label configuration.
        std::string label_text_;
        Position label_position_;
        bool has_label_;

        std::string value_format_;
        Position value_label_position_;
        bool has_value_label_;
        std::function<double(int32_t)> value_transform_;

        // Structure to hold value label update data.
        struct ValueLabelData {
            lv_obj_t* value_label;
            std::string format;
            std::function<double(int32_t)> transform;
        };

        Result<lv_obj_t*, std::string> createSlider();
        void createLabel();
        void createValueLabel();
        void setupEvents();

        // Static callbacks.
        static void valueUpdateCallback(lv_event_t* e);
        static void sliderDeleteCallback(lv_event_t* e);
    };

    /**
     * ButtonBuilder - Fluent interface for creating buttons with text and callbacks.
     */
    class ButtonBuilder {
    public:
        explicit ButtonBuilder(lv_obj_t* parent);

        // Core button configuration.
        ButtonBuilder& size(int width, int height);
        ButtonBuilder& size(const Size& sz);
        ButtonBuilder& position(int x, int y, lv_align_t align = LV_ALIGN_TOP_LEFT);
        ButtonBuilder& position(const Position& pos);
        ButtonBuilder& text(const char* text);

        // Styling.
        ButtonBuilder& backgroundColor(uint32_t color);
        ButtonBuilder& pressedColor(uint32_t color);
        ButtonBuilder& textColor(uint32_t color);
        ButtonBuilder& radius(int px);
        ButtonBuilder& font(const lv_font_t* font);
        ButtonBuilder& icon(const char* symbol); // Prefix icon (LV_SYMBOL_* or emoji).

        // Button behavior.
        ButtonBuilder& toggle(bool enabled = true);
        ButtonBuilder& checkable(bool enabled = true);

        // Event handling.
        ButtonBuilder& callback(lv_event_cb_t cb, void* user_data = nullptr);
        ButtonBuilder& events(lv_event_code_t event_code = LV_EVENT_CLICKED);

        // Build the final button.
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns button or nullptr).
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getButton() const { return button_; }
        lv_obj_t* getLabel() const { return label_; }

    private:
        lv_obj_t* parent_;
        lv_obj_t* button_;
        lv_obj_t* label_;

        // Configuration storage.
        Size size_;
        Position position_;
        std::string text_;
        std::string icon_;
        bool is_toggle_;
        bool is_checkable_;
        lv_event_cb_t callback_;
        void* user_data_;
        lv_event_code_t event_code_;

        // Styling storage.
        uint32_t bgColor_ = 0;
        uint32_t pressedColor_ = 0;
        uint32_t textColor_ = 0xFFFFFF;
        int radius_ = 0;
        const lv_font_t* font_ = nullptr;
        bool hasBgColor_ = false;
        bool hasPressedColor_ = false;
        bool hasTextColor_ = false;

        Result<lv_obj_t*, std::string> createButton();
        void createLabel();
        void setupBehavior();
        void setupEvents();
    };

    /**
     * LabelBuilder - Simple interface for creating labels.
     */
    class LabelBuilder {
    public:
        explicit LabelBuilder(lv_obj_t* parent);

        LabelBuilder& text(const char* text);
        LabelBuilder& position(int x, int y, lv_align_t align = LV_ALIGN_TOP_LEFT);
        LabelBuilder& position(const Position& pos);

        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns label or nullptr).
        lv_obj_t* buildOrLog();

    private:
        lv_obj_t* parent_;
        std::string text_;
        Position position_;
    };

    /**
     * DropdownBuilder - Interface for creating dropdown widgets.
     */
    class DropdownBuilder {
    public:
        explicit DropdownBuilder(lv_obj_t* parent);

        DropdownBuilder& options(const char* options);
        DropdownBuilder& selected(uint16_t index);
        DropdownBuilder& position(int x, int y, lv_align_t align = LV_ALIGN_TOP_LEFT);
        DropdownBuilder& position(const Position& pos);
        DropdownBuilder& size(int width, int height);
        DropdownBuilder& size(const Size& size);

        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns dropdown or nullptr).
        lv_obj_t* buildOrLog();

    private:
        lv_obj_t* parent_;
        std::string options_;
        uint16_t selectedIndex_ = 0;
        Position position_;
        Size size_;
    };

    /**
     * LabeledSwitchBuilder - Creates a switch with label in horizontal layout.
     */
    class LabeledSwitchBuilder {
    public:
        explicit LabeledSwitchBuilder(lv_obj_t* parent);

        // Configuration.
        LabeledSwitchBuilder& label(const char* text);
        LabeledSwitchBuilder& initialState(bool checked);
        LabeledSwitchBuilder& callback(lv_event_cb_t cb, void* user_data = nullptr);

        // Sizing (defaults: 80% width, 44px height to match buttons).
        LabeledSwitchBuilder& size(int width, int height);
        LabeledSwitchBuilder& height(int h);
        LabeledSwitchBuilder& width(int w);

        // Build the labeled switch (returns the switch object).
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns switch or nullptr).
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getSwitch() const { return switch_; }
        lv_obj_t* getLabel() const { return label_; }
        lv_obj_t* getContainer() const { return container_; }

    private:
        lv_obj_t* parent_;
        lv_obj_t* container_;
        lv_obj_t* switch_;
        lv_obj_t* label_;

        std::string label_text_;
        bool initial_checked_ = false;
        lv_event_cb_t callback_ = nullptr;
        void* user_data_ = nullptr;

        // Sizing (defaults from Style constants).
        int width_ = Style::CONTROL_WIDTH;
        int height_ = Style::ACTION_SIZE;

        Result<lv_obj_t*, std::string> createLabeledSwitch();
    };

    /**
     * @brief ToggleSliderBuilder - Creates a toggle switch + slider combo control.
     *
     * Layout: [Label] [Switch]
     *                [Slider]
     *                [Value]
     */
    class ToggleSliderBuilder {
    public:
        explicit ToggleSliderBuilder(lv_obj_t* parent);

        // Configuration.
        ToggleSliderBuilder& label(const char* text);
        ToggleSliderBuilder& sliderWidth(int width);
        ToggleSliderBuilder& range(int min, int max);
        ToggleSliderBuilder& value(int initialValue);
        ToggleSliderBuilder& defaultValue(int defValue);
        ToggleSliderBuilder& valueScale(double scale);
        ToggleSliderBuilder& valueFormat(const char* format);
        ToggleSliderBuilder& initiallyEnabled(bool enabled);
        ToggleSliderBuilder& onToggle(lv_event_cb_t cb, void* user_data = nullptr);
        ToggleSliderBuilder& onSliderChange(lv_event_cb_t cb, void* user_data = nullptr);

        // Build the toggle slider (returns the container).
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns container or nullptr).
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getContainer() const { return container_; }
        lv_obj_t* getSwitch() const { return switch_; }
        lv_obj_t* getSlider() const { return slider_; }
        lv_obj_t* getLabel() const { return label_; }
        lv_obj_t* getValueLabel() const { return valueLabel_; }

    private:
        lv_obj_t* parent_;
        lv_obj_t* container_;
        lv_obj_t* switch_;
        lv_obj_t* slider_;
        lv_obj_t* label_;
        lv_obj_t* valueLabel_;

        std::string label_text_ = "Feature";
        int slider_width_ = 200;
        int range_min_ = 0;
        int range_max_ = 100;
        int initial_value_ = 0;
        int default_value_ = 50;
        double value_scale_ = 1.0;
        std::string value_format_ = "%.1f";
        bool initially_enabled_ = false;
        lv_event_cb_t toggle_callback_ = nullptr;
        lv_event_cb_t slider_callback_ = nullptr;
        void* toggle_user_data_ = nullptr;
        void* slider_user_data_ = nullptr;

        Result<lv_obj_t*, std::string> createToggleSlider();
    };

    /**
     * @brief IconId - Identifiers for icons in an IconRail.
     */
    enum class IconId { CORE = 0, SCENARIO, GENERAL, PRESSURE, FORCES, TREE, COUNT };

    /**
     * @brief IconConfig - Configuration for a single icon in an IconRail.
     */
    struct IconConfig {
        IconId id;
        const char* symbol;        // LV_SYMBOL_* or text.
        const char* tooltip;       // Description for accessibility.
        uint32_t color = 0xFFFFFF; // Icon color (default white).
    };

    /**
     * @brief ActionMode - Determines button behavior.
     */
    enum class ActionMode {
        Push,  // Momentary action, no latched state.
        Toggle // Latched on/off state.
    };

    /**
     * @brief ActionButtonBuilder - Creates a square button with inset trough and glow effect.
     *
     * Layout: Outer trough container with inner button face.
     * Toggle mode shows colored glow when checked.
     *
     * Visual structure:
     * ┌─────────────────────────┐  ← Outer trough (dark, inset look)
     * │ ░░░░░░░░░░░░░░░░░░░░░░░ │  ← Padding reveals the trough
     * │ ░┌───────────────────┐░ │
     * │ ░│    Button Text    │░ │  ← Inner button face
     * │ ░└───────────────────┘░ │
     * └─────────────────────────┘
     *
     * ON state: Inner button gets colored shadow that glows into trough.
     */
    class ActionButtonBuilder {
    public:
        explicit ActionButtonBuilder(lv_obj_t* parent);

        // Text and icon.
        ActionButtonBuilder& text(const char* text);
        ActionButtonBuilder& icon(const char* symbol); // LV_SYMBOL_* or emoji.
        ActionButtonBuilder& font(const lv_font_t* f); // Custom font for icon/text.

        // Mode and state.
        ActionButtonBuilder& mode(ActionMode m);
        ActionButtonBuilder& checked(bool initial); // Initial state for Toggle mode.

        // Sizing.
        ActionButtonBuilder& size(int dimension); // Square size (width = height).
        ActionButtonBuilder& width(int w);
        ActionButtonBuilder& height(int h);
        ActionButtonBuilder& troughPadding(int px);

        // Layout.
        ActionButtonBuilder& layoutRow();    // Horizontal: icon on left, text on right (default for
                                             // rectangular).
        ActionButtonBuilder& layoutColumn(); // Vertical: icon above text (default for square).
        ActionButtonBuilder& alignLeft();    // Left-align content (useful for row layout).
        ActionButtonBuilder& alignCenter();  // Center-align content (default).

        // Colors.
        ActionButtonBuilder& backgroundColor(uint32_t color); // Button face color.
        ActionButtonBuilder& troughColor(uint32_t color);     // Outer trough color.
        ActionButtonBuilder& glowColor(uint32_t color);       // Glow color when checked.
        ActionButtonBuilder& textColor(uint32_t color);

        // Event handling.
        ActionButtonBuilder& callback(lv_event_cb_t cb, void* user_data = nullptr);

        // Build the action button (returns the outer trough container).
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging.
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getContainer() const { return container_; }
        lv_obj_t* getButton() const { return button_; }
        lv_obj_t* getLabel() const { return label_; }
        lv_obj_t* getIconLabel() const { return icon_label_; }

        // Runtime state control (for Toggle mode).
        static void setChecked(lv_obj_t* container, bool checked);
        static bool isChecked(lv_obj_t* container);

    private:
        lv_obj_t* parent_;
        lv_obj_t* container_;  // Outer trough.
        lv_obj_t* button_;     // Inner button face.
        lv_obj_t* label_;      // Text label.
        lv_obj_t* icon_label_; // Icon label (optional).

        std::string text_;
        std::string icon_;
        const lv_font_t* font_ = nullptr; // Custom font (nullptr = use default).
        ActionMode mode_ = ActionMode::Push;
        bool initial_checked_ = false;
        int width_ = Style::ACTION_SIZE;
        int height_ = Style::ACTION_SIZE;
        int trough_padding_ = Style::TROUGH_PADDING;

        lv_flex_flow_t layout_flow_ = LV_FLEX_FLOW_COLUMN; // Default: vertical.
        lv_flex_align_t main_align_ = LV_FLEX_ALIGN_CENTER;
        lv_flex_align_t cross_align_ = LV_FLEX_ALIGN_CENTER;

        uint32_t bg_color_ = Style::TROUGH_INNER_COLOR;
        uint32_t trough_color_ = Style::TROUGH_COLOR;
        uint32_t glow_color_ = 0x00CC00; // Green glow when on.
        uint32_t text_color_ = 0xFFFFFF;

        lv_event_cb_t callback_ = nullptr;
        void* user_data_ = nullptr;

        Result<lv_obj_t*, std::string> createActionButton();
        static void onButtonClicked(lv_event_t* e);

        // State stored in container's user data.
        struct ActionButtonState {
            bool is_toggle;
            bool is_checked;
            uint32_t glow_color;
            lv_obj_t* button; // Inner button for styling.
            lv_event_cb_t user_callback;
            void* user_data;
        };

        void applyCheckedStyle(bool checked);
    };

    /**
     * @brief ActionDropdownBuilder - Creates a dropdown with trough styling matching ActionButton.
     *
     * Layout: Outer trough container with optional label and styled dropdown.
     *
     * Visual structure:
     * ┌─────────────────────────────┐  ← Outer trough (dark, inset look)
     * │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
     * │ ░ Label:  [Dropdown    ▼] ░ │  ← Label + dropdown inside
     * │ ░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
     * └─────────────────────────────┘
     */
    class ActionDropdownBuilder {
    public:
        explicit ActionDropdownBuilder(lv_obj_t* parent);

        // Dropdown options (newline-separated).
        ActionDropdownBuilder& options(const char* opts);
        ActionDropdownBuilder& selected(uint16_t index);

        // Optional label.
        ActionDropdownBuilder& label(const char* text);

        // Sizing.
        ActionDropdownBuilder& width(int w);         // Total width including trough.
        ActionDropdownBuilder& dropdownWidth(int w); // Width of dropdown itself.
        ActionDropdownBuilder& troughPadding(int px);

        // Colors.
        ActionDropdownBuilder& backgroundColor(uint32_t color); // Dropdown background.
        ActionDropdownBuilder& troughColor(uint32_t color);     // Outer trough color.
        ActionDropdownBuilder& textColor(uint32_t color);
        ActionDropdownBuilder& labelColor(uint32_t color);

        // Event handling.
        ActionDropdownBuilder& callback(lv_event_cb_t cb, void* user_data = nullptr);

        // Build the dropdown (returns the outer trough container).
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging.
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getContainer() const { return container_; }
        lv_obj_t* getDropdown() const { return dropdown_; }
        lv_obj_t* getLabel() const { return label_; }

        // Runtime helpers.
        static uint16_t getSelected(lv_obj_t* container);
        static void setSelected(lv_obj_t* container, uint16_t index);

    private:
        lv_obj_t* parent_;
        lv_obj_t* container_; // Outer trough.
        lv_obj_t* dropdown_;
        lv_obj_t* label_;

        std::string options_;
        std::string label_text_;
        uint16_t selected_index_ = 0;
        int width_ = LV_PCT(90);
        int dropdown_width_ = 0; // 0 = auto (fill available space).
        int trough_padding_ = Style::TROUGH_PADDING;

        uint32_t bg_color_ = Style::TROUGH_INNER_COLOR;
        uint32_t trough_color_ = Style::TROUGH_COLOR;
        uint32_t text_color_ = 0xFFFFFF;
        uint32_t label_color_ = 0xFFFFFF;

        lv_event_cb_t callback_ = nullptr;
        void* user_data_ = nullptr;

        Result<lv_obj_t*, std::string> createActionDropdown();
    };

    /**
     * @brief ActionStepperBuilder - Creates a stepper control with − value + layout.
     *
     * Visual structure (80px tall to match ActionButton):
     * ┌─────────┬─────────────────┬─────────┐
     * │         │      Label      │         │
     * │    −    │      5.0        │    +    │
     * │         │                 │         │
     * └─────────┴─────────────────┴─────────┘
     *
     * Three sections inside one trough: minus button, center label+value, plus button.
     */
    class ActionStepperBuilder {
    public:
        explicit ActionStepperBuilder(lv_obj_t* parent);

        // Label displayed above the value.
        ActionStepperBuilder& label(const char* text);

        // Value range and step.
        ActionStepperBuilder& range(int32_t min, int32_t max);
        ActionStepperBuilder& step(int32_t stepSize);
        ActionStepperBuilder& value(int32_t initialValue);

        // Display formatting.
        ActionStepperBuilder& valueFormat(const char* fmt); // e.g., "%.1f"
        ActionStepperBuilder& valueScale(double scale);     // Display as value * scale.

        // Sizing.
        ActionStepperBuilder& width(int w);
        ActionStepperBuilder& height(int h); // Default 80 to match ActionButton.

        // Colors.
        ActionStepperBuilder& backgroundColor(uint32_t color);
        ActionStepperBuilder& troughColor(uint32_t color);
        ActionStepperBuilder& textColor(uint32_t color);
        ActionStepperBuilder& buttonColor(uint32_t color);

        // Event handling (called on value change).
        ActionStepperBuilder& callback(lv_event_cb_t cb, void* user_data = nullptr);

        // Build the stepper (returns the outer trough container).
        Result<lv_obj_t*, std::string> build();
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getContainer() const { return container_; }

        // Runtime helpers.
        static int32_t getValue(lv_obj_t* container);
        static void setValue(lv_obj_t* container, int32_t value);

    private:
        lv_obj_t* parent_;
        lv_obj_t* container_; // Outer trough.
        lv_obj_t* minusBtn_;
        lv_obj_t* plusBtn_;
        lv_obj_t* labelObj_;
        lv_obj_t* valueObj_;

        std::string label_text_;
        std::string value_format_ = "%.0f";
        int32_t min_ = 0;
        int32_t max_ = 100;
        int32_t step_ = 1;
        int32_t value_ = 0;
        double value_scale_ = 1.0;

        int width_ = LV_PCT(95);
        int height_ = Style::ACTION_SIZE;

        uint32_t bg_color_ = Style::TROUGH_INNER_COLOR;
        uint32_t trough_color_ = Style::TROUGH_COLOR;
        uint32_t text_color_ = 0xFFFFFF;
        uint32_t button_color_ = Style::TROUGH_INNER_COLOR;

        lv_event_cb_t callback_ = nullptr;
        void* user_data_ = nullptr;

        Result<lv_obj_t*, std::string> createActionStepper();
        void updateValueDisplay();

        static void onMinusClicked(lv_event_t* e);
        static void onPlusClicked(lv_event_t* e);
    };

    /**
     * @brief CollapsiblePanelBuilder - Creates a collapsible panel with header and content area.
     *
     * Layout: [▼ Title]
     *         [Content Area]
     *
     * Clicking the header toggles the content visibility with smooth animation.
     */
    class CollapsiblePanelBuilder {
    public:
        explicit CollapsiblePanelBuilder(lv_obj_t* parent);

        // Configuration.
        CollapsiblePanelBuilder& title(const char* text);
        CollapsiblePanelBuilder& size(int width, int height = LV_SIZE_CONTENT);
        CollapsiblePanelBuilder& size(const Size& sz);
        CollapsiblePanelBuilder& initiallyExpanded(bool expanded);
        CollapsiblePanelBuilder& backgroundColor(uint32_t color);
        CollapsiblePanelBuilder& headerColor(uint32_t color);
        CollapsiblePanelBuilder& onToggle(lv_event_cb_t cb, void* user_data = nullptr);

        // Build the collapsible panel (returns the container).
        Result<lv_obj_t*, std::string> build();

        // Build with automatic error logging (returns container or nullptr).
        lv_obj_t* buildOrLog();

        // Access to created objects.
        lv_obj_t* getContainer() const { return container_; }
        lv_obj_t* getHeader() const { return header_; }
        lv_obj_t* getContent() const { return content_; }
        lv_obj_t* getTitleLabel() const { return title_label_; }
        lv_obj_t* getIndicator() const { return indicator_; }

        // Check if currently expanded.
        bool isExpanded() const { return is_expanded_; }

    private:
        lv_obj_t* parent_;
        lv_obj_t* container_;
        lv_obj_t* header_;
        lv_obj_t* content_;
        lv_obj_t* title_label_;
        lv_obj_t* indicator_;

        std::string title_text_ = "Panel";
        Size size_;
        bool is_expanded_ = true;
        uint32_t bg_color_ = 0x303030;
        uint32_t header_color_ = 0x404040;
        lv_event_cb_t toggle_callback_ = nullptr;
        void* user_data_ = nullptr;

        Result<lv_obj_t*, std::string> createCollapsiblePanel();
        static void onHeaderClick(lv_event_t* e);

        // Store state for the collapse/expand animation.
        struct PanelState {
            lv_obj_t* content;
            lv_obj_t* indicator;
            bool is_expanded;
        };
    };

    // Static factory methods for fluent interface.
    static SliderBuilder slider(lv_obj_t* parent);
    static ButtonBuilder button(lv_obj_t* parent);
    static LabelBuilder label(lv_obj_t* parent);
    static DropdownBuilder dropdown(lv_obj_t* parent);
    static LabeledSwitchBuilder labeledSwitch(lv_obj_t* parent);
    static ToggleSliderBuilder toggleSlider(lv_obj_t* parent);
    static CollapsiblePanelBuilder collapsiblePanel(lv_obj_t* parent);
    static ActionButtonBuilder actionButton(lv_obj_t* parent);
    static ActionDropdownBuilder actionDropdown(lv_obj_t* parent);
    static ActionStepperBuilder actionStepper(lv_obj_t* parent);

    // Common value transform functions for sliders.
    struct Transforms {
        // Linear scaling: value * scale
        static std::function<double(int32_t)> Linear(double scale)
        {
            return [scale](int32_t value) { return value * scale; };
        }

        // Exponential scaling: base^(value * scale + offset)
        static std::function<double(int32_t)> Exponential(
            double base, double scale, double offset = 0)
        {
            return [base, scale, offset](int32_t value) {
                return std::pow(base, value * scale + offset);
            };
        }

        // Percentage: value as-is (for 0-100 ranges)
        static std::function<double(int32_t)> Percentage()
        {
            return [](int32_t value) { return static_cast<double>(value); };
        }

        // Logarithmic: log(1 + value * scale)
        static std::function<double(int32_t)> Logarithmic(double scale = 1.0)
        {
            return [scale](int32_t value) { return std::log1p(value * scale); };
        }
    };

    // Utility methods for common positioning patterns.
    static Position topLeft(int x, int y) { return Position(x, y, LV_ALIGN_TOP_LEFT); }
    static Position topRight(int x, int y) { return Position(x, y, LV_ALIGN_TOP_RIGHT); }
    static Position center(int x = 0, int y = 0) { return Position(x, y, LV_ALIGN_CENTER); }

    // Common size presets for consistency.
    static Size sliderSize(int width = 200) { return Size(width, 10); }
    static Size buttonSize(int width = 100, int height = 40) { return Size(width, height); }
    static Size smallButton(int width = 80, int height = 30) { return Size(width, height); }
};
