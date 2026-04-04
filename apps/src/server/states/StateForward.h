#pragma once

namespace DirtSim {
namespace Server {

class StateMachine;

namespace State {

// Forward declarations.
struct Error;
struct Evolution;
struct Idle;
struct PlanPlayback;
struct PreStartup;
struct SearchActive;
struct Shutdown;
struct SimPaused;
struct SimRunning;
struct Startup;
struct UnsavedTrainingResult;

// Forward declaration of wrapper (definition in State.h after state includes).
class Any;

} // namespace State
} // namespace Server
} // namespace DirtSim
