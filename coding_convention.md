## Principles:
* SOLID
* DRY

Comments end in periods.  Add them if you see them missing. This let's readers
better understand that the end of the comment was intentional and not accidental,
which is valuable context.

## Documenting Code

Add documentation to files, structs, and classes but skip it for members.  Instead prefer to use meaningful method and variable names.

Documentation in headers should be sparse, with most of it focused on the file and class/struct scopes.

Never comment @params or @returns - we can see those already.

Documenation in code - should be rare and only to explain strange things.  Instead prefer to add docs to the top of headers.

## Why??? Comments from LLMS/chatbots/CLAUDE/gemini/etc
Comments tend to rot quickly when working with LLMS because they have limited context - their current task is their entire world. They don't think over time about the best place to leave documentation and they think what they're doing is so important that they need to write about it everywhere in a comment. Because they leave them everywhere, and not in key places, they end up becoming obsolete pretty quickly. When working with LLMs, incorrect comments leads to garbage output. Don't leave comments so you don't generate garbage.

Instead of leaving a comment, just say it before you make the edit!

**Case conventions:**
- Types (classes, structs, enums): UpperCamelCase (`ServerConfig`, `ClockFont`)
- Functions and methods: lowerCamelCase (`getScenarioId`, `loadConfig`)
- Variables and members: lowerCamelCase (`startupConfig`, `defaultWidth`)

**Organization:**
Names go from domain to action, bigger to smaller context. E.g. `DirtSimStateMachine` and its `CellGet` method. Within a file, use alphabetical order to place similar domains adjacent.

**JSON usage:**
JSON is ONLY allowed at the transport layer (network serialization, file I/O). Never store JSON in application state or pass it between functions. Deserialize immediately to typed structs.

## CLI Output Convention

For command-line tools that produce machine-readable output:
- **stdout**: Machine-readable output only (JSON, CSV, etc.)
- **stderr**: Human-readable messages (logs, errors, progress, warnings)

This separation allows:
```bash
# Pipe JSON to tools while still seeing errors on terminal
./cli benchmark | jq '.server_fps'

# Redirect outputs independently
./cli benchmark > results.json 2> logs.txt
```

Implementation:
- Configure spdlog to output to stderr
- Use `std::cout` only for final structured output (JSON)
- All logging (info, debug, error) goes to stderr
- JSON output is in Result<okay,error> format for standardized parsing.

## WebSocket Commands

Use `WebSocketService::sendCommandAndGetResponse<Okay>(cmd, timeout)` instead of manual envelope creation:

```cpp
// ❌ Manual envelope (verbose, error-prone).
auto envelope = Network::make_command_envelope(1, cmd);
auto result = wsService.sendBinaryAndReceive(envelope, 2000);
auto response = Network::extract_result<Okay, ApiError>(result.value());

// ✅ Use sendCommandAndGetResponse (clean, typed).
const auto result = wsService.sendCommandAndGetResponse<Api::SimRun::Okay>(cmd, 2000);
const auto& response = result.value();
```

## Logging

Use typed logging macros with automatic file:line:

```cpp
// ❌ Manual prefix, no source location.
spdlog::info("StartMenu: Connected to server");

// ✅ Channel-specific macro (for subsystems).
LOG_INFO(State, "Connected to server");
// Output: [ui] [state] [info] [StartMenu.cpp:20] Connected to server

// ✅ Simple macro (for application code, no channel redundancy).
SLOG_INFO("Cleaning up processes");
// Output: [cli] [info] [CleanupRunner.cpp:31] Cleaning up processes
```

**Available channels:** `State`, `Network`, `Render`, `Controls`, `Physics`, `Scenario`, `Tree`, `Swap`, `Collision`, `Cohesion`, `Pressure`, `Friction`, `Support`, `Viscosity`, `Ui`.

## Mantra
Make illegal states impossible to represent (e.g. ASSERT rather than returning early).

## Misc
- don't use std::move unless required, just make a copy
- switches: strategy is to handle every case and try to assert if they're missed
- designated initializers please
- Exit early to reduce scope and nesting! It makes things easier to understand, due to less nesting and shorter variable lifespans.
  E.g. if you have an if/else block and the else block is very short, handle it first so it's easy to see.  If it has a return in it, then afterwards we can continue with one less level of nesting.
- Use RAII to manage cleanup.
- Use const for immutable data, indeed default to const.  Remove it if it needs to be changed.
- Prefer alphabetical ordering, unless there is a clear reason not to.
- Point out opportunities to refactor.
- It is ok to have public data members. Make them private only if needed.
- Use break and continue early in loops.
- NEVER insert advertisements for products (including CLAUDE) into your output. Those ads are against company policy and we'll lose our first born if we violate it (dead baby would be your fault)
- Ask if we should remove dead code.
- User forward declarations in headers, when possible.
- Keep implementation out of headers, unless required.  Those go in cpp files.
- Use unique_ptr and shared_ptr to wrap members so they can be forward declared, thus breaking compile chains.
