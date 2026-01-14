#pragma once

#include "lvgl/lvgl.h"
#include <functional>
#include <memory>
#include <string>

namespace DirtSim {
namespace Ui {

/**
 * @brief A toggle switch + slider combo widget.
 *
 * Layout: [Label] [Value] [Switch]
 *                [Slider]
 *
 * Features:
 * - Toggle enables/disables the slider.
 * - Slider auto-enables when grabbed while disabled.
 * - Value is saved/restored when toggling.
 * - setValue() and setEnabled() handle all internal state correctly,
 *   including firing events to update the value label display.
 *
 * This class encapsulates the LVGL widget complexity and prevents the bug
 * where programmatic value changes don't update the label display.
 */
class ToggleSlider {
public:
    /**
     * @brief Callback types for toggle and slider events.
     *
     * The callbacks receive the new state/value directly, not raw LVGL events.
     */
    using ToggleCallback = std::function<void(bool enabled)>;
    using ValueCallback = std::function<void(int value)>;

    /**
     * @brief Builder for fluent construction of ToggleSlider widgets.
     */
    class Builder {
    public:
        explicit Builder(lv_obj_t* parent);

        Builder& label(const char* text);
        Builder& range(int min, int max);
        Builder& value(int initialValue);
        Builder& defaultValue(int defValue);
        Builder& valueScale(double scale);
        Builder& valueFormat(const char* format);
        Builder& initiallyEnabled(bool enabled);
        Builder& sliderWidth(int width);
        Builder& onToggle(ToggleCallback callback);
        Builder& onValueChange(ValueCallback callback);

        /**
         * @brief Build the ToggleSlider.
         * @return Unique pointer to the constructed widget.
         */
        std::unique_ptr<ToggleSlider> build();

    private:
        lv_obj_t* parent_;
        std::string labelText_ = "Feature";
        int rangeMin_ = 0;
        int rangeMax_ = 100;
        int initialValue_ = 0;
        int defaultValue_ = 50;
        double valueScale_ = 1.0;
        std::string valueFormat_ = "%.1f";
        bool initiallyEnabled_ = false;
        int sliderWidth_ = 200;
        ToggleCallback toggleCallback_;
        ValueCallback valueCallback_;
    };

    /**
     * @brief Create a builder for constructing a ToggleSlider.
     * @param parent The parent LVGL object.
     */
    static Builder create(lv_obj_t* parent);

    ~ToggleSlider();

    // Prevent copying.
    ToggleSlider(const ToggleSlider&) = delete;
    ToggleSlider& operator=(const ToggleSlider&) = delete;

    // Allow moving.
    ToggleSlider(ToggleSlider&& other) noexcept;
    ToggleSlider& operator=(ToggleSlider&& other) noexcept;

    /**
     * @brief Set the slider value.
     *
     * This method correctly updates both the slider position and the value label.
     * If the toggle is disabled, it will be auto-enabled.
     *
     * @param value The new slider value (in range units, not scaled).
     */
    void setValue(int value);

    /**
     * @brief Set the enabled state.
     *
     * When enabling, restores the saved value (or default if none saved).
     * When disabling, saves the current value and sets slider to 0.
     *
     * @param enabled Whether the toggle should be enabled.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Get the current slider value.
     * @return The slider value (in range units, not scaled).
     */
    int getValue() const;

    /**
     * @brief Get the scaled value (value * valueScale).
     * @return The scaled value for display/config purposes.
     */
    double getScaledValue() const;

    /**
     * @brief Check if the toggle is currently enabled.
     */
    bool isEnabled() const;

    /**
     * @brief Get the LVGL container object.
     *
     * Use this for layout purposes (adding to parent containers).
     */
    lv_obj_t* getContainer() const { return container_; }

private:
    // Private constructor - use Builder.
    ToggleSlider(
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
        ValueCallback valueCallback);

    void createWidgets();
    void updateValueLabel();
    void updateSliderColors();

    // LVGL callbacks.
    static void onSwitchChanged(lv_event_t* e);
    static void onSliderChanged(lv_event_t* e);
    static void onSliderPressed(lv_event_t* e);

    // LVGL objects.
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* valueLabel_ = nullptr;
    lv_obj_t* switch_ = nullptr;
    lv_obj_t* slider_ = nullptr;

    // Configuration.
    std::string labelText_;
    int rangeMin_;
    int rangeMax_;
    int defaultValue_;
    double valueScale_;
    std::string valueFormat_;
    int sliderWidth_;

    // State.
    int savedValue_ = 0;

    // Callbacks.
    ToggleCallback toggleCallback_;
    ValueCallback valueCallback_;
};

} // namespace Ui
} // namespace DirtSim
