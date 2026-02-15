#include "TrainingBestSnapshot.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const TrainingBestSnapshot::CommandSignatureCount& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, TrainingBestSnapshot::CommandSignatureCount& value)
{
    value = ReflectSerializer::from_json<TrainingBestSnapshot::CommandSignatureCount>(j);
}

void to_json(nlohmann::json& j, const TrainingBestSnapshot& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, TrainingBestSnapshot& value)
{
    value = ReflectSerializer::from_json<TrainingBestSnapshot>(j);
}

} // namespace Api
} // namespace DirtSim
