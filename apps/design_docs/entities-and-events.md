# Entities and Event System

## Overview

This document describes the entity system and random event system for scenarios, with the Duck entity and Clock scenario events as the first implementation.

## Entity System Architecture

### Core Concepts

**Entities** are sprite-based objects that exist in the world but render as images rather than cell materials. They have:
- Physics state (position, velocity, mass, facing direction)
- Sub-cell precision via COM (just like cell particles)
- World coordinates (not pixel coordinates)
- Serialization support (sent from server to UI for rendering)

**Design Goals:**
- Entities and organisms will eventually merge
- Entity system should be extensible for future creature types
- Clean separation: server handles physics/AI, UI handles rendering

### Entity Data Structure

Location: `src/core/Entity.h`

```cpp
enum class EntityType : uint8_t {
    DUCK = 0,
    SPARKLE = 1,
    // Future: BUTTERFLY, BIRD, FISH, etc.
};

struct Entity {
    uint32_t id;
    EntityType type;
    bool visible;

    // Physics state (all vectors for consistency).
    Vector2<float> position;     // Cell coordinates.
    Vector2<float> com;          // Sub-cell offset [-1, 1].
    Vector2<float> velocity;     // Cells per second.
    Vector2<float> facing;       // Direction (normalized).
    float mass;
};
```

**No entity-specific fields** - These will be added when we have clear patterns for multiple entity types.

### Data Flow

```
Server (ClockScenario)
    ↓
WorldData.entities
    ↓
RenderMessage.entities (binary serialization)
    ↓
UI unpacks (Disconnected.cpp)
    ↓
WorldData.entities (local copy)
    ↓
EntityRenderer draws sprites
```

### Rendering

Location: `src/ui/rendering/EntityRenderer.{h,cpp}`

- **DUCK**: Renders 101×112 duck sprite scaled to 1 cell, flips horizontally based on facing.x
- **SPARKLE**: Renders as yellow cross (5 pixels), opacity from velocity.magnitude()

Entities render on top of cells (after bones, before bilinear filter).

## Duck Entity

### Physics Specifications

**Super Mario Bros Style Movement:**
- **Max speed**: 0.5 cells/second
- **Acceleration**: Slower as speed increases (not constant)
- **Drag**: Up to 10% of max velocity lost per tick
- **Jump**: Acceleration to travel up 2 cells
- **Gravity**: Affected by world gravity (same as cell COMs)
- **Collision**: Ghost mode for now (no collision detection)

**Physics State:**
- `position`: Cell coordinates (float)
- `com`: Sub-cell precision [-1, 1]
- `velocity`: Cells per second
- `mass`: 1.0
- `on_ground`: Boolean (for jump detection)

**Constants (to be defined):**
```cpp
MAX_SPEED = 0.5;           // Cells per second.
DRAG_FACTOR = 0.1;         // 10% of max velocity per second.
JUMP_VELOCITY = ???;       // Calculated to reach 2 cells height.
ACCELERATION_CURVE = ???;  // Slower as speed approaches max.
```

### Duck AI State Machine

**Actions:**
```cpp
enum class Action {
    WAIT,        // Stand still.
    RUN_LEFT,    // Run left 1-5 cells.
    RUN_RIGHT,   // Run right 1-5 cells.
    JUMP         // Jump upward.
};
```

**State:**
```cpp
struct DuckState {
    Action current_action;
    float action_timer;       // Time remaining for action.
    int run_distance;         // Target cells to run (1-5).
    float run_start_x;        // Starting X for run actions.
};
```

**Action Selection:**
- Pick randomly between WAIT, RUN_LEFT, RUN_RIGHT, JUMP
- Run distance: random [1, 5] cells
- Wait duration: TBD
- Actions execute sequentially (one finishes before next starts)

**Implementation Location:**
- `src/core/Duck.{h,cpp}` - Duck class with physics and AI
- ClockScenario creates and updates Duck instances

## Clock Scenario Events

### Event System Design

**Event Types:**
1. **RAIN**: Water drops from sky, drain at bottom center
2. **DUCK**: Duck + sparkles run across screen

**Configuration:**
```cpp
struct ClockConfig {
    double event_frequency;  // [0, 1] - 0 = disabled, 1 = very frequent.
};
```

**Event Flow:**
```
No Event → Timer expires → Pick random event → Event runs → Event ends → Schedule next
    ↑                                                                          ↓
    └──────────────────────────────────────────────────────────────────────────┘
```

**Timing:**
- First event: Triggers immediately when frequency > 0
- Subsequent events: `delay = 30s * (1 - frequency) ± 20% jitter`
- Event selection: Random 50/50 (rain vs duck) - **Currently duck-only for testing**

**Constants (ClockScenario.cpp):**
```cpp
BASE_EVENT_DELAY = 30.0;   // Seconds between events.
RAIN_DURATION = 10.0;      // Rain event duration.
```

### Rain Event

**Behavior:**
- Spawn water drops randomly across top (10 drops/second)
- Drain at bottom center (5 cells wide)
- Water evaporates in drain (50% per tick)
- Interacts with wall borders and clock digits

### Duck Event

**Current Implementation (stationary):**
- Duck spawns at `(width-5, height-5)` near bottom right
- Stationary (velocity = 0)
- 8 sparkles orbit around duck (1.5 cell radius, 2 rad/s)
- Duration: 30 seconds

**Future Implementation (with physics):**
- Duck spawns at random position on floor
- Physics-driven movement with SMB-style controls
- AI chooses actions (run, jump, wait)
- Duration: Until duck exits screen or timeout

## Clock World Modifications

**Wall Border:**
- Added WALL cells around entire world perimeter
- Provides floor for duck to walk on
- Rain water bounces off walls

**Setup Order:**
1. Clear all cells
2. Add wall border (top, bottom, left, right)
3. Draw clock digits (WALL material)

## Implementation Status

**Completed:**
- ✅ Entity struct with physics properties
- ✅ Entity serialization (WorldData → RenderMessage → UI)
- ✅ EntityRenderer for DUCK and SPARKLE types
- ✅ ClockConfig with event_frequency
- ✅ Event system in ClockScenario (rain + duck events)
- ✅ Wall border around clock world
- ✅ Duck spawning with sparkles
- ✅ Entity unpacking on UI side

**In Progress:**
- ⚙️ Duck physics (SMB-style movement)
- ⚙️ Duck AI state machine
- ⚙️ Duck collision detection

**Future:**
- More entity types (butterfly, bird, fish)
- Entity-cell collision
- Jump collision with clock digits
- Multiple ducks
- Entity-entity interaction

## Technical Notes

### Entity vs Organism

Entities and organisms will eventually merge. Key differences currently:
- **Entities**: Sprite-based, single-cell physics, AI-driven
- **Organisms**: Multi-cell structures, cell-based rendering, neural networks

Convergence path:
1. Add organism_id to Entity
2. Allow entities to control organism cells
3. Unify physics (entity COM → organism cell COMs)

### Performance Considerations

- Entities are sent every frame (not sparse like organisms)
- Entity count should stay small (<100) to avoid serialization overhead
- Duck sprite is 1 cell = ~4-10 pixels depending on zoom
- Sparkles are simple (5 pixels each)

### Rendering Pipeline

Entities render after all cell rendering but before post-processing:
1. Clear canvas
2. Draw cells (material colors, borders)
3. Draw debug overlays (COM, velocity, pressure)
4. Draw bones
5. **Draw entities** ← Duck and sparkles
6. Apply bilinear filter (SMOOTH mode only)
7. Invalidate canvas

## Constants Reference

**Duck Physics (planned):**
```cpp
MAX_SPEED = 0.5;              // Cells per second.
DRAG_FACTOR = 0.1;            // 10% max velocity per second.
JUMP_HEIGHT = 2.0;            // Cells.
```

**Event System:**
```cpp
BASE_EVENT_DELAY = 30.0;      // Seconds between events.
RAIN_DURATION = 10.0;         // Rain event duration.
RAIN_DROPS_PER_SECOND = 10.0; // Drop spawn rate.
DRAIN_SIZE = 5;               // Drain width (cells).
SPARKLE_COUNT = 8;            // Sparkles per duck.
SPARKLE_ORBIT_RADIUS = 1.5;   // Cells from duck center.
SPARKLE_ORBIT_SPEED = 2.0;    // Radians per second.
```

## Files Modified/Created

### Core
- `src/core/Entity.h` - Entity struct and EntityType enum
- `src/core/Entity.cpp` - JSON serialization
- `src/core/Duck.h` - Duck class (physics + AI) **[Created, not implemented]**
- `src/core/Duck.cpp` - Duck implementation **[Not yet created]**
- `src/core/WorldData.h` - Added entities field
- `src/core/RenderMessage.h` - Added entities field
- `src/core/RenderMessageUtils.h` - Copy entities in packRenderMessage

### Server
- `src/server/scenarios/scenarios/ClockConfig.h` - Added event_frequency
- `src/server/scenarios/scenarios/ClockScenario.h` - Event system, duck state
- `src/server/scenarios/scenarios/ClockScenario.cpp` - Event implementation

### UI
- `src/ui/rendering/EntityRenderer.h` - Entity rendering interface
- `src/ui/rendering/EntityRenderer.cpp` - Duck and sparkle rendering
- `src/ui/rendering/CellRenderer.cpp` - Calls renderEntities()
- `src/ui/state-machine/states/Disconnected.cpp` - Unpack entities from RenderMessage

### Build
- `CMakeLists.txt` - Added Entity.cpp and EntityRenderer.cpp
