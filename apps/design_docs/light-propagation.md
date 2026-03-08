# Propagation-Based Light System

## Motivation

The current lighting system (`WorldLightCalculator`) was built up incrementally and uses ~10
separate heuristic passes per frame: sunlight, side light, diagonal light, bounce, ambient,
emissive cells, emissive overlay, point light ray tracing, diffusion, and material colors. Each
pass has its own tuning knobs (side_light_intensity, bounce_intensity, shadow_decay_rate,
diagonal_light_enabled, etc.) that interact in non-obvious ways and don't generalize well across
world sizes.

This document describes a replacement: a unified **directional light propagation** model where
all illumination effects — direct shadows, fill light, bounce, color bleeding, ambient occlusion
— emerge from a single mechanism.

## Core Concept

Each cell stores light traveling through it in **8 compass directions**. Each simulation step,
light advances one cell along its direction, interacting with materials as it passes through.
Sources inject light at their positions. Materials absorb, transmit, and scatter light based on
their physical properties.

This is a **discrete ordinates** approach to radiative transfer, applied to a 2D grid.

### Key Differences from Current System

| Current (heuristic) | Propagation (unified) |
|---|---|
| 10 separate passes with distinct logic | One propagation step, repeated K times |
| Side/diagonal light fakes sky illumination | Sky light emerges from scatter physics |
| Bounce is a horizontal-only sweep | Multi-directional bounce emerges from scatter |
| Diffusion is isotropic (loses direction) | Light preserves direction; only scatter redirects |
| ~1100 lines, 14 config knobs | ~200-300 lines, 7 config knobs |
| Always correct but always expensive | Incremental; cost proportional to change |

## Data Structures

### Directional Light Field

Each cell stores the intensity of light passing through it in each of 8 directions:

```cpp
// 8 compass directions for light propagation.
enum class LightDir : uint8_t {
    N, NE, E, SE, S, SW, W, NW,
    COUNT
};

// Per-cell directional light. 8 channels x RgbF = 96 bytes/cell.
struct DirectionalLight {
    RgbF channel[8] = {};

    // Total visible light at this cell (sum of all incoming directions).
    RgbF total() const;
};
```

Two full grids for double-buffering (read from old, write to new, swap each step):

```cpp
GridBuffer<DirectionalLight> light_field_;
GridBuffer<DirectionalLight> light_field_next_;
```

### Memory Budget

| Component | Per cell | 200x150 grid |
|-----------|----------|--------------|
| DirectionalLight | 96 bytes | 2.88 MB |
| Double buffer | 192 bytes | 5.76 MB |
| Optical cache (optional) | 96 bytes | 2.88 MB |
| **Total** | **288 bytes** | **8.64 MB** |

Well within Pi 5 capacity.

### Direction Utilities

```cpp
// Upstream neighbor offset for each direction.
// Light heading SOUTH at (x,y) came from (x, y-1).
constexpr Vector2i upstream(LightDir d);

// Opposite direction (for reverse-direction specular reflection).
constexpr LightDir opposite(LightDir d);

// Weight for diagonal vs cardinal (diagonal = 1/sqrt(2)).
constexpr float dirWeight(LightDir d);
```

## Material Properties

### Existing Properties (unchanged meaning)

```cpp
float opacity;           // Fraction of light intercepted [0-1].
float scatter;           // Of intercepted light, fraction re-emitted vs absorbed [0-1].
uint32_t tint;           // Color filter applied to transmitted and scattered light.
float saturation;        // Material base color strength (rendering, not transport).
float emission;          // Self-illumination intensity [0-1].
uint32_t emission_color; // Color of emitted light.
```

### New Property

```cpp
float specularity = 0.0f;  // Scatter distribution: 0 = diffuse, 1 = mirror [0-1].
```

Controls the directional distribution of scattered light:
- **0.0 (diffuse/Lambertian)**: Scattered light distributes uniformly across all 8 directions.
  Rough surfaces: dirt, sand, wood, wall.
- **1.0 (mirror/specular)**: Scattered light goes entirely to the reverse direction. Polished
  surfaces: metal, still water.
- **0.0-1.0 (glossy)**: Linear blend. Wet surfaces, brushed metal.

v1 uses reverse direction as the mirror reflection. A future version could infer surface normals
from neighbor occupancy for physically correct reflection angles.

### Material Table

| Material | opacity | scatter | specularity | tint | emission | Behavior |
|----------|---------|---------|-------------|------|----------|----------|
| Air      | 0.0     | 0.0     | 0.0         | white | 0.0     | Fully transparent |
| Dirt     | 0.7     | 0.2     | 0.0         | brown | 0.0     | Absorptive, weak diffuse scatter |
| Leaf     | 0.1     | 0.3     | 0.0         | green | 0.0     | Mostly transparent, green-tinted, diffuse scatter |
| Metal    | 1.0     | 0.8     | 0.8         | white | 0.0     | Opaque, strong specular reflection |
| Root     | 0.7     | 0.15    | 0.0         | brown | 0.0     | Similar to dirt |
| Sand     | 0.4     | 0.2     | 0.1         | warm  | 0.0     | Semi-transparent, faint glint |
| Seed     | 0.3     | 0.2     | 0.0         | green | 0.1     | Faint green glow |
| Wall     | 1.0     | 0.3     | 0.0         | white | 0.0     | Opaque, diffuse reflection |
| Water    | 0.03    | 0.5     | 0.6         | blue  | 0.0     | Nearly transparent, specular surface |
| Wood     | 0.6     | 0.2     | 0.05        | warm  | 0.0     | Semi-opaque, barely glossy |

## Propagation Algorithm

### One Step

Each step advances all light by one cell in its direction of travel. Run K steps per frame.

```
for each cell (x, y):
    material = world.material(x, y)
    fill     = world.fill_ratio(x, y)
    props    = material.light_properties

    eff_opacity = props.opacity * fill
    transmit    = 1.0 - eff_opacity
    eff_tint    = lerp(white, props.tint, fill)

    for each direction D:
        // Pull light heading in direction D from the upstream neighbor.
        (ux, uy) = upstream(x, y, D)
        incoming = old_field[ux, uy].channel[D]

        // --- Transmission (light passing through without interacting) ---
        through = incoming * transmit * eff_tint

        // --- Scattering (intercepted light that is re-emitted) ---
        intercepted = incoming * eff_opacity
        scattered = intercepted * props.scatter * eff_tint

        // Forward the non-scattered transmitted light.
        new_field[x, y].channel[D] += through

        // Distribute scattered light.
        specular_amount = scattered * props.specularity
        diffuse_amount  = scattered * (1.0 - props.specularity)

        // Specular: reverse direction.
        new_field[x, y].channel[opposite(D)] += specular_amount

        // Diffuse: uniform across all 8 directions, weighted by cardinal/diagonal.
        for each direction D':
            new_field[x, y].channel[D'] += diffuse_amount * uniform_weight[D']
```

Where `uniform_weight` normalizes cardinal (1.0) and diagonal (0.707) contributions to sum to
1.0.

### Source Injection

After each propagation step, inject light from sources:

```
// --- Sunlight ---
// Direct sun enters from above as strong southbound light.
for x in 0..width:
    new_field[x, 0].channel[S] += sun_color * sun_intensity

// --- Sky dome ---
// Diffuse sky light enters from above at multiple angles.
for x in 0..width:
    new_field[x, 0].channel[S]  += sky_color * sky_intensity * 0.5
    new_field[x, 0].channel[SE] += sky_color * sky_intensity * 0.25
    new_field[x, 0].channel[SW] += sky_color * sky_intensity * 0.25

// --- Emissive materials ---
// Cells with emission > 0 inject light in all directions.
for each cell (x, y) where emission > 0:
    for each direction D:
        new_field[x, y].channel[D] += emission_color * emission * (1.0 / 8.0)

// --- Emissive overlay ---
// Scenario-controlled per-cell emission (e.g., clock digits).
for each overlay cell (x, y):
    for each direction D:
        new_field[x, y].channel[D] += overlay_color * overlay_intensity * (1.0 / 8.0)

// --- Point lights ---
for each point_light:
    for each direction D:
        new_field[light.x, light.y].channel[D] += light.color * light.intensity * (1.0 / 8.0)

// --- Spot lights ---
// Inject only into directions within the spot's cone.
for each spot_light:
    for each direction D:
        if angle_of(D) within spot.arc_width of spot.direction:
            factor = angular_falloff(D, spot.direction, spot.arc_width, spot.focus)
            new_field[light.x, light.y].channel[D] += light.color * light.intensity * factor
```

### Per-Frame Pipeline

```cpp
void LightPropagator::calculate(World& world, const LightConfig& config, Timers& timers) {
    auto& data = world.getData();
    ensureBufferSizes(data.width, data.height);

    {
        ScopeTimer t(timers, "light_propagate");
        for (int step = 0; step < config.steps_per_frame; ++step) {
            clearField(light_field_next_);
            propagateStep(data);
            injectSources(world, config);
            std::swap(light_field_, light_field_next_);
        }
    }

    {
        ScopeTimer t(timers, "light_ambient");
        applyAmbient(data, config);
    }

    {
        ScopeTimer t(timers, "light_store_raw");
        storeRawLight(data);
    }

    {
        ScopeTimer t(timers, "light_material_colors");
        applyMaterialColors(data);
    }
}
```

### Ambient Light

Ambient is still a uniform baseline, applied after propagation. It represents multiply-scattered
environmental light that is too diffuse to model directionally. With a good propagation model
and enough steps, ambient can be set very low — most "fill" light emerges naturally from scatter.

```
for each cell (x, y):
    visible = light_field_[x, y].total() + ambient_color * ambient_intensity
    data.colors[x, y] = visible
```

### Material Colors (Rendering)

Final rendering step, unchanged from current system:

```
for each cell (x, y):
    data.colors[x, y] *= lerp(white, material_base_color, saturation)
```

## LightConfig

The config simplifies from 14 fields to 7:

```cpp
struct LightConfig {
    uint32_t sun_color;
    float sun_intensity;
    uint32_t sky_color;         // Diffuse sky hemisphere.
    float sky_intensity;
    uint32_t ambient_color;     // Minimum floor.
    float ambient_intensity;
    int steps_per_frame;        // Speed of light / quality knob.
};
```

### What Gets Removed

| Removed field | Why |
|---|---|
| `side_light_enabled` | Sky dome + scatter replaces side light |
| `side_light_intensity` | Controlled by sky_intensity |
| `diagonal_light_enabled` | Sky dome SE/SW channels replace diagonal |
| `diagonal_light_intensity` | Controlled by sky_intensity |
| `bounce_intensity` | Emerges from material scatter |
| `shadow_decay_rate` | Emerges from sky dome channels |
| `diffusion_iterations` | Replaced by steps_per_frame |
| `diffusion_rate` | Replaced by material scatter properties |
| `air_scatter_rate` | Air's scatter property (0.0) handles this |

### Suggested Defaults

```cpp
LightConfig getDefaultLightConfig() {
    return {
        .sun_color = ColorNames::warmSunlight(),
        .sun_intensity = 1.0f,
        .sky_color = ColorNames::skyBlue(),
        .sky_intensity = 0.4f,
        .ambient_color = ColorNames::dayAmbient(),
        .ambient_intensity = 0.05f,    // Much lower than current — scatter provides fill.
        .steps_per_frame = 15,
    };
}
```

## UI Controls

### Current Controls (replaced)

The current Light section in PhysicsPanel has 14 controls. Most exist only because the heuristic
system needs manual tuning knobs for each separate pass:

| Current control | Disposition |
|---|---|
| Sun On (toggle) | Replaced by setting sun intensity to 0 |
| Sun intensity (slider) | **Keep** |
| Sun color (preset dropdown) | **Keep** |
| Ambient color (preset dropdown) | **Keep** |
| Ambient intensity (slider) | **Keep** (lower default) |
| Diffusion iterations (slider) | Gone — steps_per_frame controls convergence |
| Diffusion rate (slider) | Gone — material scatter IS this |
| Air scatter rate (slider) | Gone — air's scatter property |
| Bounce intensity (slider) | Gone — emerges from scatter |
| Diagonal on (toggle) | Gone — emerges from sky dome |
| Diagonal intensity (slider) | Gone — sky_intensity controls this |
| Side on (toggle) | Gone — emerges from sky dome |
| Shadow decay rate (slider) | Gone — sky dome channels handle this |
| Side light intensity (slider) | Gone — sky_intensity controls this |

9 of 14 controls disappear because the physics handles what they were manually faking.

### New Panel Layout

```
Light
  Sun       [||||||||---]  1.0      // Direct sun intensity.
  SunC      [Warm Sunlight v]       // Sun color preset.
  Sky       [|||||------]  0.4      // Diffuse sky hemisphere (replaces side/diagonal/bounce).
  Ambient   [||--------]   0.05     // Minimum visibility floor.
  AmbientC  [Day Ambient v]         // Ambient color preset.
```

Five controls, each with clear physical meaning:

- **Sun intensity**: How bright the direct sun beam is. Drives shadow contrast.
- **Sun color**: Warm sunlight, cool moonlight, torch orange, candle yellow, white.
- **Sky intensity**: How much diffuse light enters from the sky hemisphere. This single slider
  replaces side light, diagonal light, bounce, and shadow decay. High = soft/overcast,
  low = harsh shadows.
- **Ambient intensity**: Minimum light floor. With good scatter, this can be very low — most
  fill comes from real scattered light, not faked ambient.
- **Ambient color**: Day, dusk, night, cave presets.

### Time-of-Day Presets

A preset dropdown can drive all five controls at once for common lighting moods:

| Preset | Sun | Sun color | Sky | Ambient | Ambient color |
|---|---|---|---|---|---|
| Noon | 1.0 | Warm sunlight | 0.5 | 0.05 | Day |
| Overcast | 0.3 | White | 0.8 | 0.1 | Day |
| Dusk | 0.4 | Torch orange | 0.2 | 0.1 | Dusk |
| Night | 0.0 | — | 0.0 | 0.03 | Night |
| Cave | 0.0 | — | 0.0 | 0.01 | Cave |

Night and cave modes make point lights and emissive cells the dominant light sources. Individual
sliders remain available for fine-tuning after selecting a preset.

### Steps Per Frame

`steps_per_frame` is a performance/quality knob, not a lighting parameter. It controls how fast
light propagates (convergence speed) vs per-frame compute cost. It does not belong in the main
Light panel. Options:

- **Fixed default** (15): Good balance for Pi 5. No UI control needed.
- **Advanced/debug section**: Expose only in a debug panel for performance tuning.
- **Automatic**: Adaptive — run more steps when the light field is changing, fewer when stable.

## Convergence

### Speed of Light

Each step advances light by one cell. With K steps per frame at F fps, light travels K*F
cells per second.

| steps_per_frame | 60 fps | 30 fps | Frames for 150-tall grid |
|---|---|---|---|
| 5  | 300 cells/s | 150 cells/s | 30 frames (0.5s) |
| 10 | 600 cells/s | 300 cells/s | 15 frames (0.25s) |
| 15 | 900 cells/s | 450 cells/s | 10 frames (0.17s) |
| 20 | 1200 cells/s | 600 cells/s | 8 frames (0.13s) |

Direct sunlight (straight down) converges in `ceil(height / K)` frames. Scattered fill light
takes additional frames but builds up visibly — it looks like natural light filling in, not like
something is broken.

### Steady State

The system reaches steady state when the light field stops changing between steps. This happens
when source injection balances absorption. No explicit convergence check is needed — the physics
guarantees stability as long as total outgoing light <= total incoming light per cell, which
holds because `transmit + scatter <= 1.0` by construction.

### World Changes

When the world changes (cell moves, material placed/removed), the light field naturally adjusts
over subsequent steps. Shadows fill in, newly exposed areas illuminate. No dirty tracking is
required for correctness, though it could be added as an optimization (skip propagation steps
when the field has converged).

## Point Lights and Spot Lights

Point lights inject at their grid position into all 8 channels. The propagation handles
occlusion naturally — light heading toward an opaque cell is absorbed, creating shadows without
explicit ray tracing.

Spot lights inject only into channels within their angular cone. A spotlight pointing east
injects into E, NE, SE channels with angular falloff weighting. The cone naturally propagates
outward through the grid.

Rotating lights update their injection direction each frame.

### Falloff

Point/spot light intensity naturally falls off with distance because:
1. Light spreads across 8 directions (geometric spreading).
2. Each step through air preserves energy but distributes it across a growing wavefront.
3. Any non-zero air scatter further reduces beam intensity.

If additional falloff control is needed, a per-light radius and attenuation can scale the
injection intensity, but the propagation itself provides physically motivated falloff.

## Emergent Effects

Effects that currently require separate passes but emerge automatically from propagation:

### Fill Light on Vertical Surfaces
Sky dome light entering at SE/SW angles strikes vertical surfaces and scatters, illuminating
them. No side light pass needed.

### Soft Shadow Edges
Light heading SE/SW partially wraps around obstacle edges. After several scatter events, some
light reaches behind obstacles. Shadows are sharp directly behind an obstacle and soften at
edges — physically correct penumbra.

### Multi-Bounce Illumination
Sunlight hits ground, scatters upward (N, NE, NW channels), illuminates ceiling. Ceiling
scatters back down. Each propagation step adds one more bounce level. No separate bounce pass
needed.

### Color Bleeding
Sunlight hits orange dirt, scatters as orange-tinted light into nearby shadows. Green leaves
scatter green-tinted light onto surfaces below.

### Ambient Occlusion
Narrow caves and crevices receive less light because rays must scatter multiple times to
penetrate deep. Each scatter event loses energy (some absorbed). Tight spaces are naturally
darker.

### Specular Highlights (Metal, Water)
Light hitting a high-specularity surface reflects directionally. A metal floor bounces sunlight
upward as a concentrated beam. Water surfaces create glints.

## Integration

### Rendering (CellRenderer)

Unchanged. The propagator writes to `data.colors` just like the current calculator. CellRenderer
reads `cell.getColor()`.

### Photosynthesis (Trees)

Unchanged. Trees read light intensity from cell color: `(R+G+B)/3`.

### Raw Light Buffer (Entity Sprites)

Unchanged. `storeRawLight()` packs `data.colors` to `raw_light_` after propagation.

### Emissive Overlay

Unchanged interface. `setEmissive(x, y, color, intensity)` adds to source injection.

### LightManager

Unchanged. Point/spot/rotating light lifecycle management works the same way. The propagator
reads from `LightManager::forEachLight()` during source injection.

### LightHandHeld (Organism Flashlights)

Unchanged. Updates spot light direction/position in LightManager. The propagator picks it up
during source injection.

## Implementation Plan

### Phase 1: Core Propagation

1. Add `specularity` field to `LightProperties` (default 0.0 for all materials).
2. Create `LightPropagator` class with `DirectionalLight` grid and double buffer.
3. Implement `propagateStep()` — the inner loop.
4. Implement source injection for sun, sky, emissive cells.
5. Implement `calculate()` with K steps, ambient, store raw, material colors.
6. Wire into `World::advanceTime()` alongside existing calculator (behind a flag or config).
7. Unit tests: empty world convergence, wall shadow, material attenuation.
8. Set material specularity values (metal=0.8, water=0.6, sand=0.1, wood=0.05).

### Phase 2: Light Sources

1. Point light source injection (all 8 channels at position).
2. Spot light source injection (angular cone channels).
3. Rotating light support (direction update + spot injection).
4. Emissive overlay injection.
5. Unit tests: point light in dark room, spot light cone.

### Phase 3: Tuning and Cutover

1. Tune `steps_per_frame` default for Pi 5 performance.
2. Tune material scatter/specularity values for visual quality.
3. Tune sky_intensity and ambient_intensity for natural fill.
4. Compare visual output with current system, iterate.
5. Remove old `WorldLightCalculator` and its config fields.
6. Update LightPanel UI to new config fields.

### Phase 4: Enhancements (Future)

1. Surface-normal-aware specular reflection (infer normals from neighbors).
2. Dirty tracking: skip propagation steps when light field has converged.
3. Adaptive steps_per_frame: more steps when field is changing, fewer when stable.
4. Per-light radius clamping for performance (skip injection outside radius).

## Testing

### Unit Tests

```cpp
// Direct sunlight reaches bottom of empty world after enough steps.
TEST(LightPropagator, SunlightReachesBottom) {
    World world(10, 20);
    LightPropagator prop;
    LightConfig config = getDefaultLightConfig();
    config.steps_per_frame = 25;  // Enough to traverse height.
    config.ambient_intensity = 0.0f;

    prop.calculate(world, config, timers);

    // Bottom row should be illuminated.
    float brightness = world.getData().colors.at(5, 19).brightness();
    EXPECT_GT(brightness, 0.5f);
}

// Wall creates shadow below it.
TEST(LightPropagator, WallCreatesShadow) {
    World world(10, 20);
    // Solid wall across row 5.
    for (int x = 0; x < 10; ++x)
        world.setCell(x, 5, MaterialType::Wall, 1.0f);

    LightPropagator prop;
    LightConfig config = getDefaultLightConfig();
    config.steps_per_frame = 25;
    config.ambient_intensity = 0.0f;

    prop.calculate(world, config, timers);

    // Above wall: bright.
    EXPECT_GT(world.getData().colors.at(5, 3).brightness(), 0.5f);
    // Below wall: dark (only scattered light).
    EXPECT_LT(world.getData().colors.at(5, 15).brightness(), 0.1f);
}

// Metal reflects light specularly.
TEST(LightPropagator, MetalReflects) {
    World world(10, 20);
    // Metal floor at row 15.
    for (int x = 0; x < 10; ++x)
        world.setCell(x, 15, MaterialType::Metal, 1.0f);

    LightPropagator prop;
    LightConfig config = getDefaultLightConfig();
    config.steps_per_frame = 40;
    config.sky_intensity = 0.0f;
    config.ambient_intensity = 0.0f;

    prop.calculate(world, config, timers);

    // Above metal: illuminated by reflected light (N channel).
    float above_metal = world.getData().colors.at(5, 10).brightness();
    // Same height in world without metal would be darker (only downward light).
    EXPECT_GT(above_metal, 0.1f);
}

// Emissive cell illuminates surroundings.
TEST(LightPropagator, EmissiveCellGlows) {
    World world(20, 20);
    // Block sun.
    for (int x = 0; x < 20; ++x)
        world.setCell(x, 0, MaterialType::Wall, 1.0f);
    // Emissive seed in center.
    world.setCell(10, 10, MaterialType::Seed, 1.0f);

    LightPropagator prop;
    LightConfig config = getDefaultLightConfig();
    config.sun_intensity = 0.0f;
    config.sky_intensity = 0.0f;
    config.ambient_intensity = 0.0f;
    config.steps_per_frame = 15;

    prop.calculate(world, config, timers);

    // Seed cell: bright.
    EXPECT_GT(world.getData().colors.at(10, 10).brightness(), 0.05f);
    // Nearby cell: some light.
    EXPECT_GT(world.getData().colors.at(12, 10).brightness(), 0.01f);
    // Far cell: dark.
    EXPECT_LT(world.getData().colors.at(19, 19).brightness(), 0.01f);
}
```

### Visual Testing

`lightMapString()` ASCII visualization works the same way — reads from `data.colors` which the
propagator writes to.

## Performance Estimate

### Per Step

Inner loop: 8 directions x (1 read + a few multiplies + scatter distribution) per cell.
Approximately 50-80 FLOPs per cell per step.

For 200x150 grid: 30,000 cells x 8 dirs x ~10 ops = ~2.4M operations per step.

### Per Frame

At K=15 steps/frame: ~36M operations. This is comparable to the current system's 10 passes of
simpler per-cell work. The inner loop is tighter (one uniform operation vs 10 different
operations), which should be more cache-friendly.

OpenMP parallelization applies: rows or blocks of cells are independent within a step (reading
from old buffer, writing to new buffer).

### Comparison

| | Current | Propagation (K=15) |
|---|---|---|
| Grid passes/frame | ~10 (heterogeneous) | 15 (uniform) |
| Code paths | 10 distinct functions | 1 inner loop |
| Cache behavior | Mixed (different access patterns) | Uniform (sequential scan) |
| OpenMP granularity | Per-pass | Per-step (finer) |

Actual performance will need benchmarking on Pi 5. The uniform inner loop and better cache
behavior may compensate for the higher step count.
