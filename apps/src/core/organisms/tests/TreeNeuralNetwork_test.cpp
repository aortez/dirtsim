#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/TreeCommandProcessor.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include <gtest/gtest.h>
#include <iostream>
#include <map>

using namespace DirtSim;

// Recording processor that wraps another processor and logs all commands.
class RecordingCommandProcessor : public ITreeCommandProcessor {
public:
    explicit RecordingCommandProcessor(std::unique_ptr<ITreeCommandProcessor> inner)
        : inner_(std::move(inner))
    {}

    CommandExecutionResult validate(Tree& tree, World& world, const TreeCommand& cmd) override
    {
        commands_.push_back(cmd);
        return inner_->validate(tree, world, cmd);
    }

    CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) override
    {
        return inner_->execute(tree, world, cmd);
    }

    double getEnergyCost(const TreeCommand& cmd) const override
    {
        return inner_->getEnergyCost(cmd);
    }

    const std::vector<TreeCommand>& getCommands() const { return commands_; }
    size_t getCommandCount() const { return commands_.size(); }

private:
    std::unique_ptr<ITreeCommandProcessor> inner_;
    std::vector<TreeCommand> commands_;
};

class TreeNeuralNetworkTest : public ::testing::Test {
protected:
    void SetUp() override { createWorld(); }

    void createWorld()
    {
        // Create a 15x15 world (matches neural grid size).
        world = std::make_unique<World>(15, 15);

        // Clear world to air.
        for (uint32_t y = 0; y < 15; ++y) {
            for (uint32_t x = 0; x < 15; ++x) {
                world->getData().at(x, y) = Cell();
            }
        }

        // Add dirt at bottom 3 rows (y=12,13,14).
        for (uint32_t y = 12; y < 15; ++y) {
            for (uint32_t x = 0; x < 15; ++x) {
                world->addMaterialAtCell(
                    { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                    Material::EnumType::Dirt,
                    1.0);
            }
        }
    }

    // Run simulation until seed lands, returns frame count.
    int waitForLanding(Tree* tree)
    {
        Vector2i last_pos = tree->getAnchorCell();
        int frame = 0;
        int frames_stationary = 0;

        while (frame < 200) {
            world->advanceTime(0.016);
            frame++;

            Vector2i current_pos = tree->getAnchorCell();
            if (current_pos.y != last_pos.y) {
                last_pos = current_pos;
                frames_stationary = 0;
            }
            else {
                frames_stationary++;
            }

            // Check if seed has landed (on or near dirt, stationary for a few frames).
            if (current_pos.y >= 11 && frames_stationary > 10) {
                return frame;
            }
        }
        return -1; // Failed to land.
    }

    std::unique_ptr<World> world;
};

TEST_F(TreeNeuralNetworkTest, NeuralBrainProducesCommands)
{
    bool success = false;
    size_t total_commands = 0;

    for (int trial = 0; trial < 10; ++trial) {
        uint32_t seed = 42 + trial;

        // Reset world for each trial.
        world = std::make_unique<World>(15, 15);
        for (uint32_t y = 0; y < 15; ++y) {
            for (uint32_t x = 0; x < 15; ++x) {
                world->getData().at(x, y) = Cell();
            }
        }
        for (uint32_t y = 12; y < 15; ++y) {
            for (uint32_t x = 0; x < 15; ++x) {
                world->addMaterialAtCell(
                    { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                    Material::EnumType::Dirt,
                    1.0);
            }
        }

        // Create tree with different seed.
        OrganismId id = world->getOrganismManager().createTree(*world, 7, 10);
        Tree* tree = world->getOrganismManager().getTree(id);
        tree->setBrain(std::make_unique<NeuralNetBrain>(seed));
        tree->setEnergy(200.0);

        auto recorder =
            std::make_unique<RecordingCommandProcessor>(std::make_unique<TreeCommandProcessor>());
        RecordingCommandProcessor* recorder_ptr = recorder.get();
        tree->processor = std::move(recorder);

        // Wait for seed to fall.
        int landed_frame = waitForLanding(tree);
        if (landed_frame < 0) continue;

        // Run simulation.
        for (int i = 0; i < 500; ++i) {
            world->advanceTime(0.016);
        }

        size_t command_count = recorder_ptr->getCommandCount();
        total_commands += command_count;

        if (command_count > 0) {
            success = true;
            std::cout << "Trial " << trial << " (seed " << seed << "): " << command_count
                      << " commands\n";
        }
    }

    std::cout << "Total commands across all trials: " << total_commands << "\n";
    EXPECT_TRUE(success) << "At least one trial should produce commands";
    EXPECT_GT(total_commands, 0u) << "Total commands should be > 0";
}
