#pragma once

namespace DirtSim {

/**
 * @brief Base class for World calculator classes.
 *
 * Provides common constants and consistent patterns for calculator classes.
 */
class WorldCalculatorBase {
public:
    // Default constructor - calculators are now stateless.
    WorldCalculatorBase() = default;

    // Virtual destructor for proper cleanup.
    virtual ~WorldCalculatorBase() = default;

    // Common constants used across calculator classes.
    static constexpr double MIN_MATTER_THRESHOLD = 0.001; // Minimum matter to process.
};

} // namespace DirtSim