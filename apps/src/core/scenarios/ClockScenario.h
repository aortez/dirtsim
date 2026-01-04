#pragma once

#include "ClockConfig.h"
#include "ClockFontPatterns.h"
#include "core/Cell.h"
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
    COLOR_CYCLE,
    COLOR_SHOWCASE,
    DUCK,
    MELTDOWN,
    RAIN
};

struct EventTimingConfig {
    double duration;
    double chance_per_second;
    double cooldown;
};

struct DuckEventConfig {
    EventTimingConfig timing = { .duration = 30.0, .chance_per_second = 0.05, .cooldown = 10.0 };
    bool floor_obstacles_enabled = true;
};

struct MeltdownEventConfig {
    EventTimingConfig timing = { .duration = 20.0, .chance_per_second = 0.01, .cooldown = 30.0 };
};

struct RainEventConfig {
    EventTimingConfig timing = { .duration = 20.0, .chance_per_second = 0.01, .cooldown = 30.0 };
};

struct ColorCycleEventConfig {
    EventTimingConfig timing = { .duration = 10.0, .chance_per_second = 0.04, .cooldown = 15.0 };
};

struct ColorShowcaseEventConfig {
    EventTimingConfig timing = { .duration = 120.0, .chance_per_second = 0.1, .cooldown = 60.0 };
    std::vector<MaterialType> showcase_materials = {
        MaterialType::LEAF, MaterialType::WATER, MaterialType::SEED, MaterialType::WOOD
    };
};

struct MeltdownEventState {
    // Meltdown lets digits fall, converts to water on impact.
    int digit_bottom_y = 0;         // Scanned at event start: lowest Y with digit material.
    MaterialType digit_material{};  // Material type digits become when melting.
};

struct RainEventState {
    // Rain-specific state (could add intensity, pattern, etc.).
};

struct ColorCycleEventState {
    size_t current_index = 0;      // Current position in cycle.
    double time_per_color = 0.0;   // Calculated at start: duration / num_materials.
    double time_in_current = 0.0;  // Time spent on current color.
};

struct ColorShowcaseEventState {
    size_t current_index = 0;  // Current position in showcase materials list.
};

enum class DoorSide { LEFT, RIGHT };

enum class DuckEventPhase {
    DOOR_OPENING,  // Door open, waiting before spawning duck.
    DUCK_ACTIVE,   // Duck spawned and walking.
    DOOR_CLOSING   // Duck exited, waiting briefly before closing door.
};

// Floor modification that challenges the duck.
enum class FloorObstacleType { HURDLE, PIT };

struct FloorObstacle {
    int start_x = 0;              // Starting X position.
    int width = 1;                // Number of contiguous cells (1-3).
    FloorObstacleType type = FloorObstacleType::HURDLE;
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

    // Floor obstacles to challenge the duck (hurdles and pits).
    std::vector<FloorObstacle> floor_obstacles;
    double obstacle_spawn_timer = 0.0;  // Time until next obstacle spawn attempt.
};

using EventState = std::variant<ColorCycleEventState, ColorShowcaseEventState, DuckEventState, MeltdownEventState, RainEventState>;

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

struct ClockEventConfigs {
    ColorCycleEventConfig color_cycle;
    ColorShowcaseEventConfig color_showcase;
    DuckEventConfig duck;
    MeltdownEventConfig meltdown;
    RainEventConfig rain;
};

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

    explicit ClockScenario(ClockEventConfigs event_configs = {});

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

    // Event state accessors.
    bool isEventActive(ClockEventType type) const;
    size_t getActiveEventCount() const;
    const EventTimingConfig& getEventTiming(ClockEventType type) const;

private:
    ScenarioMetadata metadata_;
    Config::Clock config_;
    ClockEventConfigs event_configs_;
    std::string last_drawn_time_;

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
    uint32_t current_drain_size_ = 0;  // Current drain size (0, 1, 3, 5, or 7).
    std::chrono::steady_clock::time_point last_drain_size_change_;

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
    void clearDigits(World& world);
    void drawDigit(World& world, int digit, int start_x, int start_y);
    void drawColon(World& world, int x, int start_y);
    void drawTime(World& world);
    std::string getCurrentTimeString() const;

    // Event system helpers.
    void updateEvents(World& world, double deltaTime);
    void tryTriggerEvents(World& world);
    void startEvent(World& world, ClockEventType type);
    void updateEvent(World& world, ClockEventType type, ActiveEvent& event, double deltaTime);
    void endEvent(World& world, ClockEventType type, ActiveEvent& event);
    void cancelAllEvents(World& world);
    double countWaterInBottomThird(const World& world) const;
    void updateDrain(World& world, double deltaTime);
    void sprayDrainCell(World& world, Cell& cell, uint32_t x, uint32_t y);

    // Event-specific update handlers (called via visitor).
    void updateColorCycleEvent(World& world, ColorCycleEventState& state, double deltaTime);
    void updateColorShowcaseEvent(World& world, ColorShowcaseEventState& state, double deltaTime);
    void updateDuckEvent(World& world, DuckEventState& state, double& remaining_time, double deltaTime);
    void spawnDuck(World& world, DuckEventState& state);
    void updateMeltdownEvent(World& world, MeltdownEventState& state, double& remaining_time, double deltaTime);
    void updateRainEvent(World& world, RainEventState& state, double deltaTime);

    // Floor obstacle helpers for duck event.
    void spawnFloorObstacle(World& world, DuckEventState& state);
    void clearFloorObstacles(World& world, DuckEventState& state);

    bool isMeltdownActive() const;
    void convertStrayDigitMaterialToWater(World& world, MaterialType digit_material);
    void redrawWalls(World& world);
};

} // namespace DirtSim
