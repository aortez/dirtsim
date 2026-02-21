#include "TrainingBestPlaybackFrame.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

void to_json(nlohmann::json& j, const TrainingBestPlaybackFrame& value)
{
    j = ReflectSerializer::to_json(value);
}

void from_json(const nlohmann::json& j, TrainingBestPlaybackFrame& value)
{
    value = ReflectSerializer::from_json<TrainingBestPlaybackFrame>(j);
}

} // namespace Api
} // namespace DirtSim
