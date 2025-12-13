#pragma once

#include "ClockConfig.h"
#include "server/scenarios/Scenario.h"
#include <array>
#include <memory>

namespace DirtSim {

/**
 * Clock scenario - displays system time as a digital clock using 7-segment digits.
 *
 * Format: HH:MM:SS
 * Each digit is 5 cells wide x 7 cells tall.
 * Digits are separated by 1 cell gap.
 * Colons are 1 cell wide with 1 cell padding on each side.
 */
class ClockScenario : public Scenario {
public:
    // Digit dimensions.
    static constexpr int DIGIT_WIDTH = 5;
    static constexpr int DIGIT_HEIGHT = 7;
    static constexpr int DIGIT_GAP = 1;
    static constexpr int COLON_WIDTH = 1;
    static constexpr int COLON_PADDING = 1;

    // 7-segment patterns for digits 0-9.
    // Each digit is a 5x7 grid where true = wall cell.
    // clang-format off
    static constexpr std::array<std::array<std::array<bool, DIGIT_WIDTH>, DIGIT_HEIGHT>, 10>
        DIGIT_PATTERNS = {{
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
    // clang-format on

    ClockScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    ClockConfig config_;
    int last_second_ = -1;

    int calculateTotalWidth() const;
    void drawDigit(World& world, int digit, int start_x, int start_y);
    void drawColon(World& world, int x, int start_y);
    void drawTime(World& world);
};

} // namespace DirtSim
