#pragma once

#include "spdlog/spdlog.h"
#include <cstdlib>

/**
 * Runtime assertion that works in both debug and release builds.
 *
 * Unlike standard assert(), DIRTSIM_ASSERT is never compiled out.
 * Use for critical invariants that indicate bugs if violated.
 *
 * When an assertion fails:
 * - Logs a CRITICAL message with file, line, and condition
 * - Aborts the program immediately
 *
 * Example:
 *   DIRTSIM_ASSERT(duck->anchor_cell == cell_position,
 *                  "Duck anchor must match cell position");
 */
#define DIRTSIM_ASSERT(condition, message)                                                  \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            spdlog::critical("ASSERTION FAILED: {} at {}:{}", message, __FILE__, __LINE__); \
            spdlog::critical("  Condition: {}", #condition);                                \
            std::abort();                                                                   \
        }                                                                                   \
    } while (0)
