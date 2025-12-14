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

// clang-format on

} // namespace ClockFonts
} // namespace DirtSim
