#pragma once

// This file aggregates all UI state definitions.
// Each state has its own header file for better organization.

#include "Disconnected.h"
#include "Network.h"
#include "Paused.h"
#include "Shutdown.h"
#include "SimRunning.h"
#include "StartMenu.h"
#include "Startup.h"
#include "StateForward.h"
#include "Synth.h"
#include "SynthConfig.h"
#include "Training.h"

namespace DirtSim {
namespace Ui {
namespace State {

/**
 * @brief Forward-declarable state wrapper class.
 *
 * Wraps the state variant to enable forward declaration,
 * reducing compilation dependencies.
 */
class Any {
public:
    using Variant = std::variant<
        Disconnected,
        Network,
        Paused,
        Shutdown,
        SimRunning,
        StartMenu,
        Startup,
        Synth,
        SynthConfig,
        Training>;

    template <typename T>
    Any(T&& state) : variant_(std::forward<T>(state))
    {}

    Any() = default;

    Variant& getVariant() { return variant_; }
    const Variant& getVariant() const { return variant_; }

private:
    Variant variant_;
};

/**
 * @brief Get the name of the current state.
 * Requires complete state definitions, so defined here after all includes.
 */
inline std::string getCurrentStateName(const Any& state)
{
    return std::visit([](const auto& s) { return std::string(s.name()); }, state.getVariant());
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
