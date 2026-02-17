#include "OrganismTracker.h"

namespace DirtSim {

void OrganismTracker::reset()
{
    history_.samples.clear();
    pathDistance_ = 0.0;
}

void OrganismTracker::track(double simTime, const Vector2d& position)
{
    if (!history_.samples.empty()) {
        const Vector2d delta = position - history_.samples.back().position;
        pathDistance_ += delta.mag();
    }

    history_.samples.push_back({ .simTime = simTime, .position = position });
}

} // namespace DirtSim
