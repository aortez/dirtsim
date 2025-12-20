# WebSocketService Design Specification

## Overview

**WebSocketService** is a unified WebSocket library supporting both client and server roles simultaneously, with type-safe command/response patterns and dual protocol support (binary zpp_bits + JSON).

**Location:** `src/core/network/WebSocketService.{h,cpp}`

**Purpose:** Type-safe network communication with minimal boilerplate.

## Design Principles

### 1. Dual-Role Architecture

A single `WebSocketService` instance can act as both client and server:

**Client role:**
- Connect to remote endpoints
- Send typed commands, receive typed responses
- Handle unsolicited messages via callbacks

**Server role:**
- Listen for incoming connections
- Handle commands via registered handlers
- Broadcast or send directed messages to clients

Example - UI StateMachine uses both:
```cpp
// Server role: listen for CLI/browser commands
wsService_->listen(7070);
wsService_->registerHandler<UiApi::StatusGet::Cwc>(handler);

// Client role: connect to DSSM server for world data
wsService_->connect("ws://localhost:8080");
wsService_->sendCommand<Api::StateGet::Command>({});
```

### 2. Protocol Flexibility

Two wire formats, auto-detected by WebSocket frame type:

**Binary (zpp_bits):**
- Zero-copy serialization
- Compact representation
- Used for high-frequency data (RenderMessages at 60fps)

**JSON:**
- Human-readable
- Browser-native (JavaScript)
- Used by CLI and web dashboard

The service tracks which protocol each client uses and responds in the same format.

### 3. Type Safety

Commands are strongly-typed structs:

```cpp
// Define command structure
namespace Api::StateGet {
    struct Command { };  // No parameters needed
    struct Okay {
        std::string state;
        uint32_t width;
        uint32_t height;
    };
    using Response = Result<Okay, ApiError>;
    using Cwc = CommandWithCallback<Command, Response>;
}

// Compiler enforces correct types
auto result = wsService->sendCommand<Api::StateGet::Command>({});
if (result.isOkay()) {
    auto state = result.value();  // Type: StateGet::Okay
    spdlog::info("Server state: {}", state.state);
}
```

Type errors caught at compile time. No casting, no variant visiting at call sites.

### 4. Zero-Boilerplate Handlers

Register command handlers in one line:

```cpp
// Immediate response
wsService->registerHandler<Api::StatusGet::Cwc>([](Api::StatusGet::Cwc cwc) {
    cwc.sendResponse(Api::StatusGet::Response::okay(getStatus()));
});

// Queued to state machine
wsService->registerHandler<Api::SimRun::Cwc>([this](Api::SimRun::Cwc cwc) {
    queueEvent(cwc);  // State machine calls sendResponse() when ready
});
```

Infrastructure handles deserialization, response serialization, protocol detection, and error handling.

### 5. Directed Messaging

Send messages to specific clients (not just broadcast):

```cpp
// Connection IDs auto-assigned when clients connect
std::string connId = wsService->getConnectionId(ws);

// Send to specific client
wsService->sendToClient(connId, jsonMessage);
```

**Use case:** WebRTC signaling - ICE candidates must go to the specific peer that initiated the stream, not broadcast to all connected browsers.

## Architecture

### Component Diagram

```
┌────────────────────────────────────────────────────┐
│              WebSocketService                      │
│                                                    │
│  Client Methods          Server Methods            │
│  ───────────────         ────────────────          │
│  • connect(url)          • listen(port)            │
│  • sendCommand<T>()      • registerHandler<T>()    │
│  • disconnect()          • sendToClient(id, msg)   │
│  • onBinary(callback)    • broadcastBinary(data)   │
│                          • stopListening()         │
│                                                    │
│  Internal Infrastructure:                          │
│  • Correlation ID tracking (request/response)      │
│  • Per-client protocol detection (binary/JSON)     │
│  • Connection registry (ID ↔ WebSocket mapping)    │
│  • Result<T, Error> return types                   │
└────────────────────────────────────────────────────┘
```

### Message Envelope (Binary Protocol)

Binary messages use a typed envelope:

```cpp
struct MessageEnvelope {
    uint64_t id;                     // Correlation ID for matching requests/responses
    std::string message_type;        // "StateGet", "StateGet_response", etc.
    std::vector<std::byte> payload;  // Command or Result (zpp_bits serialized)
};
```

The entire envelope is zpp_bits serialized before sending.

### CommandWithCallback Pattern

Commands are bundled with their response callbacks:

```cpp
template <typename CommandT, typename ResponseT>
struct CommandWithCallback {
    CommandT command;
    std::function<void(ResponseT)> callback;

    void sendResponse(ResponseT&& response);  // Invoke callback once
};
```

**Benefits:**
- Handler receives everything needed to respond
- Can queue CWC to state machine for async processing
- Response callback already knows which client to send to
- Prevents double-send bugs (asserts if called twice)

## Usage Examples

### Server: Accept Connections and Handle Commands

```cpp
// Create and configure service
auto wsService = std::make_unique<Network::WebSocketService>();

// Register binary handlers (one-liners)
wsService->registerHandler<Api::StateGet::Cwc>([this](Api::StateGet::Cwc cwc) {
    cwc.sendResponse(Api::StateGet::Response::okay(getCurrentState()));
});

wsService->registerHandler<Api::SimRun::Cwc>([this](Api::SimRun::Cwc cwc) {
    queueEvent(cwc);  // State machine responds later
});

// Set up JSON protocol support (for CLI/browser clients)
wsService->setJsonDeserializer([](const std::string& json) -> std::any {
    CommandDeserializerJson deserializer;
    auto result = deserializer.deserialize(json);
    if (result.isError()) {
        throw std::runtime_error(result.errorValue().message);
    }
    return result.value();  // Return ApiCommand variant in std::any
});

wsService->setJsonCommandDispatcher([this](
    std::any cmdAny,
    std::shared_ptr<rtc::WebSocket> ws,
    uint64_t correlationId,
    HandlerInvoker invokeHandler) {
    // Cast to your variant type and dispatch
    ApiCommand cmd = std::any_cast<ApiCommand>(cmdAny);
    // Use macros or visitor pattern to invoke registered handlers
});

// Start listening
auto result = wsService->listen(8080);
```

### Client: Connect and Send Commands

```cpp
// Create service
auto wsService = std::make_unique<Network::WebSocketService>();

// Connect to server
auto connectResult = wsService->connect("ws://localhost:8080");
if (connectResult.isError()) {
    spdlog::error("Connection failed: {}", connectResult.errorValue());
    return;
}

// Send typed command (blocks until response)
auto stateResult = wsService->sendCommand<Api::StateGet::Command>({});
if (stateResult.isOkay()) {
    auto state = stateResult.value();
    spdlog::info("World size: {}x{}", state.width, state.height);
}

// Handle broadcasts
wsService->onBinary([](const std::vector<std::byte>& data) {
    // Deserialize RenderMessage and update display
});

// Cleanup
wsService->disconnect();
```

### Commands That Need Follow-Up Messages

For commands that need to send additional messages after the initial response (like WebRTC ICE candidates):

**1. Add `connectionId` field to command:**
```cpp
struct StreamStart::Command {
    std::string clientId;     // Application-level client identifier
    std::string connectionId; // Auto-populated by registerHandler
};
```

**2. Infrastructure auto-populates it:**
```cpp
// In registerHandler template (automatic)
if constexpr (requires { cwc.command.connectionId; }) {
    cwc.command.connectionId = getConnectionId(ws);
}
```

**3. Handler uses it for directed messaging:**
```cpp
wsService->registerHandler<UiApi::StreamStart::Cwc>([this](UiApi::StreamStart::Cwc cwc) {
    std::string connectionId = cwc.command.connectionId;

    // Create callback for follow-up messages
    auto sendIceCandidate = [this, connectionId](const std::string& json) {
        wsService_->sendToClient(connectionId, json);
    };

    // Process command with callback
    std::string offer = webRtcStreamer_->initiateStream(
        cwc.command.clientId,
        sendIceCandidate  // Called later when ICE candidates are ready
    );

    // Send initial response
    cwc.sendResponse(UiApi::StreamStart::Response::okay({
        .initiated = true,
        .sdpOffer = offer
    }));
});
```

## Key Design Decisions

### Dual Protocol: Binary + JSON

**Why support both?**
- **Binary:** Performance-critical paths (60fps world updates, ~100KB per frame)
- **JSON:** Human tooling (CLI output), browser compatibility, debugging

**How it works:**
- WebSocket frame type determines protocol (binary frame vs text frame)
- Service tracks per-client preference
- Response sent in same format as request

**Trade-off:** Slight complexity in handler registration, but enables gradual migration and tool compatibility.

### std::any for Generic Variant Support

**Problem:** Different components use different command variant types
- Server: `ApiCommand` (20 server commands)
- UI: `UiApiCommand` (15 UI commands)

**Solution:** JsonCommandDispatcher uses `std::any`
```cpp
using JsonCommandDispatcher = std::function<void(
    std::any cmdVariant,  // Can hold ApiCommand OR UiApiCommand
    std::shared_ptr<rtc::WebSocket> ws,
    uint64_t correlationId,
    HandlerInvoker invokeHandler
)>;
```

Each component's dispatcher casts to its own variant type. Type-safe within each component, flexible across components.

### String-Based Connection IDs

**Alternative considered:** Store `std::shared_ptr<rtc::WebSocket>` in command structs.

**Problems with WebSocket pointers:**
- Not serializable (zpp_bits can't handle shared_ptr to opaque types)
- Tight coupling to libdatachannel
- Can't work across process boundaries

**String-based IDs:**
- Serializable
- Decoupled from transport layer
- Service manages ID→WebSocket mapping internally
- Auto-generated ("conn_1", "conn_2", etc.)

**When to use:** Add `std::string connectionId;` field to commands that need directed follow-up messaging (WebRTC signaling, streaming protocols, etc.).

### Synchronous vs Async Responses

**WebRTC case study:** StreamStart originally used async callback for SDP offer.

**Original flow:**
1. StreamStart arrives
2. Immediate response: `{"initiated": true}`
3. Later (async): SDP offer sent via broadcast callback
4. Browser waits for separate offer message

**New flow:**
1. StreamStart arrives
2. Generate SDP offer synchronously
3. Response includes offer: `{"initiated": true, "sdpOffer": "v=0..."}`
4. Browser processes offer immediately

**Benefits:**
- Faster connection setup (~20ms vs ~200ms)
- Simpler browser code (no separate message listener)
- Cleaner request/response semantics
- Trickle ICE is modern WebRTC standard

**Trade-off:** Handler blocks slightly longer (~20ms), but it's worth it for cleaner design.

## Wire Protocol

### Binary Frame

```
WebSocket Binary Frame
│
├─ MessageEnvelope (zpp_bits serialized)
│  ├─ id: uint64_t
│  ├─ message_type: string
│  └─ payload: vector<byte>
│     │
│     └─ Command struct OR Result<Okay, Error> (zpp_bits)
```

### JSON Frame

```json
// Command
{
  "id": 12345,
  "command": "StateGet"
}

// Success Response
{
  "id": 12345,
  "success": true,
  "state": "Idle",
  "width": 100,
  "height": 100
}

// Error Response
{
  "id": 12345,
  "error": "World not initialized"
}
```

## Implementation Details

### Handler Registration Flow

When you call `registerHandler<CwcT>(handler)`:

1. **Extract command name** from `CommandT::name()`
2. **Create generic wrapper** that:
   - Deserializes payload → typed Command
   - Populates `connectionId` (if field exists)
   - Creates CWC with response callback
   - Detects client protocol (JSON/binary)
   - Serializes response in appropriate format
3. **Store in command registry** keyed by command name

### Connection ID Auto-Population

Uses C++20 `requires` for compile-time field detection:

```cpp
if constexpr (requires { cwc.command.connectionId; }) {
    cwc.command.connectionId = getConnectionId(ws);
}
```

Zero overhead for commands without the field. Commands opt-in by declaring:
```cpp
struct MyCommand {
    // ... other fields ...
    std::string connectionId;  // Infrastructure fills this
};
```

### Error Handling

**Expected errors** (network timeout, command validation failure):
- Returned as `Result<T, Error>`
- Sent back to client with correlation ID
- Client handles gracefully

**Programmer errors** (invalid serialization, null checks):
- Throw exceptions
- Logged and connection may close
- Should not happen in correct code

### Thread Safety

- **Client-side:** Blocking `sendCommand` uses mutex + condition variable for response waiting
- **Server-side:** Handler callbacks may be invoked from WebSocket thread
- **State machine queueing:** Use `queueEvent(cwc)` to move processing to main thread

## Best Practices

### When to Use Directed Messaging

Add `connectionId` to commands that need follow-up messages:
- ✅ WebRTC signaling (ICE candidates after initial offer)
- ✅ Streaming protocols (chunked data delivery)
- ✅ Long-running operations with progress updates
- ❌ Simple request/response (use CWC callback instead)

### When to Queue vs Respond Immediately

**Immediate response** (in registerHandler callback):
- Read-only queries (StatusGet, StateGet)
- Fast operations (<1ms)
- No state machine interaction needed

**Queued response** (via state machine):
- State transitions (SimRun, Reset)
- Operations requiring exclusive access
- Anything that might take >1ms

### Protocol Selection

**Use binary when:**
- High frequency (>10Hz updates)
- Large payloads (>1KB)
- Between internal services

**Use JSON when:**
- CLI tool output (humans read it)
- Browser clients (JavaScript native)
- Debugging protocol issues
- External tool integration

## Component Integration

### Server (DSSM)
- Listens on port 8080
- 20 API commands registered
- Broadcasts RenderMessages to connected UIs (binary)
- Accepts JSON from CLI for debugging

### UI Client
- Dual role: client (to server) + server (for CLI/browser)
- Connects to server port 8080 for world data (binary)
- Listens on port 7070 for control commands (JSON from browser)
- 15 UI commands registered

### CLI Tool
- Pure client role
- Sends JSON commands (human-friendly)
- Type-safe CommandDispatcher wraps WebSocketService

### Browser Dashboard
- Connects to UI port 7070
- Sends JSON commands (StatusGet, StreamStart, etc.)
- Receives JSON responses
- WebRTC video via separate signaling

## Files

**Core:**
- `src/core/network/WebSocketService.{h,cpp}` - Main service
- `src/core/network/BinaryProtocol.h` - MessageEnvelope, serialization helpers
- `src/core/CommandWithCallback.h` - CWC template

**Server:**
- `src/server/StateMachine.cpp::setupWebSocketService()` - Handler registration
- `src/server/network/CommandDeserializerJson.{h,cpp}` - JSON → ApiCommand

**UI:**
- `src/ui/state-machine/StateMachine.cpp::setupWebSocketService()` - Handler registration
- `src/ui/state-machine/network/CommandDeserializerJson.{h,cpp}` - JSON → UiApiCommand

**CLI:**
- `src/cli/CommandDispatcher.{h,cpp}` - Type-safe command interface

## Current Limitations

1. **libdatachannel buffering:** RenderMessages may arrive in bursts with ~2s initial delay. Investigate rtc::WebSocketConfiguration settings for tuning.

2. **Complex response types:** Some responses (PeersGet, TimerStatsGet) have nested structures that ReflectSerializer can't auto-serialize. Require custom toJson() implementations.

## Future Enhancements

1. **Binary CLI mode** - Optional `--binary` flag for performance testing without JSON overhead
2. **Connection pooling** - Reuse connections for multiple requests
3. **Compression** - zstd for large payloads (fractal snapshots, full world dumps)
4. **Peer-to-peer** - Server-to-server mesh networking using WebSocketService
5. **Request pipelining** - Send multiple commands before waiting for responses
