#pragma once

#include "MasterVolumeSet.h"
#include "NoteOff.h"
#include "NoteOn.h"
#include "StatusGet.h"
#include <variant>

namespace DirtSim {
namespace AudioApi {

using AudioApiCommand =
    std::variant<MasterVolumeSet::Command, NoteOff::Command, NoteOn::Command, StatusGet::Command>;

} // namespace AudioApi
} // namespace DirtSim
