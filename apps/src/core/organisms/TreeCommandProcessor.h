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
    virtual CommandExecutionResult validate(Tree& tree, World& world, const TreeCommand& cmd) = 0;
    virtual CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) = 0;
    virtual double getEnergyCost(const TreeCommand& cmd) const = 0;
};

// Default implementation that validates and executes commands.
class TreeCommandProcessor : public ITreeCommandProcessor {
public:
    CommandExecutionResult validate(Tree& tree, World& world, const TreeCommand& cmd) override;
    CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) override;
    double getEnergyCost(const TreeCommand& cmd) const override;
};

} // namespace DirtSim
