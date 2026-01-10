#include "MarqueeTypes.h"

#include <cstdint>

namespace DirtSim {

std::vector<DigitPlacement> layoutString(
    const std::string& content, int digitWidth, int digitGap, int colonWidth)
{
    std::vector<DigitPlacement> placements;
    double x = 0.0;

    for (const char c : content) {
        if (c >= '0' && c <= '9') {
            placements.push_back({ .c = c, .x = x, .y = 0.0 });
            x += digitWidth;
        }
        else if (c == ':') {
            placements.push_back({ .c = c, .x = x, .y = 0.0 });
            x += colonWidth;
        }
        else if (c == ' ') {
            // Spaces create gaps but aren't drawn.
            x += digitGap;
        }
    }

    return placements;
}

int calculateStringWidth(const std::string& content, int digitWidth, int digitGap, int colonWidth)
{
    int width = 0;

    for (const char c : content) {
        if (c >= '0' && c <= '9') {
            width += digitWidth;
        }
        else if (c == ':') {
            width += colonWidth;
        }
        else if (c == ' ') {
            width += digitGap;
        }
    }

    return width;
}

// ============================================================================
// Effect Functions
// ============================================================================

void startHorizontalScroll(
    HorizontalScrollState& state,
    const std::string& content,
    double visible_width,
    double speed,
    int digit_width,
    int digit_gap,
    int colon_width)
{
    state.viewport_x = 0.0;
    state.content_width = calculateStringWidth(content, digit_width, digit_gap, colon_width);
    state.visible_width = visible_width;
    state.speed = speed;
    state.scrolling_out = true;
    state.digit_width = digit_width;
    state.digit_gap = digit_gap;
    state.colon_width = colon_width;
}

MarqueeFrame updateHorizontalScroll(
    HorizontalScrollState& state, const std::string& content, double deltaTime)
{
    // Advance viewport.
    state.viewport_x += state.speed * deltaTime;

    if (state.scrolling_out) {
        // Scrolling out: content moves left until off-screen.
        if (state.viewport_x >= state.content_width) {
            // Content is fully off-screen left. Teleport to right side.
            state.viewport_x = -state.visible_width;
            state.scrolling_out = false;
        }
    }
    else {
        // Scrolling in: content comes in from right until at position 0.
        if (state.viewport_x >= 0.0) {
            state.viewport_x = 0.0;
        }
    }

    MarqueeFrame frame;
    frame.digits = layoutString(content, state.digit_width, state.digit_gap, state.colon_width);
    frame.viewportX = state.viewport_x;
    frame.finished = !state.scrolling_out && state.viewport_x >= 0.0;

    return frame;
}

// ============================================================================
// Vertical Slide Effect Functions
// ============================================================================

void initVerticalSlide(
    VerticalSlideState& state,
    double speed,
    int digit_width,
    int digit_gap,
    int digit_height,
    int colon_width)
{
    state.speed = speed;
    state.digit_width = digit_width;
    state.digit_gap = digit_gap;
    state.digit_height = digit_height;
    state.colon_width = colon_width;
    state.active = false;
    state.changing_digits.clear();
    state.old_time_str.clear();
    state.new_time_str.clear();
}

bool checkAndStartSlide(
    VerticalSlideState& state, const std::string& old_time, const std::string& new_time)
{
    // No change? Nothing to animate.
    if (old_time == new_time) {
        return false;
    }

    // If already animating, don't interrupt.
    if (state.active) {
        return false;
    }

    // Find which characters changed.
    state.changing_digits.clear();
    state.old_time_str = old_time;
    state.new_time_str = new_time;

    // Strings should be the same length for time display.
    size_t len = std::min(old_time.size(), new_time.size());
    for (size_t i = 0; i < len; ++i) {
        if (old_time[i] != new_time[i]) {
            // Only animate digits and colons, not spaces.
            char old_c = old_time[i];
            char new_c = new_time[i];
            if ((old_c >= '0' && old_c <= '9') || old_c == ':' || (new_c >= '0' && new_c <= '9')
                || new_c == ':') {
                state.changing_digits.push_back(
                    { .string_index = i, .old_char = old_c, .new_char = new_c, .progress = 0.0 });
            }
        }
    }

    if (state.changing_digits.empty()) {
        return false;
    }

    state.active = true;
    return true;
}

MarqueeFrame updateVerticalSlide(VerticalSlideState& state, double deltaTime)
{
    MarqueeFrame frame;

    if (!state.active) {
        // Not animating - just return the current time string normally.
        frame.digits =
            layoutString(state.new_time_str, state.digit_width, state.digit_gap, state.colon_width);
        frame.finished = true;
        return frame;
    }

    // Advance animation progress for all changing digits.
    bool all_complete = true;
    for (auto& slide : state.changing_digits) {
        slide.progress += state.speed * deltaTime;
        if (slide.progress < 1.0) {
            all_complete = false;
        }
        else {
            slide.progress = 1.0;
        }
    }

    // Build the frame with animated digit positions.
    // Start with the new time string layout as the base.
    auto base_layout =
        layoutString(state.new_time_str, state.digit_width, state.digit_gap, state.colon_width);

    // Create a set of changing string indices for quick lookup.
    std::vector<bool> is_changing(state.new_time_str.size(), false);
    for (const auto& slide : state.changing_digits) {
        if (slide.string_index < is_changing.size()) {
            is_changing[slide.string_index] = true;
        }
    }

    // Map from string index to base_layout index.
    // We need to build this because layoutString skips spaces but preserves order.
    std::vector<size_t> string_to_layout(state.new_time_str.size(), SIZE_MAX);
    size_t layout_idx = 0;
    for (size_t str_idx = 0; str_idx < state.new_time_str.size() && layout_idx < base_layout.size();
         ++str_idx) {
        char c = state.new_time_str[str_idx];
        if ((c >= '0' && c <= '9') || c == ':') {
            string_to_layout[str_idx] = layout_idx;
            ++layout_idx;
        }
    }

    // For each changing digit, we output TWO placements: old char sliding out, new char sliding in.
    // For non-changing digits, output the normal placement.
    for (size_t str_idx = 0; str_idx < state.new_time_str.size(); ++str_idx) {
        size_t layout_i = string_to_layout[str_idx];
        if (layout_i == SIZE_MAX) {
            continue; // This was a space, skip.
        }

        if (!is_changing[str_idx]) {
            // Not changing - use normal position.
            frame.digits.push_back(base_layout[layout_i]);
        }
    }

    // Now add the animating digits.
    double dh = static_cast<double>(state.digit_height);

    for (const auto& slide : state.changing_digits) {
        size_t layout_i = string_to_layout[slide.string_index];
        if (layout_i == SIZE_MAX) {
            continue;
        }

        double base_x = base_layout[layout_i].x;
        double base_y = base_layout[layout_i].y; // Should be 0.

        // Old character slides down and out (y goes from base_y to base_y + digit_height).
        // New character slides down from above (y goes from base_y - digit_height to base_y).
        double old_y = base_y + (slide.progress * dh);
        double new_y = base_y - dh + (slide.progress * dh);

        // Add old character (sliding out) if still visible.
        if (slide.old_char != ' ' && old_y < dh) {
            frame.digits.push_back({ .c = slide.old_char, .x = base_x, .y = old_y });
        }

        // Add new character (sliding in) if visible.
        if (slide.new_char != ' ' && new_y > -dh) {
            frame.digits.push_back({ .c = slide.new_char, .x = base_x, .y = new_y });
        }
    }

    if (all_complete) {
        state.active = false;
        frame.finished = true;
    }

    return frame;
}

} // namespace DirtSim
