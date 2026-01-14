#include "EvolutionProgress.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

nlohmann::json EvolutionProgress::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace Api
} // namespace DirtSim
