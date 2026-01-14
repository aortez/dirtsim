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

## Config File Locations

### Search Order

Config files are searched in this order (first match wins):

1. `--config-dir` flag (explicit override)
2. `./config/` (CWD - for development)
3. `~/.config/dirtsim/` (user overrides)
4. `/etc/dirtsim/` (system defaults - Pi deployment)

### Development Layout

Developers run from the `apps/` directory. Configs live in `apps/config/`:

```
apps/
├── config/
│   ├── server.json
│   ├── server-logging.json
│   ├── ui.json
│   └── ui-logging.json
├── build-debug/bin/
│   ├── cli
│   ├── dirtsim-server
│   └── dirtsim-ui
└── Makefile
```

Running `make run` or `./build-debug/bin/cli run-all` from `apps/` finds configs in `./config/`.

### Production Layout (Pi)

Yocto bakes configs into the image at `/etc/dirtsim/`:

```
/etc/dirtsim/
├── server.json
├── server-logging.json
├── ui.json
└── ui-logging.json
```

User overrides (if needed) go in `~/.config/dirtsim/` and take precedence.

### .local Override Pattern

For local tweaks not committed to git, use `.local` suffix:

```
apps/config/server.json.local    # Full replacement for server.json
apps/config/ui-logging.json.local
```

The `.local` file is a **complete replacement**, not a merge. If you want to customize, copy the whole file and modify it. This keeps the logic simple and makes it clear exactly what config is being used.

All `.local` files should be gitignored.

## Implementation Notes

- Use `ReflectSerializer` for automatic JSON conversion (already works for scenario configs).
- Config search: check `.local` first, then base file, at each search path.
- Keep backward compatibility: if no config found, use hardcoded defaults.

## Open Questions

- Runtime state persistence (current scenario state, not just config)?
- Should configs be hot-reloadable, or require restart?

## Next Steps

1. Split logging config into `server-logging.json` and `ui-logging.json`.
2. Add `server-config.json` with startup scenario and scenario defaults.
3. Load config at server startup, apply to `SimRunning::onEnter()`.
4. Optional: Add save/load API commands for runtime persistence.
