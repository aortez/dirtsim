#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Benchmark scenario config - performance testing with complex physics.
 */
struct BenchmarkConfig {
    using serialize = zpp::bits::members<0>;
};

} // namespace DirtSim
