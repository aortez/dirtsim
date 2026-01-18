#pragma once

#include "ApiMacros.h"
#include "CellGet.h"
#include "CellSet.h"
#include "ClockEventTrigger.h"
#include "DiagramGet.h"
#include "EvolutionStart.h"
#include "EvolutionStop.h"
#include "Exit.h"
#include "FingerDown.h"
#include "FingerMove.h"
#include "FingerUp.h"
#include "GenomeDelete.h"
#include "GenomeGet.h"
#include "GenomeGetBest.h"
#include "GenomeList.h"
#include "GenomeSet.h"
#include "GravitySet.h"
#include "PeersGet.h"
#include "PerfStatsGet.h"
#include "PhysicsSettingsGet.h"
#include "PhysicsSettingsSet.h"
#include "RenderFormatGet.h"
#include "RenderFormatSet.h"
#include "Reset.h"
#include "ScenarioConfigSet.h"
#include "ScenarioListGet.h"
#include "SeedAdd.h"
#include "SimRun.h"
#include "SimStop.h"
#include "SpawnDirtBall.h"
#include "StateGet.h"
#include "StatusGet.h"
#include "TimerStatsGet.h"
#include "TrainingResultAvailableAck.h"
#include "TrainingResultDiscard.h"
#include "TrainingResultGet.h"
#include "TrainingResultList.h"
#include "TrainingResultSave.h"
#include "WorldResize.h"
#include <concepts>
#include <nlohmann/json.hpp>
#include <string_view>
#include <variant>

namespace DirtSim {

/**
 * @brief Concept for types that represent API commands.
 *
 * An API command type must provide:
 * - A static name() method returning the command name
 * - A toJson() method for serialization
 *
 * This concept enables type-safe generic programming with API commands
 * and provides clear compiler error messages when constraints are violated.
 */
template <typename T>
concept ApiCommandType = requires(T cmd) {
    { cmd.toJson() } -> std::convertible_to<nlohmann::json>;
    { T::name() } -> std::convertible_to<std::string_view>;
};

/**
 * @brief Variant containing all API command types.
 */
using ApiCommand = std::variant<
    Api::CellGet::Command,
    Api::CellSet::Command,
    Api::ClockEventTrigger::Command,
    Api::DiagramGet::Command,
    Api::EvolutionStart::Command,
    Api::EvolutionStop::Command,
    Api::Exit::Command,
    Api::FingerDown::Command,
    Api::FingerMove::Command,
    Api::FingerUp::Command,
    Api::GenomeDelete::Command,
    Api::GenomeGet::Command,
    Api::GenomeGetBest::Command,
    Api::GenomeList::Command,
    Api::GenomeSet::Command,
    Api::GravitySet::Command,
    Api::PeersGet::Command,
    Api::PerfStatsGet::Command,
    Api::PhysicsSettingsGet::Command,
    Api::PhysicsSettingsSet::Command,
    Api::RenderFormatGet::Command,
    Api::RenderFormatSet::Command,
    Api::Reset::Command,
    Api::ScenarioConfigSet::Command,
    Api::ScenarioListGet::Command,
    Api::SeedAdd::Command,
    Api::SimRun::Command,
    Api::SimStop::Command,
    Api::SpawnDirtBall::Command,
    Api::StateGet::Command,
    Api::StatusGet::Command,
    Api::TimerStatsGet::Command,
    Api::TrainingResultAvailableAck::Command,
    Api::TrainingResultDiscard::Command,
    Api::TrainingResultGet::Command,
    Api::TrainingResultList::Command,
    Api::TrainingResultSave::Command,
    Api::WorldResize::Command>;

} // namespace DirtSim
