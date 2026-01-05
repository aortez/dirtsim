#pragma once

#include "core/MaterialType.h"
#include "core/Vector2.h"
#include "core/organisms/OrganismType.h"
#include <variant>
#include <vector>

namespace DirtSim {

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

struct ColorCycleEventConfig {
    EventTimingConfig timing = { .duration = 10.0, .chance_per_second = 0.04, .cooldown = 15.0 };
};

struct ColorShowcaseEventConfig {
    EventTimingConfig timing = { .duration = 120.0, .chance_per_second = 0.1, .cooldown = 60.0 };
    std::vector<MaterialType> showcase_materials = {
        MaterialType::LEAF, MaterialType::WATER, MaterialType::SEED, MaterialType::WOOD
    };
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

// ============================================================================
// Event State Types
// ============================================================================

struct ColorCycleEventState {
    size_t current_index = 0;      // Current position in cycle.
    double time_per_color = 0.0;   // Calculated at start: duration / num_materials.
    double time_in_current = 0.0;  // Time spent on current color.
};

struct ColorShowcaseEventState {
    size_t current_index = 0;  // Current position in showcase materials list.
};

struct MeltdownEventState {
    // Meltdown lets digits fall, converts to water on impact.
    int digit_bottom_y = 0;         // Scanned at event start: lowest Y with digit material.
    MaterialType digit_material{};  // Material type digits become when melting.
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
    double door_open_timer = 0.0;
    double door_close_timer = 0.0;
    double obstacle_spawn_timer = 0.0;
};

using EventState = std::variant<ColorCycleEventState, ColorShowcaseEventState, DuckEventState, MeltdownEventState, RainEventState>;

struct ActiveEvent {
    EventState state;
    double remaining_time;
};

// ============================================================================
// Aggregated Event Configs
// ============================================================================

struct ClockEventConfigs {
    ColorCycleEventConfig color_cycle;
    ColorShowcaseEventConfig color_showcase;
    DuckEventConfig duck;
    MeltdownEventConfig meltdown;
    RainEventConfig rain;
};

} // namespace DirtSim
