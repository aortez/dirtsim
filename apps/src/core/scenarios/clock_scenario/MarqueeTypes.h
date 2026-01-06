#pragma once

#include <string>
#include <variant>
#include <vector>

namespace DirtSim {

/**
 * Types for the marquee effect system.
 *
 * DigitPlacement positions a character in virtual space.
 * MarqueeFrame contains all placements plus viewport transform.
 * The renderer clips to visible area after applying the viewport.
 */

struct DigitPlacement {
    char c = ' ';
    double x = 0.0;
    double y = 0.0;
};

struct MarqueeFrame {
    std::vector<DigitPlacement> digits;
    double viewportX = 0.0;
    double viewportY = 0.0;
    double zoom = 1.0;
    bool finished = false;
};

// ============================================================================
// Effect State Types
// ============================================================================

/**
 * Horizontal scroll effect state.
 *
 * Phase 1 (scrolling_out=true): Viewport moves right, content scrolls left
 *         until fully off-screen.
 * Phase 2 (scrolling_out=false): Viewport teleports to -visible_width, then
 *         scrolls back to 0 so content appears from the right.
 */
struct HorizontalScrollState {
    double viewport_x = 0.0;      // Current viewport X position.
    double content_width = 0.0;   // Total width of laid-out content.
    double visible_width = 0.0;   // Width of visible area.
    double speed = 100.0;         // Scroll speed in units per second.
    bool scrolling_out = true;    // True = scrolling out left, false = scrolling in from right.

    // Layout parameters (stored at start).
    int digit_width = 0;
    int digit_gap = 0;
    int colon_width = 0;
};

using MarqueeEffectState = std::variant<HorizontalScrollState>;

// Lays out a string into digit placements at y=0.
std::vector<DigitPlacement> layoutString(
    const std::string& content,
    int digitWidth,
    int digitGap,
    int colonWidth);

// Calculates the total width of a laid-out string.
int calculateStringWidth(
    const std::string& content,
    int digitWidth,
    int digitGap,
    int colonWidth);

// ============================================================================
// Effect Functions
// ============================================================================

// Initialize a horizontal scroll effect.
void startHorizontalScroll(
    HorizontalScrollState& state,
    const std::string& content,
    double visible_width,
    double speed,
    int digit_width,
    int digit_gap,
    int colon_width);

// Update the horizontal scroll effect and return the frame to render.
MarqueeFrame updateHorizontalScroll(
    HorizontalScrollState& state,
    const std::string& content,
    double deltaTime);

} // namespace DirtSim
