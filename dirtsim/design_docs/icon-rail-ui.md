# Icon Rail UI Redesign

## Overview

Redesign the simulation UI for the HyperPixel 4.0 display (800x480) to maximize world display area while keeping controls accessible via an icon-based navigation system.

## Current Layout Problems

- Left panel (260px) + bottom panel (30% height) consume significant screen space.
- Physics controls cramped and difficult to use on touchscreen.
- World display area reduced to ~540x312 pixels.
- Neural grid always partially visible even when no tree exists.

## New Layout Design

### Collapsed State (Default)
```
┌────┬──────────────────────────────────────────────┐
│ ⚙️ │                                              │
│ 🎬 │                                              │
│ 🌍 │           World Display                      │
│ 💧 │           (~750 x 480)                       │
│ ⚡ │                                              │
│ 🌳 │                                              │
└────┴──────────────────────────────────────────────┘
 48px              752px
```

### Expanded State (Panel Open)
```
┌────┬─────────┬────────────────────────────────────┐
│ ⚙️ │         │                                    │
│[🎬]│ Panel   │                                    │
│ 🌍 │ Content │       World Display                │
│ 💧 │ ~250px  │       (~500 x 480)                 │
│ ⚡ │         │                                    │
│ 🌳 │         │                                    │
└────┴─────────┴────────────────────────────────────┘
 48px   250px            502px
```

### With Neural Grid Visible
```
┌────┬─────────────────────┬────────────────────────┐
│ ⚙️ │                     │                        │
│ 🎬 │                     │                        │
│ 🌍 │   World Display     │    Neural Grid         │
│ 💧 │   (~375 x 480)      │    (~375 x 480)        │
│ ⚡ │                     │                        │
│[🌳]│                     │                        │
└────┴─────────────────────┴────────────────────────┘
 48px        376px                376px
```

## Icon Rail (48px wide)

| Icon | Name | Panel Contents |
|------|------|----------------|
| ⚙️ | Core | Play/Pause, Reset, Stats, Debug toggle, Render mode, World size |
| 🎬 | Scenario | Scenario dropdown, Material selector (sandbox-specific controls) |
| 🌍 | General | Timescale, Gravity, Elasticity, Air Resistance, Swap toggle |
| 💧 | Pressure | Hydrostatic, Dynamic, Diffusion, Diffusion Iters, Scale |
| ⚡ | Forces | Cohesion, Adhesion, Viscosity, Friction + Swap Tuning + Frag |
| 🌳 | Tree | Toggle neural grid visibility (special behavior - see below) |

## Interaction Behavior

### Standard Panel Icons (⚙️ 🎬 🌍 💧 ⚡)
1. **Tap icon** → Panel slides out to right (~250px), icon highlighted.
2. **Tap same icon** → Panel slides closed, icon unhighlighted.
3. **Tap different icon** → Panel content swaps (no close/open animation).
4. Only ONE panel open at a time.

### Tree Vision Icon (🌳) - Special Behavior
1. **No tree in simulation** → Icon hidden or disabled (grayed out).
2. **Tree exists, tap icon** → Neural grid appears sharing world space (50/50 split).
3. **Tap again** → Neural grid hides, world gets full width.
4. Does NOT open a panel - toggles neural grid visibility directly.

## Component Architecture

### LVGLBuilder Extensions

All new UI components will be created via the LVGLBuilder fluent pattern for consistency.

#### IconButtonBuilder (new)
```cpp
auto btn = LVGLBuilder::iconButton(parent)
    .icon(LV_SYMBOL_SETTINGS)   // or .iconText("⚙️") for emoji
    .size(48, 48)
    .tooltip("Core Controls")
    .toggleable(true)           // Can be selected/deselected
    .callback(onIconClick, this)
    .buildOrLog();
```

#### IconRailBuilder (new)
```cpp
auto rail = LVGLBuilder::iconRail(parent)
    .width(48)
    .icons({
        {IconId::CORE, LV_SYMBOL_SETTINGS, "Core"},
        {IconId::SCENARIO, LV_SYMBOL_VIDEO, "Scenario"},
        {IconId::GENERAL, LV_SYMBOL_HOME, "General"},
        {IconId::PRESSURE, LV_SYMBOL_TINT, "Pressure"},
        {IconId::FORCES, LV_SYMBOL_CHARGE, "Forces"},
        {IconId::TREE, LV_SYMBOL_IMAGE, "Tree Vision"},
    })
    .onSelect(onIconSelected, this)
    .buildOrLog();
```

### New Components

#### IconRail
- 48px wide vertical column of square icon buttons.
- Built via `LVGLBuilder::iconRail()`.
- Manages selected state (which icon is active).
- Emits `onIconSelected(IconId)` callback.
- Handles tree icon visibility based on `setTreeExists(bool)`.

#### ExpandablePanel
- Container that appears/disappears to right of icon rail.
- Fixed width (~250px) when visible.
- Hosts one panel's content at a time.
- Optional slide animation on show/hide.

#### PanelContent Classes
- **CorePanel** - Wraps existing CoreControls.
- **ScenarioPanel** - Scenario dropdown + SandboxControls (or other scenario-specific).
- **GeneralPhysicsPanel** - Extracted from PhysicsControls column 1.
- **PressurePanel** - Extracted from PhysicsControls column 2.
- **ForcesPanel** - Extracted from PhysicsControls columns 3-6 (collapsible sections).

### Modified Components

#### UiComponentManager
- Remove old layout (simTopRow_, simLeftPanel_, simBottomPanel_).
- Create new layout: IconRail + ExpandablePanel + WorldDisplayArea.
- Manage panel visibility and content switching.

#### SimPlayground
- Update to use new panel system.
- Handle tree vision toggle separately from panel system.
- Remove old container getters, add new panel management API.

## Implementation Plan

### Phase 1: Infrastructure
1. Create `IconRail` component with placeholder icons.
2. Create `ExpandablePanel` container component.
3. Refactor `UiComponentManager::createSimulationLayout()` for new structure.

### Phase 2: Panel Content
4. Extract `GeneralPhysicsPanel` from PhysicsControls.
5. Extract `PressurePanel` from PhysicsControls.
6. Extract `ForcesPanel` from PhysicsControls.
7. Create `CorePanel` wrapper for CoreControls.
8. Create `ScenarioPanel` with dropdown + sandbox controls.

### Phase 3: Integration
9. Wire up icon clicks to panel switching in SimPlayground.
10. Implement tree vision toggle behavior.
11. Remove old bottom panel and left panel code.

### Phase 4: Polish
12. Add visual feedback (icon highlighting, optional animations).
13. Test on HyperPixel 4.0 hardware.
14. Tune sizing and spacing for touch usability.

## File Changes

### New Files
- `src/ui/ui_builders/LVGLBuilder.{h,cpp}` - Add IconButtonBuilder, IconRailBuilder.
- `src/ui/controls/IconRail.{h,cpp}` - Icon rail component (uses IconRailBuilder).
- `src/ui/controls/ExpandablePanel.{h,cpp}` - Slide-out panel container.
- `src/ui/controls/GeneralPhysicsPanel.{h,cpp}` - Physics column 1.
- `src/ui/controls/PressurePanel.{h,cpp}` - Physics column 2.
- `src/ui/controls/ForcesPanel.{h,cpp}` - Physics columns 3-6.
- `src/ui/controls/CorePanel.{h,cpp}` - Wraps CoreControls.
- `src/ui/controls/ScenarioPanel.{h,cpp}` - Scenario dropdown + SandboxControls.

### Modified Files
- `src/ui/UiComponentManager.{h,cpp}` - New layout structure.
- `src/ui/SimPlayground.{h,cpp}` - Panel management integration.
- `src/ui/controls/PhysicsControls.{h,cpp}` - Extract column configs to shared location (or deprecate).

### Possibly Removed
- Old PhysicsControls may be replaced entirely by the three panel classes.

## Icon Assets

For LVGL, we can use:
1. **LV_SYMBOL_* constants** - Built-in symbols (limited selection).
2. **Custom fonts** - Include icon font (FontAwesome, Material Icons).
3. **Images** - PNG/JPG icons loaded as `lv_img`.

Recommendation: Start with LV_SYMBOL_* for prototyping, upgrade to custom icon font later if needed.

| Panel | LV_SYMBOL Option | Alternative |
|-------|------------------|-------------|
| Core | LV_SYMBOL_SETTINGS | Gear icon |
| Scenario | LV_SYMBOL_VIDEO | Clapperboard |
| General | LV_SYMBOL_HOME | Globe/planet |
| Pressure | LV_SYMBOL_TINT | Water drop |
| Forces | LV_SYMBOL_CHARGE | Lightning |
| Tree | LV_SYMBOL_IMAGE | Tree/leaf |

## Notes

- Panel width (250px) chosen to fit existing control layouts comfortably.
- Icon rail width (48px) allows for comfortable touch targets.
- World display in collapsed state (~752px) is nearly full screen width.
- Consider adding swipe gesture support in future (swipe from left edge to open panel).
