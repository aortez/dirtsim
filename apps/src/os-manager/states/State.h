#pragma once

#include "Error.h"
#include "Idle.h"
#include "Rebooting.h"
#include "Startup.h"
#include "StateForward.h"

#include <string>
#include <variant>

namespace DirtSim {
namespace OsManager {
namespace State {

class Any {
public:
    using Variant = std::variant<Startup, Idle, Error, Rebooting>;

    template <typename T>
    Any(T&& state) : variant_(std::forward<T>(state))
    {}

    Any() = default;

    Variant& getVariant() { return variant_; }
    const Variant& getVariant() const { return variant_; }

private:
    Variant variant_;
};

inline std::string getCurrentStateName(const Any& state)
{
    return std::visit([](const auto& s) { return std::string(s.name()); }, state.getVariant());
}

} // namespace State
} // namespace OsManager
} // namespace DirtSim
