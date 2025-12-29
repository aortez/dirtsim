# Configuration System Design

Working document for dirtsim configuration architecture.

## Goals

1. **Scenario persistence** - Start up in a preconfigured scenario (e.g., clock with specific font, seconds display, timezone).
2. **Per-process configs** - Server and UI have different concerns; don't share configs unnecessarily.
3. **Clean separation** - Logging, app settings, and scenarios are distinct concerns.

## Current State

| Config | Location | Scope |
|--------|----------|-------|
| `logging-config.json` | `apps/` | Shared by server + UI |
| `apps/config/pi/` | Various | System-level (labwc, kanshi, desktop) |

**Problems with current approach:**
- Server and UI share logging config but have mostly different channels.
- No scenario or app config exists - everything uses hardcoded defaults.
- UI loads config for physics channels it never uses.

## Proposed Structure

```
/etc/dirtsim/
├── server/
│   ├── config.json      # Scenarios, startup, physics tuning
│   └── logging.json     # Server-specific channels
└── ui/
    ├── config.json      # Display, brightness, UI preferences
    └── logging.json     # UI-specific channels
```

Or flat naming:
```
/etc/dirtsim/
├── server-config.json
├── server-logging.json
├── ui-config.json
└── ui-logging.json
```

## Config Ownership

| Config Type | Owner | Contents |
|-------------|-------|----------|
| Scenarios | Server | Default scenario, per-scenario saved settings |
| Startup | Server | Which scenario to load on boot, auto-start |
| Display | UI | Brightness, rotation, render mode |
| Logging | Per-process | Channel levels, sinks, patterns |

## Logging Channels by Process

| Channel | Server | UI |
|---------|--------|-----|
| brain, tree | ✅ | ❌ |
| physics, swap, cohesion, pressure, collision, friction, support, viscosity | ✅ | ❌ |
| controls, render, ui | ❌ | ✅ |
| scenario | ✅ | ❌ |
| network, state | ✅ | ✅ |

## Server Config Example

```json
{
  "startup": {
    "scenario": "clock",
    "auto_start": true
  },
  "scenarios": {
    "clock": {
      "font": "Segment7Tall",
      "show_seconds": true,
      "timezone_index": 2,
      "event_frequency": 0.5
    },
    "sandbox": {
      "quadrant_enabled": true,
      "water_column_enabled": false,
      "rain_rate": 0.0
    }
  }
}
```

## Implementation Notes

- Use `ReflectSerializer` for automatic JSON conversion (already works for scenario configs).
- Support XDG-style layering: system defaults in `/etc/dirtsim/`, user overrides in `~/.config/dirtsim/`.
- Keep `.local` override pattern from logging (e.g., `config.json.local` for uncommitted tweaks).

## Open Questions

- Should logging configs move into per-process directories, or stay flat?
- Where do shared settings (if any) belong?
- Runtime state persistence (current scenario state, not just config)?

## Next Steps

1. Split logging config into `server-logging.json` and `ui-logging.json`.
2. Add `server-config.json` with startup scenario and scenario defaults.
3. Load config at server startup, apply to `SimRunning::onEnter()`.
4. Optional: Add save/load API commands for runtime persistence.
