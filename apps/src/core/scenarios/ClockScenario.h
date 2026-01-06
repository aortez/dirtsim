#pragma once

#include "ClockConfig.h"
#include "ClockFontPatterns.h"
#include "clock_scenario/ClockEventTypes.h"
#include "clock_scenario/ColorCycleEvent.h"
#include "clock_scenario/ColorShowcaseEvent.h"
#include "clock_scenario/DoorManager.h"
#include "clock_scenario/MeltdownEvent.h"
#include "clock_scenario/ObstacleManager.h"
#include "clock_scenario/RainEvent.h"
#include "core/Cell.h"
#include "core/scenarios/Scenario.h"
#include <array>
#include <map>
#include <memory>
#include <random>

namespace DirtSim {

class World;

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

    // Door and obstacle management.
    DoorManager door_manager_;
    ObstacleManager obstacle_manager_;

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
    void drawTimeString(World& world, const std::string& time_str);
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
    void updateMarqueeEvent(World& world, MarqueeEventState& state, double& remaining_time, double deltaTime);
    void updateMeltdownEvent(World& world, MeltdownEventState& state, double& remaining_time, double deltaTime);
    void updateRainEvent(World& world, RainEventState& state, double deltaTime);

    bool isMeltdownActive() const;
    void convertStrayDigitMaterialToWater(World& world, MaterialType digit_material);
    void redrawWalls(World& world);
};

} // namespace DirtSim
