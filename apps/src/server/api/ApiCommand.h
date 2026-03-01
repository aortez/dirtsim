#pragma once

#include "ApiMacros.h"
#include "CellGet.h"
#include "CellSet.h"
#include "ClockEventTrigger.h"
#include "DiagramGet.h"
#include "EventSubscribe.h"
#include "EvolutionStart.h"
#include "EvolutionStop.h"
#include "Exit.h"
#include "FingerDown.h"
#include "FingerMove.h"
#include "FingerUp.h"
#include "GenomeDelete.h"
#include "GenomeGet.h"
#include "GenomeList.h"
#include "GenomeSet.h"
#include "GravitySet.h"
#include "NesInputSet.h"
#include "PerfStatsGet.h"
#include "PhysicsSettingsGet.h"
#include "PhysicsSettingsSet.h"
#include "RenderFormatGet.h"
#include "RenderFormatSet.h"
#include "RenderStreamConfigSet.h"
#include "Reset.h"
#include "ScenarioListGet.h"
#include "ScenarioSwitch.h"
#include "SeedAdd.h"
#include "SimRun.h"
#include "SimStop.h"
#include "SpawnDirtBall.h"
#include "StateGet.h"
#include "StatusGet.h"
#include "TimerStatsGet.h"
#include "TrainingBestSnapshotGet.h"
#include "TrainingResultDelete.h"
#include "TrainingResultDiscard.h"
#include "TrainingResultGet.h"
#include "TrainingResultList.h"
#include "TrainingResultSave.h"
#include "TrainingResultSet.h"
#include "UserSettingsGet.h"
#include "UserSettingsPatch.h"
#include "UserSettingsReset.h"
#include "UserSettingsSet.h"
#include "WebSocketAccessSet.h"
#include "WebUiAccessSet.h"
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
    Api::EventSubscribe::Command,
    Api::EvolutionStart::Command,
    Api::EvolutionStop::Command,
    Api::Exit::Command,
    Api::FingerDown::Command,
    Api::FingerMove::Command,
    Api::FingerUp::Command,
    Api::GenomeDelete::Command,
    Api::GenomeGet::Command,
    Api::GenomeList::Command,
    Api::GenomeSet::Command,
    Api::GravitySet::Command,
    Api::NesInputSet::Command,
    Api::PerfStatsGet::Command,
    Api::PhysicsSettingsGet::Command,
    Api::PhysicsSettingsSet::Command,
    Api::RenderFormatGet::Command,
    Api::RenderFormatSet::Command,
    Api::RenderStreamConfigSet::Command,
    Api::Reset::Command,
    Api::ScenarioListGet::Command,
    Api::ScenarioSwitch::Command,
    Api::SeedAdd::Command,
    Api::SimRun::Command,
    Api::SimStop::Command,
    Api::SpawnDirtBall::Command,
    Api::StateGet::Command,
    Api::StatusGet::Command,
    Api::TimerStatsGet::Command,
    Api::TrainingBestSnapshotGet::Command,
    Api::UserSettingsGet::Command,
    Api::UserSettingsPatch::Command,
    Api::UserSettingsReset::Command,
    Api::UserSettingsSet::Command,
    Api::TrainingResultDiscard::Command,
    Api::TrainingResultDelete::Command,
    Api::TrainingResultGet::Command,
    Api::TrainingResultList::Command,
    Api::TrainingResultSave::Command,
    Api::TrainingResultSet::Command,
    Api::WebSocketAccessSet::Command,
    Api::WebUiAccessSet::Command,
    Api::WorldResize::Command>;

} // namespace DirtSim
