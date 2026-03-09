#pragma once

#include "core/scenarios/ClockTimezone.h"

#include <chrono>
#include <ctime>

namespace DirtSim::ClockTimeResolver {

std::tm resolveClockTime(Config::ClockTimezone timezone, std::chrono::system_clock::time_point now);

} // namespace DirtSim::ClockTimeResolver
