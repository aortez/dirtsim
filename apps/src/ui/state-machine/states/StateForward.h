#pragma once

namespace DirtSim {
namespace Ui {

class StateMachine;

namespace State {

// Forward declarations.
struct Disconnected;
struct NetworkScanner;
struct NetworkSettings;
struct NetworkWifi;
struct NetworkWifiConnecting;
struct NetworkWifiDetails;
struct NetworkWifiPassword;
struct Paused;
struct Shutdown;
struct SimRunning;
struct StartMenu;
struct Startup;
struct Synth;
struct SynthConfig;
struct TrainingActive;
struct TrainingIdle;
struct TrainingUnsavedResult;

// Forward declaration of wrapper (definition in State.h after state includes).
class Any;

} // namespace State
} // namespace Ui
} // namespace DirtSim
