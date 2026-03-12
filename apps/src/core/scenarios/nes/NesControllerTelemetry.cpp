#include "NesControllerTelemetry.h"

#include "core/ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const NesControllerTelemetry& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, NesControllerTelemetry& value)
{
    value = ReflectSerializer::from_json<NesControllerTelemetry>(j);
}

} // namespace DirtSim
