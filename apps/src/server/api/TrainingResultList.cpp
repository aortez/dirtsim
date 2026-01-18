#include "TrainingResultList.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {
namespace TrainingResultList {

void to_json(nlohmann::json& j, const Entry& entry)
{
    j = ReflectSerializer::to_json(entry);
}

void from_json(const nlohmann::json& j, Entry& entry)
{
    entry = ReflectSerializer::from_json<Entry>(j);
}

} // namespace TrainingResultList
} // namespace Api
} // namespace DirtSim
