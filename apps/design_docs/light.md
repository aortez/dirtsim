# Light System Design

## Overview

The light system calculates illumination across the world grid from multiple sources: ambient light, directional sunlight, and point light sources. Light values are stored per-cell as packed RGBA and used for both rendering and simulation (tree photosynthesis).

## Light Components

Each cell's final color is computed from:

```
cell.color = ambient + direct_sun + Σ(point_lights) + diffused
```

### 1. Ambient Light

Baseline illumination everywhere in the world.

- Runtime-controllable via `LightConfig.ambientColor`
- Simulates scattered/indirect environmental light
- Applied uniformly to all cells before other calculations

### 2. Directional Sunlight

Top-down light source simulating the sun.

- Cast from top of world downward per column
- Attenuated by material opacity as it passes through cells
- Materials can tint light passing through (e.g., leaves tint green)
- Controllable intensity and color via `LightConfig`

**Algorithm:**
```cpp
for each column x:
    Light sun = sunColor * sunIntensity
    for y = 0 to height:  // Top to bottom
        cell[x,y].light += sun

        // Attenuate by material opacity.
        float opacity = material.opacity
        sun = scale(sun, 1.0f - opacity)

        // Apply material tint to transmitted light.
        sun = multiply(sun, material.tint)
```

### 3. Point Light Sources

Localized light sources with position, color, intensity, and falloff.

**Sources:**
- Explicit `PointLight` objects in world
- Emissive materials (cells with `emission > 0`)

**Algorithm:**
```cpp
for each point_light:
    for each cell in radius:
        // Cast ray from light to cell.
        Light received = traceRay(light.position, cell.position, light.color)

        // Apply distance falloff.
        float dist = distance(light.position, cell.position)
        float falloff = 1.0f / (1.0f + dist * dist * attenuation)

        cell.light += scale(received, falloff * light.intensity)
```

**Ray Tracing:**
- Bresenham or DDA line algorithm from light to target
- Accumulate opacity along the ray
- Apply material tints to transmitted light

### 4. Light Diffusion (Scattering)

Spreads light from bright areas into shadowed areas, creating soft shadow edges and ambient fill.

**Algorithm:**
```cpp
for iteration = 0 to diffusionIterations:
    for each cell:
        Light neighborAvg = average of 4 cardinal neighbors
        float scatter = material.scatter
        cell.lightNext = cell.light + scatter * (neighborAvg - cell.light)
    swap(light, lightNext)
```

**Material Scatter Values:**
- Controls how much light a material re-emits vs absorbs
- High scatter (Metal: 0.8) = reflective, bounces light
- Low scatter (Wall: 0.1) = absorbs most light
- Zero scatter = pure absorption

## Data Structures

### Cell Color Storage

```cpp
// In Cell - replaces render_as.
uint32_t color_;  // Packed RGBA
```

The calculator writes final lit color directly. Trees unpack RGB when querying light for photosynthesis.

### LightConfig

```cpp
// In PhysicsConfig.
struct LightConfig {
    // Ambient.
    uint32_t ambientColor = ColorNames::dayAmbient();

    // Sun.
    bool sunEnabled = true;
    uint32_t sunColor = ColorNames::warmSunlight();
    float sunIntensity = 1.0f;

    // Diffusion.
    int diffusionIterations = 2;
    float diffusionRate = 0.3f;
};
```

### Point Light

```cpp
struct PointLight {
    Vector2d position;
    uint32_t color;
    float intensity = 1.0f;
    float radius = 20.0f;      // Max range.
    float attenuation = 0.1f;  // Falloff rate.
};

// In World.
std::vector<PointLight> pointLights_;
```

### Material Light Properties

```cpp
// Add to MaterialProperties.
float opacity = 0.0f;           // Blocks direct light [0-1].
float scatter = 0.0f;           // Re-emits light to neighbors [0-1].
uint32_t tint = ColorNames::white();  // Color filter for transmitted light.
float emission = 0.0f;          // Self-illumination [0-1].
uint32_t emissionColor = ColorNames::white();
```

**Material Values:**

| Material | Opacity | Scatter | Tint | Emission |
|----------|---------|---------|------|----------|
| Air      | 0.0     | 0.0     | white | 0.0 |
| Water    | 0.05    | 0.1     | slight blue | 0.0 |
| Leaf     | 0.4     | 0.3     | green | 0.0 |
| Sand     | 0.7     | 0.2     | warm | 0.0 |
| Dirt     | 0.9     | 0.2     | brown | 0.0 |
| Wood     | 0.95    | 0.15    | warm | 0.0 |
| Seed     | 0.3     | 0.2     | green | 0.1 (faint glow) |
| Root     | 0.9     | 0.15    | brown | 0.0 |
| Metal    | 1.0     | 0.8     | white | 0.0 |
| Wall     | 1.0     | 0.1     | white | 0.0 |

## WorldLightCalculator

New calculator following existing pattern.

```cpp
class WorldLightCalculator {
public:
    void calculate(World& world, const LightConfig& config);

    // ASCII visualization of light levels for testing and debugging.
    // Returns multi-line string with brightness mapped to characters.
    std::string lightMapString(const World& world) const;

private:
    void applyAmbient(World& world, uint32_t ambientColor);
    void applySunlight(World& world, uint32_t sunColor, float intensity);
    void applyPointLights(World& world, const std::vector<PointLight>& lights);
    void applyEmissiveCells(World& world);
    void applyDiffusion(World& world, int iterations, float rate);

    // Ray tracing helper.
    uint32_t traceRay(const World& world, Vector2i from, Vector2i to, uint32_t color);

    // Temporary buffer for diffusion double-buffering.
    std::vector<uint32_t> lightBuffer_;
};
```

### Calculation Order

```cpp
void WorldLightCalculator::calculate(World& world, const LightConfig& config) {
    // 1. Clear to black before accumulating light.
    clearLight(world);

    // 2. Add ambient light.
    applyAmbient(world, config.ambient_color);

    // 3. Add sunlight (top-down).
    if (config.sun_enabled) {
        applySunlight(world, config.sun_color, config.sun_intensity);
    }

    // 4. Add emissive material contributions.
    applyEmissiveCells(world);

    // 5. Diffuse/scatter light.
    applyDiffusion(world, config.diffusion_iterations, config.diffusion_rate);

    // 6. Apply material base colors (multiply light by material color).
    applyMaterialColors(world);
}
```

### Integration with World

Light calculation runs **after scenario tick, before organisms** so trees have current light values for photosynthesis:

```cpp
void World::advanceTime(double deltaTime) {
    // 1. Physics forces (gravity, pressure, cohesion, adhesion, friction, etc.)
    // 2. Movement/transfers
    // 3. Light calculation <-- trees need this for photosynthesis
    lightCalculator_.calculate(*this, physicsConfig_.light);
    // 4. Organisms (trees, etc.)
    treeManager_.update(*this, deltaTime);
}
```

### Integration Checklist

Files requiring changes:

| File | Change |
|------|--------|
| `Cell.h/cpp` | Add `color_` field (replaces or alongside `render_as`) |
| `MaterialType.h/cpp` | Add opacity, scatter, tint, emission to MaterialProperties |
| `PhysicsConfig.h` | Add `LightConfig light` member |
| `World.h/cpp` | Add `lightCalculator_`, `pointLights_`, accessors |
| `CellRenderer.cpp` | Read `cell.getColor()` instead of computing from material |
| `WorldData.h` | Ensure `color_` is serialized (if new field) |

Optional additions:
- API commands: `LightConfigGet`, `LightConfigSet`, `PointLightAdd`
- UI: Light section in PhysicsPanel or dedicated LightPanel

## Tree Integration

Trees query cell light values for photosynthesis calculations.

```cpp
// In Tree or TreeSensoryData gathering.
float getLightAtCell(const Cell& cell) {
    uint32_t color = cell.getColor();
    // Convert to intensity (simple: average RGB).
    float r = ColorNames::getRf(color);
    float g = ColorNames::getGf(color);
    float b = ColorNames::getBf(color);
    return (r + g + b) / 3.0f;
}

// Photosynthesis uses light on LEAF cells.
for each leaf_cell in tree:
    float light = getLightAtCell(leaf_cell)
    energy += light * photosynthesisRate * deltaTime
```

## Rendering Integration

The renderer uses `cell.color_` directly instead of computing color from material type.

```cpp
// CellRenderer - simplified.
uint32_t color = cell.getColor();
// Draw cell with this color.
```

This means the light calculator must run before rendering, and must set reasonable colors even with lights disabled.

## UI Controls

Expose in a LightPanel or PhysicsPanel section:

- **Ambient color** - Color picker or presets (day/dusk/night/cave)
- **Sun enabled** - Toggle
- **Sun intensity** - Slider 0.0 - 2.0
- **Sun color** - Color picker or presets
- **Diffusion iterations** - Slider 0 - 5
- **Diffusion rate** - Slider 0.0 - 1.0

## Implementation Phases

### Phase 1: Foundation ✓ COMPLETE

1. ✓ Add `color_` field to Cell (alongside render_as)
2. ✓ Add `LightProperties` struct to MaterialType.h
3. ✓ Add light properties to all 10 materials in MaterialProperties
4. ✓ Create WorldLightCalculator with calculate() and lightMapString()
5. ✓ Add `LightConfig` struct to PhysicsSettings.h
6. ✓ Implement ambient light, sunlight with opacity/tinting, emissive cells, diffusion
7. ✓ Add 7 passing unit tests

### Phase 2: Integration (In Progress)

1. ✓ Integrate WorldLightCalculator into World::advanceTime()
2. ✓ Update CellRenderer to use cell.getColor() instead of getMaterialColor()
3. ✓ Add color to BasicCell transport (server→UI)
4. Add basic UI controls (sun toggle, intensity)
5. Tune material light properties for good visuals

### Phase 3: Point Lights (In Progress)

1. ✓ Implement PointLight struct and storage in World
2. ✓ Implement ray tracing for point lights (Bresenham with opacity/tinting)
3. ✓ Add World::addPointLight(), clearPointLights(), getPointLights()
4. ✓ Integrate applyPointLights() into calculate() sequence
5. Unit tests added (need advanceTime(0) call to rebuild grid cache before testing)
6. Add API commands for adding/removing point lights

### Phase 4: Tree Integration

1. Update TreeSensoryData to include light values
2. Implement photosynthesis using light
3. Test trees growing toward light

## Testing

### lightMapString() Visualization

The calculator provides `lightMapString()` for ASCII visualization of light levels:

```cpp
std::string WorldLightCalculator::lightMapString(const World& world) const {
    const char* shades = " .:-=+*#%@";  // 10 levels, dark to bright.
    std::string result;
    result.reserve((world.getWidth() + 1) * world.getHeight());

    for (int y = 0; y < world.getHeight(); y++) {
        for (int x = 0; x < world.getWidth(); x++) {
            float b = ColorNames::brightness(world.getCell(x, y).getColor());
            int idx = std::min(9, static_cast<int>(b * 10));
            result += shades[idx];
        }
        result += '\n';
    }
    return result;
}
```

### Test Examples

**Sunlight through empty column:**
```cpp
TEST(WorldLightCalculator, SunlightEmptyColumn) {
    World world(10, 10);
    WorldLightCalculator calc;
    LightConfig config;
    config.ambientColor = ColorNames::black();

    calc.calculate(world, config);

    // All cells should be bright (full sun).
    std::string lightMap = calc.lightMapString(world);
    // Expected: all '@' characters (max brightness).
    EXPECT_THAT(lightMap, testing::Each(testing::AnyOf('@', '\n')));
}
```

**Sunlight blocked by opaque material:**
```cpp
TEST(WorldLightCalculator, SunlightBlockedByWall) {
    World world(10, 10);
    // Wall across middle.
    for (int x = 0; x < 10; x++) {
        world.getCell(x, 3).setMaterialType(MaterialType::Wall);
    }

    WorldLightCalculator calc;
    LightConfig config;
    config.ambientColor = ColorNames::black();

    calc.calculate(world, config);

    std::string lightMap = calc.lightMapString(world);
    // Expected pattern (10x10):
    // @@@@@@@@@@   <- rows 0-2: lit
    // @@@@@@@@@@
    // @@@@@@@@@@
    // @@@@@@@@@@   <- row 3: wall (lit from above)
    //               <- rows 4-9: dark (spaces)

    // Verify cells below wall are dark.
    for (int x = 0; x < 10; x++) {
        EXPECT_LT(ColorNames::brightness(world.getCell(x, 5).getColor()), 0.1f);
    }
}
```

**Point light in dark room:**
```cpp
TEST(WorldLightCalculator, PointLightInDarkRoom) {
    World world(20, 20);
    // Ceiling blocks sun.
    for (int x = 0; x < 20; x++) {
        world.getCell(x, 0).setMaterialType(MaterialType::Wall);
    }

    // Point light in center.
    PointLight torch{{10, 10}, ColorNames::torchOrange(), 1.0f, 8.0f, 0.1f};
    world.addPointLight(torch);

    WorldLightCalculator calc;
    LightConfig config;
    config.sunEnabled = true;
    config.ambientColor = ColorNames::caveAmbient();

    calc.calculate(world, config);

    std::string lightMap = calc.lightMapString(world);
    std::cout << lightMap;  // Visual inspection.

    // Light at source should be bright.
    EXPECT_GT(ColorNames::brightness(world.getCell(10, 10).getColor()), 0.8f);
    // Light at edge of radius should be dimmer.
    EXPECT_LT(ColorNames::brightness(world.getCell(18, 10).getColor()), 0.3f);
}
```

**Diffusion softens shadows:**
```cpp
TEST(WorldLightCalculator, DiffusionSoftensShadows) {
    World world(20, 10);
    // Pillar creates shadow.
    for (int y = 0; y < 5; y++) {
        world.getCell(10, y).setMaterialType(MaterialType::Wall);
    }

    WorldLightCalculator calc;
    LightConfig config;
    config.ambientColor = ColorNames::black();

    // Without diffusion.
    config.diffusionIterations = 0;
    calc.calculate(world, config);
    float shadowNoDiffuse = ColorNames::brightness(world.getCell(10, 7).getColor());

    // With diffusion.
    config.diffusionIterations = 3;
    calc.calculate(world, config);
    float shadowWithDiffuse = ColorNames::brightness(world.getCell(10, 7).getColor());

    // Diffusion should brighten shadowed area slightly.
    EXPECT_GT(shadowWithDiffuse, shadowNoDiffuse);
}
```

### Test File Organization

```
src/core/tests/
├── ColorNames_test.cpp           // RGBA utilities, brightness.
├── WorldLightCalculator_test.cpp // Light calculations.
```

## Performance Considerations

Initial implementation prioritizes correctness over speed. Potential optimizations if needed:

- **Dirty columns**: Only recalculate sunlight for columns where cells changed
- **Sparse point lights**: Skip cells outside light radius
- **Reduced diffusion**: Fewer iterations, or only near shadow boundaries
- **Lower frequency**: Calculate light every N frames instead of every frame
- **SIMD**: Vectorize color operations

## Open Questions

1. Should light calculation happen every frame or on-demand?
2. Do we need HDR (float colors) internally, or is 8-bit sufficient?
3. Should emissive cells count as point lights, or use a simpler radial falloff?
4. How does fill_ratio affect opacity? (Partially filled cells less opaque?)

## References

- ColorNames.h/cpp - Color constants and RGBA utilities
- GridMechanics.md - Physics system architecture
- plant.md - Tree organism design (photosynthesis requirements)
