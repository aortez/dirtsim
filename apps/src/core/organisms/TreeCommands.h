#pragma once

#include "core/Vector2i.h"
#include <cstdint>
#include <type_traits>
#include <variant>

namespace DirtSim {

// Energy costs are determined by TreeCommandProcessor, not by command callers.

// Instant commands (no execution time).
struct WaitCommand {};   // Do nothing this tick.
struct CancelCommand {}; // Cancel in-progress action.

// Action commands (have execution time and target position).
struct GrowWoodCommand {
    Vector2i target_pos;
    double execution_time_seconds = 3.0;
};

struct GrowLeafCommand {
    Vector2i target_pos;
    double execution_time_seconds = 0.5;
};

struct GrowRootCommand {
    Vector2i target_pos;
    double execution_time_seconds = 2.0;
};

struct ReinforceCellCommand {
    Vector2i position;
    double execution_time_seconds = 0.5;
};

struct ProduceSeedCommand {
    Vector2i position;
    double execution_time_seconds = 2.0;
};

// TreeCommand variant - the canonical definition of all tree commands.
// The order of alternatives defines the command indices used by neural networks.
using TreeCommand = std::variant<
    WaitCommand,          // index 0 - do nothing this tick
    CancelCommand,        // index 1 - cancel in-progress action
    GrowWoodCommand,      // index 2
    GrowLeafCommand,      // index 3
    GrowRootCommand,      // index 4
    ReinforceCellCommand, // index 5
    ProduceSeedCommand    // index 6
    >;

// TreeCommandType enum - named indices into the TreeCommand variant.
// Values MUST match the variant alternative order above.
enum class TreeCommandType : uint8_t {
    WaitCommand = 0,
    CancelCommand = 1,
    GrowWoodCommand = 2,
    GrowLeafCommand = 3,
    GrowRootCommand = 4,
    ReinforceCellCommand = 5,
    ProduceSeedCommand = 6
};

constexpr size_t NUM_TREE_COMMAND_TYPES = std::variant_size_v<TreeCommand>;

// Compile-time verification that TreeCommandType values match TreeCommand variant order.
static_assert(
    std::is_same_v<
        std::variant_alternative_t<static_cast<size_t>(TreeCommandType::WaitCommand), TreeCommand>,
        WaitCommand>);
static_assert(
    std::is_same_v<
        std::
            variant_alternative_t<static_cast<size_t>(TreeCommandType::CancelCommand), TreeCommand>,
        CancelCommand>);
static_assert(std::is_same_v<
              std::variant_alternative_t<
                  static_cast<size_t>(TreeCommandType::GrowWoodCommand),
                  TreeCommand>,
              GrowWoodCommand>);
static_assert(std::is_same_v<
              std::variant_alternative_t<
                  static_cast<size_t>(TreeCommandType::GrowLeafCommand),
                  TreeCommand>,
              GrowLeafCommand>);
static_assert(std::is_same_v<
              std::variant_alternative_t<
                  static_cast<size_t>(TreeCommandType::GrowRootCommand),
                  TreeCommand>,
              GrowRootCommand>);
static_assert(std::is_same_v<
              std::variant_alternative_t<
                  static_cast<size_t>(TreeCommandType::ReinforceCellCommand),
                  TreeCommand>,
              ReinforceCellCommand>);
static_assert(std::is_same_v<
              std::variant_alternative_t<
                  static_cast<size_t>(TreeCommandType::ProduceSeedCommand),
                  TreeCommand>,
              ProduceSeedCommand>);

// Convert a TreeCommand to its TreeCommandType using the variant index.
inline TreeCommandType getCommandType(const TreeCommand& cmd)
{
    return static_cast<TreeCommandType>(cmd.index());
}

} // namespace DirtSim
