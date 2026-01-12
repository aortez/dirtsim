#pragma once

namespace DirtSim {
namespace Ui {

class StateMachine;

namespace State {

// Forward declarations.
struct Disconnected;
struct Paused;
struct Shutdown;
struct SimRunning;
struct StartMenu;
struct Startup;
struct Training;

// Forward declaration of wrapper (definition in State.h after state includes).
class Any;

} // namespace State
} // namespace Ui
} // namespace DirtSim
