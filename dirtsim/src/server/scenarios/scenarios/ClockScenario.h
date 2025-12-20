#pragma once

#include "ClockConfig.h"
#include "ClockFontPatterns.h"
#include "core/Vector2.h"
#include "core/organisms/OrganismType.h"
#include "server/scenarios/Scenario.h"
#include <array>
#include <memory>
#include <random>
#include <vector>

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

    // Event system.
    enum class EventType {
        NONE,
        RAIN,
        DUCK
    };

    enum class DuckAction {
        WAIT,
        RUN_LEFT,
        RUN_RIGHT,
        JUMP
    };

    struct DuckState {
        DuckAction current_action = DuckAction::WAIT;
        float action_timer = 0.0f;        // Time remaining for current action.
        int run_distance = 0;             // Target cells to run (1-5).
        float run_start_x = 0.0f;         // X position when run started.
    };

    EventType current_event_ = EventType::NONE;
    double event_timer_ = 0.0;          // Time remaining for current event / until next event.
    double time_since_init_ = 0.0;      // Total time since scenario started.
    bool first_event_triggered_ = false; // Track if first event occurred.
    uint32_t next_entity_id_ = 1;       // Entity ID counter.
    DuckState duck_state_;              // Duck AI state.
    OrganismId duck_organism_id_ = INVALID_ORGANISM_ID; // Current duck organism.

    // Duck door mechanic state.
    enum class DoorSide { LEFT, RIGHT };
    DoorSide entrance_side_ = DoorSide::LEFT;   // Which side duck enters from.
    Vector2i entrance_door_pos_{ -1, -1 };      // Position of entrance door.
    Vector2i exit_door_pos_{ -1, -1 };          // Position of exit door.
    bool entrance_door_open_ = false;           // True until duck moves away.
    bool exit_door_open_ = false;               // True in last 5 seconds.

    std::mt19937 rng_{ std::random_device{}() };
    std::uniform_real_distribution<double> uniform_dist_{ 0.0, 1.0 };

    // Track which cells were painted for the clock display.
    std::vector<Vector2i> painted_cells_;

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

    // Event system helpers.
    void updateEvents(World& world, double deltaTime);
    void startEvent(World& world, EventType type);
    void updateRainEvent(World& world, double deltaTime);
    void updateDuckEvent(World& world);
    void endEvent(World& world);
    void cancelEvent(World& world);
    void evaporateBottomRow(World& world, double deltaTime);
};

} // namespace DirtSim
