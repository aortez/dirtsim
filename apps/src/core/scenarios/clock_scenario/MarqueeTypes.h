#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace DirtSim {

/**
 * Types for the marquee effect system.
 *
 * CharacterPlacement positions a character in virtual space.
 * MarqueeFrame contains all placements plus viewport transform.
 * The renderer clips to visible area after applying the viewport.
 */

struct CharacterPlacement {
    std::string text; // UTF-8 character (e.g., "0", ":", "🌞").
    double x = 0.0;
    double y = 0.0;
};

struct MarqueeFrame {
    std::vector<CharacterPlacement> placements;
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
    double viewport_x = 0.0;    // Current viewport X position.
    double content_width = 0.0; // Total width of laid-out content.
    double visible_width = 0.0; // Width of visible area.
    double speed = 100.0;       // Scroll speed in units per second.
    bool scrolling_out = true;  // True = scrolling out left, false = scrolling in from right.
};

/**
 * Tracks animation state for a single digit that's changing.
 */
struct SlideDigit {
    size_t string_index = 0; // Position in the time string.
    char old_char = ' ';     // The character being replaced.
    char new_char = ' ';     // The character sliding in.
    double progress = 0.0;   // Animation progress [0, 1]. 0=old visible, 1=new visible.
};

/**
 * Vertical slide effect state.
 *
 * When digits change, the old digit slides down and out while the new digit
 * slides down from above. Unchanged digits remain static.
 */
struct VerticalSlideState {
    std::vector<SlideDigit> changing_digits; // Digits currently animating.
    std::string old_time_str;                // Previous time string.
    std::string new_time_str;                // Current time string.
    double speed = 2.0;                      // Animation speed (progress per second).
    bool active = false;                     // True while animation is in progress.
    int digit_height = 0;                    // Needed for Y animation calculations.
};

using MarqueeEffectState = std::variant<HorizontalScrollState, VerticalSlideState>;

// Lays out a string into character placements at y=0.
std::vector<CharacterPlacement> layoutString(
    const std::string& content, const std::function<int(const std::string&)>& getWidth);

// Calculates the total width of a laid-out string.
int calculateStringWidth(
    const std::string& content, const std::function<int(const std::string&)>& getWidth);

// ============================================================================
// Effect Functions
// ============================================================================

// Initialize a horizontal scroll effect.
void startHorizontalScroll(
    HorizontalScrollState& state,
    const std::string& content,
    double visible_width,
    double speed,
    const std::function<int(const std::string&)>& getWidth);

// Update the horizontal scroll effect and return the frame to render.
MarqueeFrame updateHorizontalScroll(
    HorizontalScrollState& state,
    const std::string& content,
    double deltaTime,
    const std::function<int(const std::string&)>& getWidth);

// ============================================================================
// Vertical Slide Effect Functions
// ============================================================================

// Initialize vertical slide state with layout parameters.
void initVerticalSlide(VerticalSlideState& state, double speed, int digit_height);

// Check if time changed and start a new slide animation if needed.
// Returns true if a new animation was started.
bool checkAndStartSlide(
    VerticalSlideState& state, const std::string& old_time, const std::string& new_time);

// Update the vertical slide animation and return the frame to render.
// Returns a frame with digits at their animated Y positions.
MarqueeFrame updateVerticalSlide(
    VerticalSlideState& state,
    double deltaTime,
    const std::function<int(const std::string&)>& getWidth);

} // namespace DirtSim
