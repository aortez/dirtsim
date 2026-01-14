#pragma once

/**
 * @file StateLifecycle.h
 * @brief Shared state machine lifecycle helpers for onEnter/onExit dispatch.
 *
 * Provides SFINAE-based invocation of state lifecycle methods. States can define:
 * - Any onEnter(StateMachine&) - returns state to be in (self or redirect)
 * - void onEnter(StateMachine&) - stays in current state
 * - void onExit(StateMachine&) - cleanup on exit
 */

#include <string>
#include <variant>

namespace DirtSim {

// Invoke onEnter if present. Returns state to be in (same or redirect).
template <typename StateAny, typename StateMachine>
StateAny invokeOnEnter(StateAny&& state, StateMachine& sm)
{
    std::visit(
        [&sm, &state](auto& s) {
            if constexpr (requires {
                              { s.onEnter(sm) } -> std::convertible_to<StateAny>;
                          }) {
                state = s.onEnter(sm);
            }
            else if constexpr (requires { s.onEnter(sm); }) {
                s.onEnter(sm);
            }
        },
        state.getVariant());
    return std::move(state);
}

// Invoke onExit if present.
template <typename StateAny, typename StateMachine>
void invokeOnExit(StateAny& state, StateMachine& sm)
{
    std::visit(
        [&sm](auto& s) {
            if constexpr (requires { s.onExit(sm); }) {
                s.onExit(sm);
            }
        },
        state.getVariant());
}

// Get name of current state. Requires states to have static name() method.
template <typename StateAny>
std::string getStateName(const StateAny& state)
{
    return std::visit([](const auto& s) { return std::string(s.name()); }, state.getVariant());
}

} // namespace DirtSim
