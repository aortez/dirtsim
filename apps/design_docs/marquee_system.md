# Marquee System

Visual effects for clock digit display, supporting scrolling, waves, and custom animations.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  VIRTUAL SPACE                                          │
│                                                         │
│    Content placed at arbitrary positions                │
│    (can extend beyond visible area)                     │
│                                                         │
│         ┌─────────────────┐                             │
│         │    VIEWPORT     │  ← clips to world bounds    │
│         │   (pan/zoom)    │                             │
│         └─────────────────┘                             │
└─────────────────────────────────────────────────────────┘
```

## Data Flow

1. **Effect** receives current time string (e.g., "1 2 : 3 4")
2. **Effect** produces a `MarqueeFrame`:
   - `digits`: vector of `DigitPlacement` (char + x/y in virtual space)
   - `viewportX/Y`: pan offset
   - `zoom`: scale factor
   - `finished`: signals effect completion
3. **Renderer** transforms placements by viewport, clips, draws visible digits

## Key Types

```cpp
struct DigitPlacement {
    char c;      // '0'-'9', ':', ' '
    double x;
    double y;
};

struct MarqueeFrame {
    std::vector<DigitPlacement> digits;
    double viewportX = 0.0;
    double viewportY = 0.0;
    double zoom = 1.0;
    bool finished = false;
};

// Effect state (variant-based, not inheritance)
struct HorizontalScrollState {
    double viewport_x = 0.0;
    double content_width = 0.0;
    double visible_width = 0.0;
    double speed = 100.0;
    bool scrolling_out = true;
    // Layout params stored at start.
    int digit_width, digit_gap, colon_width;
};

using MarqueeEffectState = std::variant<HorizontalScrollState>;
```

## Implemented Effects

### HorizontalScroll ✅

Scrolls digits off-screen left, teleports viewport, then scrolls back in from the right.

**Phases:**
1. `scrolling_out=true`: Viewport moves right (content moves left) until off-screen
2. Teleport: `viewport_x` jumps to `-visible_width`
3. `scrolling_out=false`: Viewport moves right until `viewport_x=0`
4. `finished=true`

**Config:**
- `scroll_speed`: Units per second (default: 100)
- Currently triggers with 100% chance, 5-second cooldown

## Planned Effects

- **VerticalWave**: Apply `sin(x + phase)` vertical offsets to each digit for a wavy animation.
- **ZoomPulse**: Scale digits in/out with zoom factor.
- **Custom**: User-provided effect function for arbitrary animations.

## Integration

Marquee is a clock scenario event following the standard lifecycle:

1. **Event System**: `MARQUEE` added to `ClockEventType` enum
2. **Config**: `MarqueeEventConfig` with timing and scroll_speed
3. **State**: `MarqueeEventState` wraps the effect state variant
4. **Tick**: When active, `updateMarqueeEvent()` runs the effect and renders the frame instead of normal `drawTime()`

The effect ends early when `MarqueeFrame.finished=true` (sets `remaining_time=0`).

## Files

- `clock_scenario/MarqueeTypes.h` - Data types, effect states, layout helpers ✅
- `clock_scenario/MarqueeTypes.cpp` - Layout and effect implementations ✅
- `clock_scenario/tests/MarqueeTypes_test.cpp` - Unit tests ✅
- `clock_scenario/ClockEventTypes.h` - Event enum, config, state structs ✅
- `ClockScenario.cpp` - Event integration ✅

## Status

**Done:**
- Core types (`DigitPlacement`, `MarqueeFrame`)
- Layout helpers (`layoutString`, `calculateStringWidth`)
- `HorizontalScrollState` and update function
- Event system integration (starts, updates, ends, cooldown)
- Unit tests (21 tests passing)
- UI control button to manually trigger marquee effect

**Next:**
- Add more effects (VerticalWave, etc.)
- Tune timing and speed
