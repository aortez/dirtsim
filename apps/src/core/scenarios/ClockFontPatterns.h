#pragma once

#include <array>

namespace DirtSim {

/**
 * @brief Font patterns for the Clock scenario.
 *
 * Contains bitmap patterns for digits 0-9 in multiple font styles.
 * Each pattern is a 2D array where true = filled cell, false = empty.
 */
namespace ClockFonts {

// Montserrat 24pt dimensions (sampled via FontSampler with trimming).
// These are layout hints - actual trimmed patterns vary per glyph (7-15 wide, 17 tall).
// Using max width (15) ensures no digits are clipped during rendering.
static constexpr int MONTSERRAT24_WIDTH = 15;
static constexpr int MONTSERRAT24_HEIGHT = 17;
static constexpr int MONTSERRAT24_GAP = 2;
static constexpr int MONTSERRAT24_COLON_WIDTH = 4;
static constexpr int MONTSERRAT24_COLON_PADDING = 1;

// Noto Color Emoji dimensions (sampled via FontSampler with FreeType).
// Color emojis are rendered as square bitmaps, then converted to materials.
static constexpr int NOTO_EMOJI_WIDTH = 32;
static constexpr int NOTO_EMOJI_HEIGHT = 32;
static constexpr int NOTO_EMOJI_GAP = 2;
static constexpr int NOTO_EMOJI_COLON_WIDTH = 8;
static constexpr int NOTO_EMOJI_COLON_PADDING = 1;

// Standard 7-segment dimensions (5×7).
static constexpr int SEGMENT7_WIDTH = 5;
static constexpr int SEGMENT7_HEIGHT = 7;
static constexpr int SEGMENT7_GAP = 1;
static constexpr int SEGMENT7_COLON_WIDTH = 1;
static constexpr int SEGMENT7_COLON_PADDING = 1;

// Large 7-segment dimensions (8×11).
static constexpr int SEGMENT7_LARGE_WIDTH = 8;
static constexpr int SEGMENT7_LARGE_HEIGHT = 11;
static constexpr int SEGMENT7_LARGE_GAP = 2;
static constexpr int SEGMENT7_LARGE_COLON_WIDTH = 2;
static constexpr int SEGMENT7_LARGE_COLON_PADDING = 1;

// Dot matrix dimensions (5×7).
static constexpr int DOT_MATRIX_WIDTH = 5;
static constexpr int DOT_MATRIX_HEIGHT = 7;
static constexpr int DOT_MATRIX_GAP = 1;
static constexpr int DOT_MATRIX_COLON_WIDTH = 1;
static constexpr int DOT_MATRIX_COLON_PADDING = 1;

// Tall 7-segment dimensions (5×11) - 50% taller, same width as standard.
static constexpr int SEGMENT7_TALL_WIDTH = 5;
static constexpr int SEGMENT7_TALL_HEIGHT = 11;
static constexpr int SEGMENT7_TALL_GAP = 1;
static constexpr int SEGMENT7_TALL_COLON_WIDTH = 1;
static constexpr int SEGMENT7_TALL_COLON_PADDING = 1;

// Extra tall 7-segment dimensions (5×17) - narrow and elegant.
static constexpr int SEGMENT7_EXTRA_TALL_WIDTH = 5;
static constexpr int SEGMENT7_EXTRA_TALL_HEIGHT = 17;
static constexpr int SEGMENT7_EXTRA_TALL_GAP = 1;
static constexpr int SEGMENT7_EXTRA_TALL_COLON_WIDTH = 1;
static constexpr int SEGMENT7_EXTRA_TALL_COLON_PADDING = 1;

// Jumbo 7-segment dimensions (5×23) - maximum height, narrow width.
static constexpr int SEGMENT7_JUMBO_WIDTH = 5;
static constexpr int SEGMENT7_JUMBO_HEIGHT = 23;
static constexpr int SEGMENT7_JUMBO_GAP = 1;
static constexpr int SEGMENT7_JUMBO_COLON_WIDTH = 1;
static constexpr int SEGMENT7_JUMBO_COLON_PADDING = 1;

// clang-format off

/**
 * @brief Standard 7-segment patterns for digits 0-9.
 * Each digit is a 5x7 grid where true = filled cell.
 * Classic calculator/digital clock style.
 */
static constexpr std::array<std::array<std::array<bool, SEGMENT7_WIDTH>, SEGMENT7_HEIGHT>, 10>
    SEGMENT7_PATTERNS = {{
        // 0: segments a, b, c, d, e, f (all except g).
        {{
            {false, true,  true,  true,  false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, false, false, false, false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, true,  true,  true,  false},
        }},
        // 1: segments b, c (right side only).
        {{
            {false, false, false, false, false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, false, false, false, false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, false, false, false, false},
        }},
        // 2: segments a, b, g, e, d.
        {{
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, true,  true,  true,  false},
            {true,  false, false, false, false},
            {true,  false, false, false, false},
            {false, true,  true,  true,  false},
        }},
        // 3: segments a, b, g, c, d.
        {{
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, true,  true,  true,  false},
        }},
        // 4: segments f, g, b, c.
        {{
            {false, false, false, false, false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, false, false, false, false},
        }},
        // 5: segments a, f, g, c, d.
        {{
            {false, true,  true,  true,  false},
            {true,  false, false, false, false},
            {true,  false, false, false, false},
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, true,  true,  true,  false},
        }},
        // 6: segments a, f, g, e, c, d (all except b).
        {{
            {false, true,  true,  true,  false},
            {true,  false, false, false, false},
            {true,  false, false, false, false},
            {false, true,  true,  true,  false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, true,  true,  true,  false},
        }},
        // 7: segments a, b, c.
        {{
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, false, false, false, false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, false, false, false, false},
        }},
        // 8: all segments.
        {{
            {false, true,  true,  true,  false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, true,  true,  true,  false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, true,  true,  true,  false},
        }},
        // 9: segments a, b, c, d, f, g (all except e).
        {{
            {false, true,  true,  true,  false},
            {true,  false, false, false, true },
            {true,  false, false, false, true },
            {false, true,  true,  true,  false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, true,  true,  true,  false},
        }},
    }};

/**
 * @brief Large 7-segment patterns for digits 0-9.
 * Each digit is an 8x11 grid where true = filled cell.
 * Thicker segments (2 cells wide) for better visibility.
 */
static constexpr std::array<std::array<std::array<bool, SEGMENT7_LARGE_WIDTH>, SEGMENT7_LARGE_HEIGHT>, 10>
    SEGMENT7_LARGE_PATTERNS = {{
        // 0
        {{
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, false, false, false, false, false, false, false},
            {false, false, false, false, false, false, false, false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
        }},
        // 1
        {{
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, false, false},
            {false, false, false, false, false, false, false, false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
        }},
        // 2
        {{
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {false, true , true , true , true , true , true , false},
        }},
        // 3
        {{
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
        }},
        // 4
        {{
            {false, false, false, false, false, false, false, false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, false, false},
        }},
        // 5
        {{
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
        }},
        // 6
        {{
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {true , true , false, false, false, false, false, false},
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
        }},
        // 7
        {{
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, false, false},
            {false, false, false, false, false, false, false, false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, false, false},
        }},
        // 8
        {{
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
        }},
        // 9
        {{
            {false, true , true , true , true , true , true , false},
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {true , true , false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
            {false, true , true , true , true , true , true , false},
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, false, false, false, false, false, true , true },
            {false, true , true , true , true , true , true , false},
        }},
    }};

/**
 * @brief Dot matrix patterns for digits 0-9.
 * Each digit is a 5x7 grid where true = filled cell.
 * Pixel-font style with rounded/filled shapes, distinct from 7-segment.
 */
static constexpr std::array<std::array<std::array<bool, DOT_MATRIX_WIDTH>, DOT_MATRIX_HEIGHT>, 10>
    DOT_MATRIX_PATTERNS = {{
        // 0 - Rounded oval with filled interior edges.
        {{
            {false, true , true , true , false},
            {true , true , false, true , true },
            {true , false, false, false, true },
            {true , false, false, false, true },
            {true , false, false, false, true },
            {true , true , false, true , true },
            {false, true , true , true , false},
        }},
        // 1 - Centered with serif base.
        {{
            {false, false, true , false, false},
            {false, true , true , false, false},
            {true , false, true , false, false},
            {false, false, true , false, false},
            {false, false, true , false, false},
            {false, false, true , false, false},
            {true , true , true , true , true },
        }},
        // 2 - Curvy top, diagonal stroke, flat base.
        {{
            {false, true , true , true , false},
            {true , false, false, false, true },
            {false, false, false, true , false},
            {false, false, true , false, false},
            {false, true , false, false, false},
            {true , false, false, false, false},
            {true , true , true , true , true },
        }},
        // 3 - Two bumps on right side.
        {{
            {true , true , true , true , false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {false, true , true , true , false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {true , true , true , true , false},
        }},
        // 4 - Angular with strong crossbar.
        {{
            {false, false, false, true , false},
            {false, false, true , true , false},
            {false, true , false, true , false},
            {true , false, false, true , false},
            {true , true , true , true , true },
            {false, false, false, true , false},
            {false, false, false, true , false},
        }},
        // 5 - Flat top, curved bottom.
        {{
            {true , true , true , true , true },
            {true , false, false, false, false},
            {true , true , true , true , false},
            {false, false, false, false, true },
            {false, false, false, false, true },
            {true , false, false, false, true },
            {false, true , true , true , false},
        }},
        // 6 - Curved top descender, round bottom.
        {{
            {false, false, true , true , false},
            {false, true , false, false, false},
            {true , false, false, false, false},
            {true , true , true , true , false},
            {true , false, false, false, true },
            {true , false, false, false, true },
            {false, true , true , true , false},
        }},
        // 7 - Flat top with diagonal descent.
        {{
            {true , true , true , true , true },
            {false, false, false, false, true },
            {false, false, false, true , false},
            {false, false, false, true , false},
            {false, false, true , false, false},
            {false, false, true , false, false},
            {false, false, true , false, false},
        }},
        // 8 - Two stacked rounded sections.
        {{
            {false, true , true , true , false},
            {true , false, false, false, true },
            {true , false, false, false, true },
            {false, true , true , true , false},
            {true , false, false, false, true },
            {true , false, false, false, true },
            {false, true , true , true , false},
        }},
        // 9 - Round top, straight descender.
        {{
            {false, true , true , true , false},
            {true , false, false, false, true },
            {true , false, false, false, true },
            {false, true , true , true , true },
            {false, false, false, false, true },
            {false, false, false, true , false},
            {false, true , true , false, false},
        }},
    }};

/**
 * @brief Tall 7-segment patterns for digits 0-9.
 * Each digit is a 5x11 grid where true = filled cell.
 * Same width as standard 7-segment but 50% taller (stretched vertically).
 * Vertical segments span 4 rows instead of 2 for elegant proportions.
 */
static constexpr std::array<std::array<std::array<bool, SEGMENT7_TALL_WIDTH>, SEGMENT7_TALL_HEIGHT>, 10>
    SEGMENT7_TALL_PATTERNS = {{
        // 0: segments a, b, c, d, e, f (all except g).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {false, false, false, false, false},  // Row 5: middle (g) - off.
            {true , false, false, false, true },  // Row 6: lower verticals (e, c).
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
        // 1: segments b, c (right side only).
        {{
            {false, false, false, false, false},  // Row 0: top (a) - off.
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, false},  // Row 5: middle (g) - off.
            {false, false, false, false, true },  // Row 6: lower right (c).
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, false},  // Row 10: bottom (d) - off.
        }},
        // 2: segments a, b, g, e, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {true , false, false, false, false},  // Row 6: lower left (e).
            {true , false, false, false, false},  // Row 7.
            {true , false, false, false, false},  // Row 8.
            {true , false, false, false, false},  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
        // 3: segments a, b, g, c, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {false, false, false, false, true },  // Row 6: lower right (c).
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
        // 4: segments f, g, b, c.
        {{
            {false, false, false, false, false},  // Row 0: top (a) - off.
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {false, false, false, false, true },  // Row 6: lower right (c).
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, false},  // Row 10: bottom (d) - off.
        }},
        // 5: segments a, f, g, c, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, false},  // Row 1: upper left (f).
            {true , false, false, false, false},  // Row 2.
            {true , false, false, false, false},  // Row 3.
            {true , false, false, false, false},  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {false, false, false, false, true },  // Row 6: lower right (c).
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
        // 6: segments a, f, g, e, c, d (all except b).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, false},  // Row 1: upper left (f).
            {true , false, false, false, false},  // Row 2.
            {true , false, false, false, false},  // Row 3.
            {true , false, false, false, false},  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {true , false, false, false, true },  // Row 6: lower verticals (e, c).
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
        // 7: segments a, b, c.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, false},  // Row 5: middle (g) - off.
            {false, false, false, false, true },  // Row 6: lower right (c).
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, false},  // Row 10: bottom (d) - off.
        }},
        // 8: all segments.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {true , false, false, false, true },  // Row 6: lower verticals (e, c).
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
        // 9: segments a, b, c, d, f, g (all except e).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {false, true , true , true , false},  // Row 5: middle (g).
            {false, false, false, false, true },  // Row 6: lower right (c).
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, true , true , true , false},  // Row 10: bottom (d).
        }},
    }};

/**
 * @brief Extra tall 7-segment patterns for digits 0-9.
 * Each digit is a 5x17 grid where true = filled cell.
 * Vertical segments span 7 rows each for very tall proportions.
 */
static constexpr std::array<std::array<std::array<bool, SEGMENT7_EXTRA_TALL_WIDTH>, SEGMENT7_EXTRA_TALL_HEIGHT>, 10>
    SEGMENT7_EXTRA_TALL_PATTERNS = {{
        // 0: segments a, b, c, d, e, f (all except g).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {false, false, false, false, false},  // Row 8: middle (g) - off.
            {true , false, false, false, true },  // Row 9: lower verticals (e, c).
            {true , false, false, false, true },  // Row 10.
            {true , false, false, false, true },  // Row 11.
            {true , false, false, false, true },  // Row 12.
            {true , false, false, false, true },  // Row 13.
            {true , false, false, false, true },  // Row 14.
            {true , false, false, false, true },  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
        // 1: segments b, c (right side only).
        {{
            {false, false, false, false, false},  // Row 0: top (a) - off.
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, false},  // Row 8: middle (g) - off.
            {false, false, false, false, true },  // Row 9: lower right (c).
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, true },  // Row 11.
            {false, false, false, false, true },  // Row 12.
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, false},  // Row 16: bottom (d) - off.
        }},
        // 2: segments a, b, g, e, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {true , false, false, false, false},  // Row 9: lower left (e).
            {true , false, false, false, false},  // Row 10.
            {true , false, false, false, false},  // Row 11.
            {true , false, false, false, false},  // Row 12.
            {true , false, false, false, false},  // Row 13.
            {true , false, false, false, false},  // Row 14.
            {true , false, false, false, false},  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
        // 3: segments a, b, g, c, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {false, false, false, false, true },  // Row 9: lower right (c).
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, true },  // Row 11.
            {false, false, false, false, true },  // Row 12.
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
        // 4: segments f, g, b, c.
        {{
            {false, false, false, false, false},  // Row 0: top (a) - off.
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {false, false, false, false, true },  // Row 9: lower right (c).
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, true },  // Row 11.
            {false, false, false, false, true },  // Row 12.
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, false},  // Row 16: bottom (d) - off.
        }},
        // 5: segments a, f, g, c, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, false},  // Row 1: upper left (f).
            {true , false, false, false, false},  // Row 2.
            {true , false, false, false, false},  // Row 3.
            {true , false, false, false, false},  // Row 4.
            {true , false, false, false, false},  // Row 5.
            {true , false, false, false, false},  // Row 6.
            {true , false, false, false, false},  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {false, false, false, false, true },  // Row 9: lower right (c).
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, true },  // Row 11.
            {false, false, false, false, true },  // Row 12.
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
        // 6: segments a, f, g, e, c, d (all except b).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, false},  // Row 1: upper left (f).
            {true , false, false, false, false},  // Row 2.
            {true , false, false, false, false},  // Row 3.
            {true , false, false, false, false},  // Row 4.
            {true , false, false, false, false},  // Row 5.
            {true , false, false, false, false},  // Row 6.
            {true , false, false, false, false},  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {true , false, false, false, true },  // Row 9: lower verticals (e, c).
            {true , false, false, false, true },  // Row 10.
            {true , false, false, false, true },  // Row 11.
            {true , false, false, false, true },  // Row 12.
            {true , false, false, false, true },  // Row 13.
            {true , false, false, false, true },  // Row 14.
            {true , false, false, false, true },  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
        // 7: segments a, b, c.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, false},  // Row 8: middle (g) - off.
            {false, false, false, false, true },  // Row 9: lower right (c).
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, true },  // Row 11.
            {false, false, false, false, true },  // Row 12.
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, false},  // Row 16: bottom (d) - off.
        }},
        // 8: all segments.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {true , false, false, false, true },  // Row 9: lower verticals (e, c).
            {true , false, false, false, true },  // Row 10.
            {true , false, false, false, true },  // Row 11.
            {true , false, false, false, true },  // Row 12.
            {true , false, false, false, true },  // Row 13.
            {true , false, false, false, true },  // Row 14.
            {true , false, false, false, true },  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
        // 9: segments a, b, c, d, f, g (all except e).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {false, true , true , true , false},  // Row 8: middle (g).
            {false, false, false, false, true },  // Row 9: lower right (c).
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, true },  // Row 11.
            {false, false, false, false, true },  // Row 12.
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, true , true , true , false},  // Row 16: bottom (d).
        }},
    }};

/**
 * @brief Jumbo 7-segment patterns for digits 0-9.
 * Each digit is a 5x23 grid where true = filled cell.
 * Vertical segments span 10 rows each for maximum height.
 */
static constexpr std::array<std::array<std::array<bool, SEGMENT7_JUMBO_WIDTH>, SEGMENT7_JUMBO_HEIGHT>, 10>
    SEGMENT7_JUMBO_PATTERNS = {{
        // 0: segments a, b, c, d, e, f (all except g).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {true , false, false, false, true },  // Row 10.
            {false, false, false, false, false},  // Row 11: middle (g) - off.
            {true , false, false, false, true },  // Row 12: lower verticals (e, c).
            {true , false, false, false, true },  // Row 13.
            {true , false, false, false, true },  // Row 14.
            {true , false, false, false, true },  // Row 15.
            {true , false, false, false, true },  // Row 16.
            {true , false, false, false, true },  // Row 17.
            {true , false, false, false, true },  // Row 18.
            {true , false, false, false, true },  // Row 19.
            {true , false, false, false, true },  // Row 20.
            {true , false, false, false, true },  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
        // 1: segments b, c (right side only).
        {{
            {false, false, false, false, false},  // Row 0: top (a) - off.
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, false},  // Row 11: middle (g) - off.
            {false, false, false, false, true },  // Row 12: lower right (c).
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, true },  // Row 16.
            {false, false, false, false, true },  // Row 17.
            {false, false, false, false, true },  // Row 18.
            {false, false, false, false, true },  // Row 19.
            {false, false, false, false, true },  // Row 20.
            {false, false, false, false, true },  // Row 21.
            {false, false, false, false, false},  // Row 22: bottom (d) - off.
        }},
        // 2: segments a, b, g, e, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, true },  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {true , false, false, false, false},  // Row 12: lower left (e).
            {true , false, false, false, false},  // Row 13.
            {true , false, false, false, false},  // Row 14.
            {true , false, false, false, false},  // Row 15.
            {true , false, false, false, false},  // Row 16.
            {true , false, false, false, false},  // Row 17.
            {true , false, false, false, false},  // Row 18.
            {true , false, false, false, false},  // Row 19.
            {true , false, false, false, false},  // Row 20.
            {true , false, false, false, false},  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
        // 3: segments a, b, g, c, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, true },  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {false, false, false, false, true },  // Row 12: lower right (c).
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, true },  // Row 16.
            {false, false, false, false, true },  // Row 17.
            {false, false, false, false, true },  // Row 18.
            {false, false, false, false, true },  // Row 19.
            {false, false, false, false, true },  // Row 20.
            {false, false, false, false, true },  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
        // 4: segments f, g, b, c.
        {{
            {false, false, false, false, false},  // Row 0: top (a) - off.
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {true , false, false, false, true },  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {false, false, false, false, true },  // Row 12: lower right (c).
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, true },  // Row 16.
            {false, false, false, false, true },  // Row 17.
            {false, false, false, false, true },  // Row 18.
            {false, false, false, false, true },  // Row 19.
            {false, false, false, false, true },  // Row 20.
            {false, false, false, false, true },  // Row 21.
            {false, false, false, false, false},  // Row 22: bottom (d) - off.
        }},
        // 5: segments a, f, g, c, d.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, false},  // Row 1: upper left (f).
            {true , false, false, false, false},  // Row 2.
            {true , false, false, false, false},  // Row 3.
            {true , false, false, false, false},  // Row 4.
            {true , false, false, false, false},  // Row 5.
            {true , false, false, false, false},  // Row 6.
            {true , false, false, false, false},  // Row 7.
            {true , false, false, false, false},  // Row 8.
            {true , false, false, false, false},  // Row 9.
            {true , false, false, false, false},  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {false, false, false, false, true },  // Row 12: lower right (c).
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, true },  // Row 16.
            {false, false, false, false, true },  // Row 17.
            {false, false, false, false, true },  // Row 18.
            {false, false, false, false, true },  // Row 19.
            {false, false, false, false, true },  // Row 20.
            {false, false, false, false, true },  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
        // 6: segments a, f, g, e, c, d (all except b).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, false},  // Row 1: upper left (f).
            {true , false, false, false, false},  // Row 2.
            {true , false, false, false, false},  // Row 3.
            {true , false, false, false, false},  // Row 4.
            {true , false, false, false, false},  // Row 5.
            {true , false, false, false, false},  // Row 6.
            {true , false, false, false, false},  // Row 7.
            {true , false, false, false, false},  // Row 8.
            {true , false, false, false, false},  // Row 9.
            {true , false, false, false, false},  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {true , false, false, false, true },  // Row 12: lower verticals (e, c).
            {true , false, false, false, true },  // Row 13.
            {true , false, false, false, true },  // Row 14.
            {true , false, false, false, true },  // Row 15.
            {true , false, false, false, true },  // Row 16.
            {true , false, false, false, true },  // Row 17.
            {true , false, false, false, true },  // Row 18.
            {true , false, false, false, true },  // Row 19.
            {true , false, false, false, true },  // Row 20.
            {true , false, false, false, true },  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
        // 7: segments a, b, c.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {false, false, false, false, true },  // Row 1: upper right (b).
            {false, false, false, false, true },  // Row 2.
            {false, false, false, false, true },  // Row 3.
            {false, false, false, false, true },  // Row 4.
            {false, false, false, false, true },  // Row 5.
            {false, false, false, false, true },  // Row 6.
            {false, false, false, false, true },  // Row 7.
            {false, false, false, false, true },  // Row 8.
            {false, false, false, false, true },  // Row 9.
            {false, false, false, false, true },  // Row 10.
            {false, false, false, false, false},  // Row 11: middle (g) - off.
            {false, false, false, false, true },  // Row 12: lower right (c).
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, true },  // Row 16.
            {false, false, false, false, true },  // Row 17.
            {false, false, false, false, true },  // Row 18.
            {false, false, false, false, true },  // Row 19.
            {false, false, false, false, true },  // Row 20.
            {false, false, false, false, true },  // Row 21.
            {false, false, false, false, false},  // Row 22: bottom (d) - off.
        }},
        // 8: all segments.
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {true , false, false, false, true },  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {true , false, false, false, true },  // Row 12: lower verticals (e, c).
            {true , false, false, false, true },  // Row 13.
            {true , false, false, false, true },  // Row 14.
            {true , false, false, false, true },  // Row 15.
            {true , false, false, false, true },  // Row 16.
            {true , false, false, false, true },  // Row 17.
            {true , false, false, false, true },  // Row 18.
            {true , false, false, false, true },  // Row 19.
            {true , false, false, false, true },  // Row 20.
            {true , false, false, false, true },  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
        // 9: segments a, b, c, d, f, g (all except e).
        {{
            {false, true , true , true , false},  // Row 0: top (a).
            {true , false, false, false, true },  // Row 1: upper verticals (f, b).
            {true , false, false, false, true },  // Row 2.
            {true , false, false, false, true },  // Row 3.
            {true , false, false, false, true },  // Row 4.
            {true , false, false, false, true },  // Row 5.
            {true , false, false, false, true },  // Row 6.
            {true , false, false, false, true },  // Row 7.
            {true , false, false, false, true },  // Row 8.
            {true , false, false, false, true },  // Row 9.
            {true , false, false, false, true },  // Row 10.
            {false, true , true , true , false},  // Row 11: middle (g).
            {false, false, false, false, true },  // Row 12: lower right (c).
            {false, false, false, false, true },  // Row 13.
            {false, false, false, false, true },  // Row 14.
            {false, false, false, false, true },  // Row 15.
            {false, false, false, false, true },  // Row 16.
            {false, false, false, false, true },  // Row 17.
            {false, false, false, false, true },  // Row 18.
            {false, false, false, false, true },  // Row 19.
            {false, false, false, false, true },  // Row 20.
            {false, false, false, false, true },  // Row 21.
            {false, true , true , true , false},  // Row 22: bottom (d).
        }},
    }};

// clang-format on

} // namespace ClockFonts
} // namespace DirtSim
