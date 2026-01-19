#pragma once

#include "os-manager/api/Reboot.h"
#include "os-manager/api/RestartServer.h"
#include "os-manager/api/RestartUi.h"
#include "os-manager/api/StartServer.h"
#include "os-manager/api/StartUi.h"
#include "os-manager/api/StopServer.h"
#include "os-manager/api/StopUi.h"
#include "os-manager/api/SystemStatus.h"
#include <concepts>
#include <string>
#include <variant>

namespace DirtSim {
namespace OsManager {

template <typename T>
concept HasEventName = requires {
    { T::name() } -> std::convertible_to<const char*>;
};

class Event {
public:
    using Variant = std::variant<
        OsApi::Reboot::Cwc,
        OsApi::RestartServer::Cwc,
        OsApi::RestartUi::Cwc,
        OsApi::StartServer::Cwc,
        OsApi::StartUi::Cwc,
        OsApi::StopServer::Cwc,
        OsApi::StopUi::Cwc,
        OsApi::SystemStatus::Cwc>;

    template <typename T>
    Event(T&& event) : variant_(std::forward<T>(event))
    {}

    Event() = default;

    Variant& getVariant() { return variant_; }
    const Variant& getVariant() const { return variant_; }

private:
    Variant variant_;
};

inline std::string getEventName(const Event& event)
{
    return std::visit([](auto&& e) { return std::string(e.name()); }, event.getVariant());
}

} // namespace OsManager
} // namespace DirtSim
