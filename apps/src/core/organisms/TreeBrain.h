#pragma once

#include "TreeCommands.h"
#include "TreeSensoryData.h"

namespace DirtSim {

/**
 * Abstract interface for tree decision-making.
 *
 * The brain is called every tick and returns a TreeCommand:
 * - WaitCommand: Do nothing this tick (continue current action or stay idle).
 * - CancelCommand: Cancel the in-progress action.
 * - Action commands: Start an action (only works if idle).
 */
class TreeBrain {
public:
    virtual ~TreeBrain() = default;

    /**
     * Called every tick to make a decision.
     *
     * The brain receives sensory data including:
     * - current_action: what action is in progress (nullopt if idle)
     * - action_progress: how far along (0.0 to 1.0)
     *
     * Returns a TreeCommand:
     * - WaitCommand: do nothing this tick
     * - CancelCommand: cancel current action
     * - Action command: start an action (ignored if busy)
     */
    virtual TreeCommand decide(const TreeSensoryData& sensory) = 0;
};

} // namespace DirtSim
