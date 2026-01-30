#pragma once

#include "core/organisms/TreeResourceTotals.h"
#include <optional>

namespace DirtSim {

class Tree;
struct FitnessContext;

class TreeEvaluator {
public:
    void reset();
    void start();
    void update(const Tree& tree);

    static double evaluate(const FitnessContext& context);

    double getMaxEnergy() const;
    const std::optional<TreeResourceTotals>& getResourceTotals() const;

private:
    double maxEnergy_ = 0.0;
    std::optional<TreeResourceTotals> resourceTotals_;
};

} // namespace DirtSim
