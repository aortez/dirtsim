#pragma once

namespace DirtSim {
namespace Server {

class StateMachine;

namespace State {

// Forward declarations.
struct Error;
struct Evolution;
struct Idle;
struct PreStartup;
struct Shutdown;
struct SimPaused;
struct SimRunning;
struct Startup;

// Forward declaration of wrapper (definition in State.h after state includes).
class Any;

} // namespace State
} // namespace Server
} // namespace DirtSim
