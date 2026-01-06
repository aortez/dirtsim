#include "MarqueeTypes.h"

namespace DirtSim {

std::vector<DigitPlacement> layoutString(
    const std::string& content,
    int digitWidth,
    int digitGap,
    int colonWidth)
{
    std::vector<DigitPlacement> placements;
    double x = 0.0;

    for (const char c : content) {
        if (c >= '0' && c <= '9') {
            placements.push_back({.c = c, .x = x, .y = 0.0});
            x += digitWidth;
        } else if (c == ':') {
            placements.push_back({.c = c, .x = x, .y = 0.0});
            x += colonWidth;
        } else if (c == ' ') {
            // Spaces create gaps but aren't drawn.
            x += digitGap;
        }
    }

    return placements;
}

int calculateStringWidth(
    const std::string& content,
    int digitWidth,
    int digitGap,
    int colonWidth)
{
    int width = 0;

    for (const char c : content) {
        if (c >= '0' && c <= '9') {
            width += digitWidth;
        } else if (c == ':') {
            width += colonWidth;
        } else if (c == ' ') {
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
    HorizontalScrollState& state,
    const std::string& content,
    double deltaTime)
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
    } else {
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

} // namespace DirtSim
