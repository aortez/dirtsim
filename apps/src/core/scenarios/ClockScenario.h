#pragma once

#include "ClockConfig.h"
#include "ClockFontPatterns.h"
#include "clock_scenario/ClockEventTypes.h"
#include "clock_scenario/ColorCycleEvent.h"
#include "clock_scenario/ColorShowcaseEvent.h"
#include "clock_scenario/DoorManager.h"
#include "clock_scenario/DrainManager.h"
#include "clock_scenario/EventManager.h"
#include "clock_scenario/MeltdownEvent.h"
#include "clock_scenario/ObstacleManager.h"
#include "clock_scenario/RainEvent.h"
#include "clock_scenario/StormManager.h"
#include "core/Cell.h"
#include "core/FontSampler.h"
#include "core/scenarios/Scenario.h"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <vector>

namespace DirtSim {

class CharacterMetrics;
class World;
struct WorldData;

/**
 * Clock scenario - displays system time as a digital clock.
 *
 * Supports multiple font styles: 7-segment, large 7-segment, and dot matrix.
 * Format: HH:MM:SS (or HH:MM if seconds disabled).
 *
 * Event system allows multiple concurrent events (rain, duck, etc.).
 */
class ClockScenario : public ScenarioRunner {
public:
    struct TimezoneInfo {
        const char* name;
        const char* label;
        int offset_hours;
    };

    static constexpr std::array<TimezoneInfo, 10> TIMEZONES = { {
        { "Local", "Local System Time", 0 },
        { "UTC", "UTC (Universal)", 0 },
        { "PST", "Los Angeles (PST)", -8 },
        { "MST", "Denver (MST)", -7 },
        { "CST", "Chicago (CST)", -6 },
        { "EST", "New York (EST)", -5 },
        { "GMT", "London (GMT)", 0 },
        { "CET", "Paris (CET)", +1 },
        { "JST", "Tokyo (JST)", +9 },
        { "AEST", "Sydney (AEST)", +10 },
    } };

    // Specifies a wall cell's position and visual appearance.
    struct WallSpec {
        int16_t x;
        int16_t y;
        Material::EnumType render_as; // Visual appearance (WOOD for frame, DIRT for floor, etc.).
    };

    explicit ClockScenario(ClockEventConfigs event_configs = {});
    ~ClockScenario();

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
    bool triggerEvent(World& world, ClockEventType type);

    // Time override for testing.
    void setTimeOverride(const std::string& time_str);
    void clearTimeOverride();

private:
    ScenarioMetadata metadata_;
    Config::Clock config_;
    ClockEventConfigs event_configs_;
    std::string last_drawn_time_;
    std::optional<std::string> time_override_; // For testing.

    // Event system.
    EventManager event_manager_;
    bool first_tick_done_ = false;

    // Managers for sub-systems.
    DoorManager door_manager_;
    DrainManager drain_manager_;
    ObstacleManager obstacle_manager_;
    StormManager storm_manager_;

    std::mt19937 rng_{ std::random_device{}() };
    std::uniform_real_distribution<double> uniform_dist_{ 0.0, 1.0 };

    // FontSampler for LVGL-based fonts (lazy-initialized).
    // Recreated if config_.font changes.
    // Mutable to allow lazy initialization in const methods.
    mutable std::unique_ptr<FontSampler> font_sampler_;
    mutable Config::ClockFont font_sampler_font_ = Config::ClockFont::DotMatrix;
    void ensureFontSamplerInitialized() const;
    const std::vector<std::vector<bool>>& getSampledDigitPattern(int digit) const;

    const CharacterMetrics& getMetrics() const;
    int getColonPadding() const;
    int getColonWidth() const;
    int getDigitGap() const;
    int getDigitHeight() const;
    int getDigitWidth() const;

    int calculateTotalWidth() const;
    void recalculateDimensions();
    void clearDigits(World& world);
    void drawCharacter(
        World& world,
        const std::string& utf8Char,
        int start_x,
        int start_y,
        std::vector<Vector2i>& outDigitPositions);
    void drawCharacterBinary(
        World& world,
        const std::string& utf8Char,
        int start_x,
        int start_y,
        std::vector<Vector2i>& outDigitPositions);
    void drawCharacterWithMaterials(
        World& world,
        const std::string& utf8Char,
        int start_x,
        int start_y,
        std::vector<Vector2i>& outDigitPositions);
    bool getCharacterPixel(const std::string& utf8Char, int row, int col) const;
    void placeDigitPixel(
        World& world,
        int x,
        int y,
        Material::EnumType renderMaterial,
        std::vector<Vector2i>& outDigitPositions);
    void drawTimeString(
        World& world, const std::string& time_str, std::vector<Vector2i>& outDigitPositions);
    void drawTime(World& world, std::vector<Vector2i>& outDigitPositions);
    std::string getCurrentTimeString() const;

    // Event system helpers.
    void updateEvents(World& world, double deltaTime, std::vector<Vector2i>& digitPositions);
    void tryTriggerPeriodicEvents(World& world);
    void tryTriggerTimeChangeEvents(World& world);
    bool isEventAllowed(ClockEventType type) const;
    void startEvent(World& world, ClockEventType type);
    void updateEvent(
        World& world,
        ClockEventType type,
        ActiveEvent& event,
        double deltaTime,
        std::vector<Vector2i>& digitPositions);
    void endEvent(World& world, ClockEventType type, ActiveEvent& event, bool setCooldown = true);
    void cancelAllEvents(World& world);
    double countWaterInBottomThird(const World& world) const;
    double countWaterInTopThird(const World& world) const;
    void processQueuedEvents(World& world);
    bool isEventBlockedByConflict(ClockEventType type) const;
    void queueEvent(ClockEventType type);

    // Event-specific update handlers (called via visitor).
    void updateColorCycleEvent(World& world, ColorCycleEventState& state, double deltaTime);
    void updateColorShowcaseEvent(World& world, ColorShowcaseEventState& state, double deltaTime);
    void updateDigitSlideEvent(
        World& world,
        DigitSlideEventState& state,
        double deltaTime,
        std::vector<Vector2i>& digitPositions);
    void updateDuckEvent(
        World& world, DuckEventState& state, double& remaining_time, double deltaTime);
    bool spawnDuck(World& world, DuckEventState& state);
    void updateMarqueeEvent(
        World& world,
        MarqueeEventState& state,
        double& remaining_time,
        double deltaTime,
        std::vector<Vector2i>& digitPositions);
    void updateMeltdownEvent(
        World& world, MeltdownEventState& state, double& remaining_time, double deltaTime);
    void updateRainEvent(World& world, RainEventState& state, double deltaTime);

    bool isMeltdownActive() const;
    void convertStrayDigitMaterialToWater(World& world, Material::EnumType digit_material);

    Material::EnumType getActiveDigitMaterial() const;
    void updateDigitMaterialOverride();
    Material::EnumType getColorCycleMaterial(const ColorCycleEventState& state) const;
    Material::EnumType getColorShowcaseMaterial(const ColorShowcaseEventState& state) const;

    // Centralized wall system.
    std::vector<WallSpec> generateWallSpecs(const WorldData& data) const;
    void applyWalls(World& world, const std::vector<WallSpec>& walls);
    void redrawWalls(World& world);

    std::optional<Material::EnumType> digit_material_override_;
    std::vector<ClockEventType> queued_events_;
};

} // namespace DirtSim
