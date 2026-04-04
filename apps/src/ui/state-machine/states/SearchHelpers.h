#pragma once

#include "core/Result.h"
#include "core/UUID.h"
#include <variant>

namespace DirtSim {
namespace Ui {

class StateMachine;

/// Shared helpers for search-related UI states.
namespace SearchHelpers {

constexpr int kServerTimeoutMs = 2000;

Result<std::monostate, std::string> startPlanPlayback(StateMachine& sm, UUID planId);
Result<std::monostate, std::string> startSearch(StateMachine& sm);
void subscribeToBasicRender(StateMachine& sm);

} // namespace SearchHelpers
} // namespace Ui
} // namespace DirtSim
