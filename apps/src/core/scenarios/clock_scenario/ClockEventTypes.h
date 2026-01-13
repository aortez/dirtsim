#pragma once

#include "MarqueeTypes.h"
#include "core/LightManager.h"
#include "core/MaterialType.h"
#include "core/StrongType.h"
#include "core/Vector2.h"
#include "core/organisms/OrganismType.h"
#include <optional>
#include <variant>
#include <vector>

namespace DirtSim {

// ============================================================================
// Event System Types
// ============================================================================

enum class ClockEventType {
    COLOR_CYCLE,
    COLOR_SHOWCASE,
    DIGIT_SLIDE,
    DUCK,
    MARQUEE,
    MELTDOWN,
    RAIN
};

// Determines when an event's trigger probability is checked.
enum class EventTriggerType {
    Periodic,     // Checked once per second.
    OnTimeChange, // Checked when displayed time string changes.
};

struct EventTimingConfig {
    EventTriggerType trigger_type = EventTriggerType::Periodic;
    double duration;
    double chance; // Probability per trigger (meaning depends on trigger_type).
    double cooldown;
};

struct ColorCycleEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::OnTimeChange,
                                 .duration = 10.0,
                                 .chance = 0.15,
                                 .cooldown = 15.0 };
};

struct ColorShowcaseEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::OnTimeChange,
                                 .duration = 10.0,
                                 .chance = 0.3,
                                 .cooldown = 60.0 };
    std::vector<Material::EnumType> showcase_materials = { Material::EnumType::Leaf,
                                                           Material::EnumType::Water,
                                                           Material::EnumType::Wood };
};

struct DigitSlideEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::OnTimeChange,
                                 .duration = 5.0,
                                 .chance = 0.5,
                                 .cooldown = 60.0 };
    double animation_speed = 2.0; // Progress per second (1.0 = 1 second to complete slide).
};

struct DuckEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::Periodic,
                                 .duration = 30.0,
                                 .chance = 0.05,
                                 .cooldown = 10.0 };
    bool floor_obstacles_enabled = true;
};

struct MarqueeEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::OnTimeChange,
                                 .duration = 10.0,
                                 .chance = 0.2,
                                 .cooldown = 5.0 };
    double scroll_speed = 100.0; // Units per second.
};

struct MeltdownEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::OnTimeChange,
                                 .duration = 20.0,
                                 .chance = 0.2,
                                 .cooldown = 30.0 };
};

struct RainEventConfig {
    EventTimingConfig timing = { .trigger_type = EventTriggerType::Periodic,
                                 .duration = 20.0,
                                 .chance = 0.01,
                                 .cooldown = 30.0 };
};

// ============================================================================
// Event State Types
// ============================================================================

struct ColorCycleEventState {
    size_t current_index = 0;     // Current position in cycle.
    double time_per_color = 0.0;  // Calculated at start: duration / num_materials.
    double time_in_current = 0.0; // Time spent on current color.
};

struct ColorShowcaseEventState {
    size_t current_index = 0; // Current position in showcase materials list.
};

struct DigitSlideEventState {
    VerticalSlideState slide_state;
};

struct MeltdownEventState {
    // Meltdown lets digits fall, converts to water on impact.
    int digit_bottom_y = 0;              // Scanned at event start: lowest Y with digit material.
    Material::EnumType digit_material{}; // Material type digits become when melting.
};

struct RainEventState {
    // Rain-specific state (could add intensity, pattern, etc.).
};

struct MarqueeEventState {
    HorizontalScrollState scroll_state;
};

enum class DoorSide { LEFT, RIGHT };

using DoorId = StrongType<struct DoorIdTag>;
const DoorId INVALID_DOOR_ID{};

enum class DuckEventPhase {
    DOOR_OPENING, // Door open, waiting before spawning duck.
    DUCK_ACTIVE,  // Duck spawned and walking.
    DOOR_CLOSING  // Duck exited, waiting briefly before closing door.
};

struct DuckEventState {
    OrganismId organism_id = INVALID_ORGANISM_ID;
    DoorSide entrance_side = DoorSide::LEFT;
    DoorId entrance_door_id = INVALID_DOOR_ID;
    DoorId exit_door_id = INVALID_DOOR_ID;
    DuckEventPhase phase = DuckEventPhase::DOOR_OPENING;
    double door_open_timer = 0.0;
    double door_close_timer = 0.0;
    double obstacle_spawn_timer = 0.0;

    // RAII handles for door indicator lights. Auto-removed when event ends.
    std::optional<LightHandle> entrance_light;
    std::optional<LightHandle> exit_light;
};

using EventState = std::variant<
    ColorCycleEventState,
    ColorShowcaseEventState,
    DigitSlideEventState,
    DuckEventState,
    MarqueeEventState,
    MeltdownEventState,
    RainEventState>;

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
    DigitSlideEventConfig digit_slide;
    DuckEventConfig duck;
    MarqueeEventConfig marquee;
    MeltdownEventConfig meltdown;
    RainEventConfig rain;
};

} // namespace DirtSim
