#include "core/scenarios/nes/NesSuperMarioBrosResponseTelemetry.h"

namespace DirtSim {

const char* toString(NesSuperMarioBrosResponseKind kind)
{
    switch (kind) {
        case NesSuperMarioBrosResponseKind::Jump:
            return "Jump";
        case NesSuperMarioBrosResponseKind::MoveLeft:
            return "MoveLeft";
        case NesSuperMarioBrosResponseKind::MoveRight:
            return "MoveRight";
        case NesSuperMarioBrosResponseKind::Duck:
            return "Duck";
    }

    return "Unknown";
}

const char* toString(NesSuperMarioBrosResponseContext context)
{
    switch (context) {
        case NesSuperMarioBrosResponseContext::StandingJump:
            return "StandingJump";
        case NesSuperMarioBrosResponseContext::RunningJump:
            return "RunningJump";
        case NesSuperMarioBrosResponseContext::GroundedStart:
            return "GroundedStart";
        case NesSuperMarioBrosResponseContext::GroundedTurnaround:
            return "GroundedTurnaround";
        case NesSuperMarioBrosResponseContext::GroundedDuck:
            return "GroundedDuck";
    }

    return "Unknown";
}

const char* toString(NesSuperMarioBrosResponseMilestone milestone)
{
    switch (milestone) {
        case NesSuperMarioBrosResponseMilestone::Acknowledge:
            return "Acknowledge";
        case NesSuperMarioBrosResponseMilestone::Commit:
            return "Commit";
        case NesSuperMarioBrosResponseMilestone::Motion:
            return "Motion";
    }

    return "Unknown";
}

} // namespace DirtSim
