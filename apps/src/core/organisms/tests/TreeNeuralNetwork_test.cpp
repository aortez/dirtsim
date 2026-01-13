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

    CommandExecutionResult execute(Tree& tree, World& world, const TreeCommand& cmd) override
    {
        commands_.push_back(cmd);
        return inner_->execute(tree, world, cmd);
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
    // Create tree via OrganismManager.
    OrganismId id = world->getOrganismManager().createTree(*world, 7, 10);
    Tree* tree = world->getOrganismManager().getTree(id);
    tree->setBrain(std::make_unique<NeuralNetBrain>(42));
    tree->setEnergy(200.0);

    // Swap in a recording processor.
    auto recorder =
        std::make_unique<RecordingCommandProcessor>(std::make_unique<TreeCommandProcessor>());
    RecordingCommandProcessor* recorder_ptr = recorder.get();
    tree->processor = std::move(recorder);

    std::cout << "Initial state (seed 2 cells above dirt):\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Wait for seed to fall.
    int landed_frame = waitForLanding(tree);
    ASSERT_GE(landed_frame, 0) << "Seed should fall and land";

    std::cout << "Seed landed at frame " << landed_frame << "\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Run simulation - the NN will produce commands.
    for (int i = 0; i < 500; ++i) {
        world->advanceTime(0.016);
    }

    std::cout << "\n=== Final state ===\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
    std::cout << "Tree age: " << tree->getAge() << "s\n";
    std::cout << "Tree cells: " << tree->getCells().size() << "\n";
    std::cout << "Tree energy: " << tree->getEnergy() << "\n";
    std::cout << "Commands executed: " << recorder_ptr->getCommandCount() << "\n";

    // Verify commands were produced.
    EXPECT_GT(recorder_ptr->getCommandCount(), 0u) << "Neural brain should produce commands";
    EXPECT_GT(tree->getAge(), 0.0) << "Tree should have aged";

    // Print command type distribution.
    std::map<std::string, int> command_counts;
    for (const auto& cmd : recorder_ptr->getCommands()) {
        std::visit(
            [&](auto&& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, GrowWoodCommand>)
                    command_counts["WOOD"]++;
                else if constexpr (std::is_same_v<T, GrowLeafCommand>)
                    command_counts["LEAF"]++;
                else if constexpr (std::is_same_v<T, GrowRootCommand>)
                    command_counts["ROOT"]++;
                else if constexpr (std::is_same_v<T, ReinforceCellCommand>)
                    command_counts["REINFORCE"]++;
                else if constexpr (std::is_same_v<T, ProduceSeedCommand>)
                    command_counts["SEED"]++;
                else if constexpr (std::is_same_v<T, WaitCommand>)
                    command_counts["WAIT"]++;
            },
            cmd);
    }

    std::cout << "\nCommand distribution:\n";
    for (const auto& [name, count] : command_counts) {
        std::cout << "  " << name << ": " << count << "\n";
    }
}
