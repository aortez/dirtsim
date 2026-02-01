#pragma once

#include "NoteOff.h"
#include "NoteOn.h"
#include "StatusGet.h"
#include <variant>

namespace DirtSim {
namespace AudioApi {

using AudioApiCommand = std::variant<NoteOff::Command, NoteOn::Command, StatusGet::Command>;

} // namespace AudioApi
} // namespace DirtSim
