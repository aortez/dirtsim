#pragma once

#include "TreeCommands.h"
#include <string>

namespace DirtSim {

class Tree;
class World;

enum class CommandResult { SUCCESS, INSUFFICIENT_ENERGY, INVALID_TARGET, BLOCKED };

struct CommandExecutionResult {
    CommandResult result;
    std::string message;

    bool succeeded() const { return result == CommandResult::SUCCESS; }
};

// Interface for processing tree commands.
class ITreeCommandProcessor {
public:
    virtual ~ITreeCommandProcessor() = default;
    virtual CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) = 0;
};

// Default implementation that validates and executes commands.
class TreeCommandProcessor : public ITreeCommandProcessor {
public:
    CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) override;
};

} // namespace DirtSim
