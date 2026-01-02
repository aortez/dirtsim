#pragma once

#include "ClockConfig.h"
#include "ClockFontPatterns.h"
#include "core/MaterialType.h"
#include "core/Vector2.h"
#include "core/organisms/OrganismType.h"
#include "core/scenarios/Scenario.h"
#include <array>
#include <map>
#include <memory>
#include <random>
#include <unordered_map>
#include <variant>
#include <vector>

namespace DirtSim {

class World;

// ============================================================================
// Event System Types
// ============================================================================

enum class ClockEventType {
    DUCK,
    MELTDOWN,
    RAIN
};

struct EventTypeConfig {
    double duration;
    double chance_per_second;
    double cooldown;
};

struct MeltdownEventState {
    // Meltdown lets digits fall, converts metal to water on impact.
    int digit_bottom_y = 0;  // Scanned at event start: lowest Y with METAL.
};

struct RainEventState {
    // Rain-specific state (could add intensity, pattern, etc.).
};

enum class DoorSide { LEFT, RIGHT };

enum class DuckEventPhase {
    DOOR_OPENING,  // Door open, waiting before spawning duck.
    DUCK_ACTIVE,   // Duck spawned and walking.
    DOOR_CLOSING   // Duck exited, waiting briefly before closing door.
};

struct DuckEventState {
    OrganismId organism_id = INVALID_ORGANISM_ID;
    DoorSide entrance_side = DoorSide::LEFT;
    Vector2i entrance_door_pos{ -1, -1 };
    Vector2i exit_door_pos{ -1, -1 };
    bool entrance_door_open = false;
    bool exit_door_open = false;
    DuckEventPhase phase = DuckEventPhase::DOOR_OPENING;
    double door_open_timer = 0.0;   // Time door has been open before duck spawns.
    double door_close_timer = 0.0;  // Time since duck exited, before closing door.
};

using EventState = std::variant<DuckEventState, MeltdownEventState, RainEventState>;

struct ActiveEvent {
    EventState state;
    double remaining_time;
};

// ============================================================================
// Door Manager
// ============================================================================

class DoorManager {
public:
    struct DoorState {
        bool is_open = false;
        DoorSide side = DoorSide::LEFT;
        Vector2i door_pos{ -1, -1 };
        Vector2i roof_pos{ -1, -1 };
    };

    bool openDoor(Vector2i pos, DoorSide side, World& world);
    void closeDoor(Vector2i pos, World& world);
    bool isOpenDoor(Vector2i pos) const;
    bool isRoofCell(Vector2i pos) const;
    std::vector<Vector2i> getOpenDoorPositions() const;
    std::vector<Vector2i> getRoofPositions() const;
    void closeAllDoors(World& world);

private:
    std::unordered_map<Vector2i, DoorState> doors_;

    static Vector2i calculateRoofPos(Vector2i door_pos, DoorSide side);
};

// ============================================================================
// Clock Scenario
// ============================================================================

/**
 * Clock scenario - displays system time as a digital clock.
 *
 * Supports multiple font styles: 7-segment, large 7-segment, and dot matrix.
 * Format: HH:MM:SS (or HH:MM if seconds disabled).
 *
 * Event system allows multiple concurrent events (rain, duck, etc.).
 */
class ClockScenario : public Scenario {
public:
    struct TimezoneInfo {
        const char* name;
        const char* label;
        int offset_hours;
    };

    static constexpr std::array<TimezoneInfo, 10> TIMEZONES = {{
        {"Local", "Local System Time", 0},
        {"UTC", "UTC (Universal)", 0},
        {"PST", "Los Angeles (PST)", -8},
        {"MST", "Denver (MST)", -7},
        {"CST", "Chicago (CST)", -6},
        {"EST", "New York (EST)", -5},
        {"GMT", "London (GMT)", 0},
        {"CET", "Paris (CET)", +1},
        {"JST", "Tokyo (JST)", +9},
        {"AEST", "Sydney (AEST)", +10},
    }};

    static const std::map<ClockEventType, EventTypeConfig> DEFAULT_EVENT_CONFIGS;

    ClockScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::Clock config_;
    int last_second_ = -1;

    // Event system.
    std::map<ClockEventType, ActiveEvent> active_events_;
    std::map<ClockEventType, double> event_cooldowns_;
    double time_since_last_trigger_check_ = 0.0;

    // Door management.
    DoorManager door_manager_;

    // Floor drain state.
    bool drain_open_ = false;
    uint32_t drain_start_x_ = 0;
    uint32_t drain_end_x_ = 0;

    std::mt19937 rng_{ std::random_device{}() };
    std::uniform_real_distribution<double> uniform_dist_{ 0.0, 1.0 };

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
    void tryTriggerEvents(World& world);
    void startEvent(World& world, ClockEventType type);
    void updateEvent(World& world, ClockEventType type, ActiveEvent& event, double deltaTime);
    void endEvent(World& world, ClockEventType type, ActiveEvent& event);
    void cancelAllEvents(World& world);
    double countWaterInBottomThird(const World& world) const;
    void updateDrain(World& world);

    // Event-specific update handlers (called via visitor).
    void updateDuckEvent(World& world, DuckEventState& state, double& remaining_time, double deltaTime);
    void spawnDuck(World& world, DuckEventState& state);
    void updateMeltdownEvent(World& world, MeltdownEventState& state, double& remaining_time, double deltaTime);
    void updateRainEvent(World& world, RainEventState& state, double deltaTime);

    bool isMeltdownActive() const;
    void convertStrayMetalToWater(World& world);
    void redrawWalls(World& world);
};

} // namespace DirtSim
