#pragma once

#include <string>
#include <vector>

namespace DirtSim {

struct GenomeSegment {
    std::string name;
    int size = 0;
};

/**
 * Describes the layout of a genome as a sequence of named segments.
 * Used by mutation to distribute budget across layers.
 */
struct GenomeLayout {
    std::vector<GenomeSegment> segments;

    int totalSize() const
    {
        int total = 0;
        for (const auto& seg : segments) {
            total += seg.size;
        }
        return total;
    }
};

} // namespace DirtSim
