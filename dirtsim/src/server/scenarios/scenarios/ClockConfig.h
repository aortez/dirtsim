#pragma once

#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Available clock font styles.
 */
enum class ClockFont : uint8_t {
    DotMatrix = 0,     // Dot matrix bitmap (5×7 cells).
    Segment7 = 1,      // Standard 7-segment (5×7 cells).
    Segment7Large = 2, // Large 7-segment (8×11 cells).
    Segment7Tall = 3,  // Tall 7-segment (5×11 cells) - 50% taller, same width.
};

// JSON serialization for ClockFont enum.
NLOHMANN_JSON_SERIALIZE_ENUM(
    ClockFont,
    {
        {ClockFont::DotMatrix, "dot_matrix"},
        {ClockFont::Segment7, "segment7"},
        {ClockFont::Segment7Large, "segment7_large"},
        {ClockFont::Segment7Tall, "segment7_tall"},
    })

/**
 * @brief Clock scenario config - displays system time using 7-segment digits.
 *
 * World size is computed from clock dimensions × scale factors.
 * Clock dimensions depend on selected font.
 *
 * Auto-scaling mode calculates scale factors to maximize clock size
 * while fitting within target display dimensions with margins.
 */
struct ClockConfig {
    using serialize = zpp::bits::members<10>;

    double horizontal_scale = 1.1;         // World width = clock_width × scale.
    double vertical_scale = 2.0;           // World height = clock_height × scale.
    uint8_t timezone_index = 2;            // Index into TIMEZONES array (2 = PST).
    ClockFont font = ClockFont::Segment7Tall;  // Font style.
    bool show_seconds = true;              // Show seconds (HH:MM:SS vs HH:MM).
    bool auto_scale = true;                // Enable auto-scaling to fit display.
    uint32_t target_display_width = 752;   // Target display width in pixels.
    uint32_t target_display_height = 480;  // Target display height in pixels.
    uint32_t margin_pixels = 20;           // Margin in pixels (all sides).

    // Random event system (disabled when event_frequency = 0).
    double event_frequency = 0.5;          // Event frequency [0, 1] (0 = disabled, 1 = very frequent).
};

} // namespace DirtSim
