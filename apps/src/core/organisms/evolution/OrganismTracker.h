#pragma once

#include "core/Vector2.h"
#include <vector>

namespace DirtSim {

struct OrganismTrackingSample {
    double simTime = 0.0;
    Vector2d position{ 0.0, 0.0 };
};

struct OrganismTrackingHistory {
    std::vector<OrganismTrackingSample> samples;
};

class OrganismTracker {
public:
    void reset();
    void track(double simTime, const Vector2d& position);

    double getPathDistance() const { return pathDistance_; }
    const OrganismTrackingHistory& getHistory() const { return history_; }

private:
    OrganismTrackingHistory history_;
    double pathDistance_ = 0.0;
};

} // namespace DirtSim
