#pragma once

#include "ClockConfig.h"
#include "ClockFontPatterns.h"
#include "server/scenarios/Scenario.h"
#include <array>
#include <memory>

namespace DirtSim {

/**
 * Clock scenario - displays system time as a digital clock.
 *
 * Supports multiple font styles: 7-segment, large 7-segment, and dot matrix.
 * Format: HH:MM:SS (or HH:MM if seconds disabled).
 */
class ClockScenario : public Scenario {
public:
    // Timezone information.
    struct TimezoneInfo {
        const char* name;  // Short name (e.g., "UTC", "PST").
        const char* label; // Display label for UI.
        int offset_hours;  // UTC offset in hours.
    };

    static constexpr std::array<TimezoneInfo, 10> TIMEZONES = {{
        {"Local", "Local System Time", 0},           // Special: use system time as-is.
        {"UTC", "UTC (Universal)", 0},               // +0.
        {"PST", "Los Angeles (PST)", -8},            // -8.
        {"MST", "Denver (MST)", -7},                 // -7.
        {"CST", "Chicago (CST)", -6},                // -6.
        {"EST", "New York (EST)", -5},               // -5.
        {"GMT", "London (GMT)", 0},                  // +0.
        {"CET", "Paris (CET)", +1},                  // +1.
        {"JST", "Tokyo (JST)", +9},                  // +9.
        {"AEST", "Sydney (AEST)", +10},              // +10.
    }};

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

    // Font dimension helpers.
    int getDigitWidth() const;
    int getDigitHeight() const;
    int getDigitGap() const;
    int getColonWidth() const;
    int getColonPadding() const;

    int calculateTotalWidth() const;
    void recalculateDimensions();
    void drawDigit(World& world, int digit, int start_x, int start_y);
    void drawColon(World& world, int x, int start_y);
    void drawTime(World& world);
};

} // namespace DirtSim
