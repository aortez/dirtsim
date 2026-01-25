#pragma once

#include <cstdint>

namespace DirtSim {

enum class GenomeSortKey : uint8_t {
    CreatedTimestamp = 0,
    Fitness = 1,
    Generation = 2,
};

enum class GenomeSortDirection : uint8_t {
    Desc = 0,
    Asc = 1,
};

} // namespace DirtSim
